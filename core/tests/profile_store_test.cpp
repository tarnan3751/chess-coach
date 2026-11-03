// Unit tests for chesscoach/profile_store.hpp (core; unit; docs/database.md §12): creating a
// profiles.db at the current version with the Default profile, every ProfileError, the migration
// framework over longer test lists (in order, one transaction each, a failing script, a newer file
// refused unmodified), the files refused without being touched, the engine cache's hit, miss and
// replace rules, the spar sessions saved, read and removed with their profile, the repertoire's
// choices and the cards they keep in step (§12.4: validation, the card rules, the import, the
// repair, the due order, the reviews), the export and the import (§12.5) with their refusals, a
// journal a stopped writer left rolled back before an import, files locked by another connection,
// the committed version 1 and 2 fixtures migrated to version 3, and the committed version 3
// fixture opened unchanged. The clock is injected.
#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/profile_store.hpp>
#include <chesscoach/repertoire.hpp>
#include <chesscoach/scheduler.hpp>
#include <chesscoach/spar.hpp>

#include "spar_fixture.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using chesscoach::CachedSearch;
using chesscoach::Card;
using chesscoach::CardSync;
using chesscoach::ChoiceChange;
using chesscoach::ChoiceImport;
using chesscoach::Color;
using chesscoach::Grade;
using chesscoach::Key;
using chesscoach::Migration;
using chesscoach::OpeningStore;
using chesscoach::Phase;
using chesscoach::Position;
using chesscoach::Profile;
using chesscoach::ProfileError;
using chesscoach::ProfileStore;
using chesscoach::ProviderError;
using chesscoach::RepertoireChoice;
using chesscoach::RepertoireNode;
using chesscoach::ReplyProvider;
using chesscoach::Review;
using chesscoach::SearchLine;
using chesscoach::SearchResult;
using chesscoach::SparSession;
using chesscoach::SparSessionRow;
using chesscoach::Tier;
using chesscoach::testing::SparFixture;

namespace {

// A data directory of its own for each test, under the build directory, removed when it ends.
class DataDir {
  public:
    explicit DataDir(std::string_view name)
        : path_(std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / "profiles" / name),
          file_(path_ / "profiles.db") {
        std::filesystem::remove_all(path_);
    }
    DataDir(const DataDir&) = delete;
    DataDir& operator=(const DataDir&) = delete;
    DataDir(DataDir&&) = delete;
    DataDir& operator=(DataDir&&) = delete;
    ~DataDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    [[nodiscard]] const std::filesystem::path& file() const { return file_; }

  private:
    std::filesystem::path path_;
    std::filesystem::path file_;
};

// A clock that says what the test sets.
struct TestClock {
    std::int64_t now = 1000;
    [[nodiscard]] chesscoach::Clock clock() {
        return [this] { return now; };
    }
};

std::string read_bytes(const std::filesystem::path& path) {
    const std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << bytes;
}

ProfileStore open_store(const DataDir& dir, TestClock& clock) {
    auto store = ProfileStore::open(dir.path(), clock.clock());
    REQUIRE(store.has_value());
    return std::move(*store);
}

ProfileError open_error(const DataDir& dir,
                        std::span<const Migration> migrations = chesscoach::profile_migrations()) {
    TestClock clock;
    const auto store = ProfileStore::open(dir.path(), clock.clock(), migrations);
    REQUIRE_FALSE(store.has_value());
    return store.error();
}

// SQLite's open flags are ints, combined with | as its API documents.
int create_flags() {
    return SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE; // NOLINT(bugprone-signed-bitwise)
}

SearchLine cp_line(int multipv, int depth, int cp, std::vector<std::string> pv) {
    return {
        .multipv = multipv,
        .depth = depth,
        .score_cp = cp,
        .score_mate = {},
        .pv = std::move(pv),
    };
}

SearchResult result_of(std::vector<SearchLine> lines) {
    return {
        .lines = std::move(lines),
        .best_move = "e2e4",
        .engine_name = "Stockfish 19",
        .limited = false,
        .elo = {},
        .stopped = false,
    };
}

constexpr Key start_key = 0x8b26ecc3ca52d0bbULL;

// Plays the scripted moves in turn, for sessions off book.
class Scripted final : public ReplyProvider {
  public:
    explicit Scripted(std::vector<std::string> moves) : moves_(std::move(moves)) {}
    tl::expected<std::string, ProviderError> reply(const chesscoach::Position& /*at*/) override {
        if (next_ == moves_.size()) {
            return tl::unexpected(ProviderError{.message = "no more moves"});
        }
        return moves_.at(next_++);
    }

  private:
    std::vector<std::string> moves_;
    std::size_t next_ = 0;
};

OpeningStore open_openings(const SparFixture& fixture) {
    auto store = OpeningStore::open(fixture.file());
    REQUIRE(store.has_value());
    return std::move(*store);
}

// A session that ends by mate at move 3, off book from the start (tier 0 of the spar fixture):
// 1. f3 e5 2. g4 Qh4#, the user playing black.
SparSession mated_session(const OpeningStore& openings, std::uint64_t seed) {
    auto session = SparSession::start(openings, Tier::under_1200, Color::black, seed);
    REQUIRE(session.has_value());
    Scripted coach({"f2f3", "g2g4"});
    REQUIRE(session->coach_reply(coach).has_value());
    REQUIRE(session->play_user_move("e7e5").has_value());
    REQUIRE(session->coach_reply(coach).has_value());
    REQUIRE(session->play_user_move("d8h4").has_value());
    REQUIRE(session->phase() == Phase::ended);
    return std::move(*session);
}

// The row the version 2 fixture holds: mated_session(seed 7), saved at 4000, ended at 4100.
SparSessionRow fixture_session() {
    return {
        .id = 1,
        .profile_id = 1,
        .tier = Tier::under_1200,
        .colour = Color::black,
        .seed = 7,
        .moves = {"f2f3", "e7e5", "g2g4", "d8h4"},
        .interruptions = 0,
        .phase = Phase::ended,
        .result = "0-1",
        .started_at = 4000,
        .ended_at = 4100,
        .end_move = 25,
        .min_games = 10,
        .data_version = 0,
    };
}

// Test migrations after the real version 1: version 2 adds a table and calls chesscoach_now(),
// version 3 alters it.
constexpr Migration version_2{
    .version = 2,
    .sql = "CREATE TABLE extra (made_at INTEGER NOT NULL);\n"
           "INSERT INTO extra VALUES (chesscoach_now());",
};
constexpr Migration version_3{.version = 3, .sql = "ALTER TABLE extra ADD COLUMN note TEXT;"};
constexpr Migration version_4{.version = 4, .sql = "CREATE TABLE later (x INTEGER);"};
constexpr Migration failing_version_3{
    .version = 3,
    .sql = "CREATE TABLE half (x INTEGER); INSERT INTO no_such_table VALUES (1);",
};

std::array<Migration, 3> three_versions() {
    return {chesscoach::profile_migrations().front(), version_2, version_3};
}

// One version past the real ones.
std::array<Migration, 4> four_versions() {
    return {chesscoach::profile_migrations().front(), version_2, version_3, version_4};
}

std::array<Migration, 3> failing_third() {
    return {chesscoach::profile_migrations().front(), version_2, failing_version_3};
}

// The search the version 1 fixture caches for the start position at three lines.
SearchResult fixture_search() {
    return result_of({
        cp_line(1, 20, 31, {"e2e4", "e7e5", "g1f3"}),
        cp_line(2, 20, 24, {"d2d4", "g8f6"}),
        SearchLine{.multipv = 3, .depth = 21, .score_cp = {}, .score_mate = 4, .pv = {"g1f3"}},
    });
}

CachedSearch cached(std::vector<SearchLine> lines, std::int64_t created_at) {
    return {.lines = std::move(lines), .engine = "Stockfish 19", .created_at = created_at};
}

// The choice of the move at the position the UCI line reaches from the start position.
RepertoireChoice choice_after(const std::vector<std::string>& line, std::string move) {
    auto position = Position::start();
    for (const auto& played : line) {
        REQUIRE(position.play_uci(played).has_value());
    }
    return {.key = position.key(), .fen = position.fen(), .move = std::move(move)};
}

// The choices of the version 3 fixture: Default's white 1. e4 and 2. Nf3 after 1... e5, and
// Club's black 1... c5 after 1. e4, all made at 5000.
RepertoireChoice white_start() { return choice_after({}, "e2e4"); }
RepertoireChoice white_after_e5() { return choice_after({"e2e4", "e7e5"}, "g1f3"); }
RepertoireChoice black_after_e4() { return choice_after({"e2e4"}, "c7c5"); }

// The reviews of the fixture's card for 1. e4: good at 6000, hard two days later.
std::vector<Review> fixture_reviews() {
    return {
        Review{.grade = Grade::good, .reviewed_at = 6000},
        Review{.grade = Grade::hard, .reviewed_at = 6000 + (2 * chesscoach::seconds_per_day)},
    };
}

std::vector<RepertoireNode> by_key(std::vector<RepertoireNode> nodes) {
    std::ranges::sort(nodes, [](const RepertoireNode& a, const RepertoireNode& b) {
        return a.choice.key < b.choice.key;
    });
    return nodes;
}

// The card of the position, which the test requires there to be.
Card card_at(const ProfileStore& store, std::int64_t profile_id, Color colour, Key key) {
    const auto cards = store.cards(profile_id, colour);
    const auto found = std::ranges::find(cards, key, &Card::key);
    REQUIRE(found != cards.end());
    return *found;
}

// A new card for the node, as the store makes it at created_at.
Card new_card_for(std::int64_t id, std::int64_t profile_id, Color colour,
                  const RepertoireChoice& choice, std::int64_t created_at) {
    return {
        .id = id,
        .profile_id = profile_id,
        .colour = colour,
        .key = choice.key,
        .move = choice.move,
        .state = chesscoach::new_card(created_at),
        .created_at = created_at,
    };
}

// Copies the profiles.db at from to to as a writer stopped mid-transaction leaves it: the file,
// with pages of an uncommitted transaction already written through a small cache (a profile
// 'Uncommitted' and 2000 engine cache rows), and its journal holding those pages as they were,
// copied while the transaction is open; the transaction is then rolled back at from.
void copy_mid_transaction(const std::filesystem::path& from, const std::filesystem::path& to) {
    SQLite::Database writer(from, SQLite::OPEN_READWRITE);
    writer.exec("PRAGMA cache_size = 10");
    writer.exec("BEGIN");
    writer.exec("INSERT INTO profiles (name, tier, created_at) VALUES ('Uncommitted', 1, 1)");
    writer.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 2000) "
                "INSERT INTO engine_cache (key, lines, line, depth, score_cp, score_mate, pv, "
                "engine, created_at) "
                "SELECT i, 1, 1, 1, 0, NULL, hex(randomblob(200)), 'x', 0 FROM n");
    auto journal = from;
    journal += "-journal";
    REQUIRE(std::filesystem::file_size(journal) > 0);
    std::filesystem::create_directories(to.parent_path());
    std::filesystem::copy_file(from, to);
    auto copied_journal = to;
    copied_journal += "-journal";
    std::filesystem::copy_file(journal, copied_journal);
    writer.exec("ROLLBACK");
}

} // namespace

TEST_CASE("A new data directory gets profiles.db at the current version with the Default profile",
          "[profiles]") {
    const DataDir dir("fresh");
    TestClock clock;
    const auto store = open_store(dir, clock);
    CHECK(std::filesystem::is_regular_file(dir.file()));
    CHECK(store.schema_version() == 3);
    CHECK(chesscoach::profile_schema_version == 3);
    CHECK(store.profiles() ==
          std::vector{
              Profile{.id = 1, .name = "Default", .tier = Tier::r1400_1599, .created_at = 1000}});
}

TEST_CASE("A zero-length profiles.db is created by every script", "[profiles]") {
    const DataDir dir("zero-length");
    write_bytes(dir.file(), "");
    TestClock clock;
    const auto store = open_store(dir, clock);
    CHECK(store.schema_version() == 3);
    CHECK(store.profiles().size() == 1);
}

TEST_CASE("Profiles are created, renamed, re-tiered and removed through the store", "[profiles]") {
    const DataDir dir("crud");
    TestClock clock;
    {
        auto store = open_store(dir, clock);
        clock.now = 2000;
        const auto club = store.create_profile("Club", Tier::r1800_1999);
        REQUIRE(club.has_value());
        CHECK(*club ==
              Profile{.id = 2, .name = "Club", .tier = Tier::r1800_1999, .created_at = 2000});
        CHECK(store.rename_profile(2, "Club night").value().name == "Club night");
        CHECK(store.set_tier(2, Tier::r2200_up).value().tier == Tier::r2200_up);

        CHECK(store.create_profile("", Tier::under_1200).error() == ProfileError::invalid_name);
        CHECK(store.create_profile("Default", Tier::under_1200).error() ==
              ProfileError::duplicate_name);
        CHECK(store.rename_profile(2, "").error() == ProfileError::invalid_name);
        CHECK(store.rename_profile(2, "Default").error() == ProfileError::duplicate_name);
        CHECK(store.rename_profile(9, "Nine").error() == ProfileError::no_such_profile);
        CHECK(store.set_tier(9, Tier::under_1200).error() == ProfileError::no_such_profile);
        CHECK(store.remove_profile(9).error() == ProfileError::no_such_profile);

        REQUIRE(store.remove_profile(1).has_value());
        CHECK(store.remove_profile(2).error() == ProfileError::last_profile);
    }
    // Everything persists.
    TestClock later{.now = 5000};
    const auto reopened = open_store(dir, later);
    CHECK(reopened.profiles() ==
          std::vector{
              Profile{.id = 2, .name = "Club night", .tier = Tier::r2200_up, .created_at = 2000}});
}

TEST_CASE("A file that is not a profiles.db is refused and left as it was", "[profiles]") {
    SECTION("not SQLite") {
        const DataDir dir("not-sqlite");
        write_bytes(dir.file(), "some notes the owner keeps here, not a database at all\n");
        const auto before = read_bytes(dir.file());
        CHECK(open_error(dir) == ProfileError::not_profiles);
        CHECK(read_bytes(dir.file()) == before);
    }
    SECTION("SQLite without meta") {
        const DataDir dir("no-meta");
        std::filesystem::create_directories(dir.path());
        {
            SQLite::Database db(dir.file(), create_flags());
            db.exec("CREATE TABLE notes (text TEXT); INSERT INTO notes VALUES ('mine');");
        }
        const auto before = read_bytes(dir.file());
        CHECK(open_error(dir) == ProfileError::not_profiles);
        CHECK(read_bytes(dir.file()) == before);
    }
    SECTION("meta without schema_version") {
        const DataDir dir("no-version");
        std::filesystem::create_directories(dir.path());
        {
            SQLite::Database db(dir.file(), create_flags());
            db.exec("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
                    "INSERT INTO meta VALUES ('owner', 'me');");
        }
        const auto before = read_bytes(dir.file());
        CHECK(open_error(dir) == ProfileError::not_profiles);
        CHECK(read_bytes(dir.file()) == before);
    }
}

TEST_CASE("A newer profiles.db is refused and left as it was", "[profiles]") {
    const DataDir dir("newer");
    {
        TestClock clock;
        const auto store = ProfileStore::open(dir.path(), clock.clock(), four_versions());
        REQUIRE(store.has_value());
        CHECK(store->schema_version() == 4);
    }
    const auto before = read_bytes(dir.file());
    CHECK(open_error(dir) == ProfileError::newer_version);
    CHECK(read_bytes(dir.file()) == before);
}

TEST_CASE("An older file is migrated one version at a time, and a failing script stops it",
          "[profiles]") {
    SECTION("each later script runs, in order") {
        const DataDir dir("migrate");
        TestClock clock;
        static_cast<void>(ProfileStore::open(dir.path(), clock.clock(),
                                             chesscoach::profile_migrations().first(1)));
        clock.now = 7000;
        const auto store = ProfileStore::open(dir.path(), clock.clock(), three_versions());
        REQUIRE(store.has_value());
        CHECK(store->schema_version() == 3);
        CHECK(store->profiles().size() == 1);
        SQLite::Database db(dir.file());
        CHECK(db.execAndGet("SELECT made_at FROM extra").getInt64() == 7000);
        CHECK(db.execAndGet("SELECT COUNT(*) FROM pragma_table_info('extra') WHERE name = 'note'")
                  .getInt() == 1);
        CHECK(db.execAndGet("SELECT value FROM meta WHERE key = 'schema_version'").getString() ==
              "3");
    }
    SECTION("a failing script leaves the file at the last version that succeeded") {
        const DataDir dir("migrate-fails");
        TestClock clock;
        static_cast<void>(ProfileStore::open(dir.path(), clock.clock(),
                                             chesscoach::profile_migrations().first(1)));
        CHECK(open_error(dir, failing_third()) == ProfileError::migration_failed);
        SQLite::Database db(dir.file());
        CHECK(db.execAndGet("SELECT value FROM meta WHERE key = 'schema_version'").getString() ==
              "2");
        CHECK(db.tableExists("extra"));
        CHECK_FALSE(db.tableExists("half"));
    }
}

TEST_CASE("The engine cache hits at the minimum depth and keeps the deeper search", "[profiles]") {
    const DataDir dir("cache");
    TestClock clock;
    auto store = open_store(dir, clock);
    CHECK_FALSE(store.cache_lookup(start_key, 3, 1).has_value());

    clock.now = 3000;
    const auto depth_10 = result_of({
        cp_line(1, 10, 30, {"e2e4", "e7e5"}),
        cp_line(2, 11, 25, {"d2d4", "d7d5"}),
        cp_line(3, 10, 20, {"g1f3"}),
    });
    const CachedSearch stored_10{
        .lines = depth_10.lines,
        .engine = "Stockfish 19",
        .created_at = 3000,
    };
    CHECK(store.cache_store(start_key, 3, depth_10) == stored_10);
    CHECK(store.cache_lookup(start_key, 3, 10) == stored_10);
    CHECK(store.cache_lookup(start_key, 3, 1) == stored_10);
    CHECK_FALSE(store.cache_lookup(start_key, 3, 11).has_value());
    CHECK_FALSE(store.cache_lookup(start_key, 1, 1).has_value());
    CHECK_FALSE(store.cache_lookup(start_key + 1, 3, 1).has_value());

    // A shallower search is not stored; the stored lines come back.
    clock.now = 3100;
    const auto depth_8 = result_of(
        {cp_line(1, 8, 40, {"c2c4"}), cp_line(2, 12, 10, {"e2e4"}), cp_line(3, 12, 5, {"d2d4"})});
    CHECK(store.cache_store(start_key, 3, depth_8) == stored_10);
    CHECK(store.cache_lookup(start_key, 3, 1) == stored_10);

    // An equal least depth replaces; so does a deeper one; and a complete result may have fewer
    // lines than asked for.
    clock.now = 3200;
    const auto equal = result_of(
        {cp_line(1, 10, 31, {"e2e4"}), cp_line(2, 10, 26, {"d2d4"}), cp_line(3, 10, 21, {"c2c4"})});
    CHECK(store.cache_store(start_key, 3, equal) == cached(equal.lines, 3200));
    CHECK(store.cache_lookup(start_key, 3, 10) == cached(equal.lines, 3200));
    clock.now = 3300;
    const auto single = result_of({cp_line(1, 20, 33, {"e2e4", "c7c5"})});
    CHECK(store.cache_store(start_key, 3, single).value().lines == single.lines);
    CHECK(store.cache_lookup(start_key, 3, 20) ==
          CachedSearch{.lines = single.lines, .engine = "Stockfish 19", .created_at = 3300});

    // Each line count is its own entry.
    const auto one_line = result_of({cp_line(1, 5, 15, {"e2e4"})});
    REQUIRE(store.cache_store(start_key, 1, one_line).has_value());
    CHECK(store.cache_lookup(start_key, 1, 5) == cached(one_line.lines, 3300));
    CHECK(store.cache_lookup(start_key, 3, 20) == cached(single.lines, 3300));
}

TEST_CASE("Mate scores and empty pvs round-trip through the cache", "[profiles]") {
    const DataDir dir("cache-mate");
    TestClock clock;
    auto store = open_store(dir, clock);
    const auto result = result_of({
        SearchLine{
            .multipv = 1,
            .depth = 30,
            .score_cp = {},
            .score_mate = -3,
            .pv = {"h7h8", "g8h8", "a1a8"},
        },
        SearchLine{.multipv = 2, .depth = 30, .score_cp = {}, .score_mate = 0, .pv = {}},
    });
    REQUIRE(store.cache_store(42, 2, result).has_value());
    CHECK(store.cache_lookup(42, 2, 30) == cached(result.lines, 1000));
}

TEST_CASE("Limited, stopped and empty results are not cached", "[profiles]") {
    const DataDir dir("cache-skip");
    TestClock clock;
    auto store = open_store(dir, clock);
    auto limited = result_of({cp_line(1, 30, 50, {"e2e4"})});
    limited.limited = true;
    limited.elo = 1320;
    CHECK(store.cache_store(start_key, 1, limited).value().lines == limited.lines);
    auto stopped = result_of({cp_line(1, 30, 50, {"e2e4"})});
    stopped.stopped = true;
    CHECK(store.cache_store(start_key, 1, stopped).value().lines == stopped.lines);
    CHECK(store.cache_store(start_key, 1, result_of({})).value().lines.empty());
    CHECK_FALSE(store.cache_lookup(start_key, 1, 0).has_value());
}

TEST_CASE("The committed version 1 file migrates to version 3 with its rows kept", "[profiles]") {
    const DataDir dir("fixture");
    std::filesystem::create_directories(dir.path());
    std::filesystem::copy_file(std::filesystem::path(CHESSCOACH_FIXTURE_DIR) / "profiles-v1.db",
                               dir.file());
    TestClock clock{.now = 9999};
    auto store = open_store(dir, clock);
    CHECK(store.schema_version() == 3);
    CHECK(store.profiles() ==
          std::vector{
              Profile{.id = 1, .name = "Default", .tier = Tier::r1400_1599, .created_at = 1000},
              Profile{.id = 2, .name = "Club", .tier = Tier::r1800_1999, .created_at = 2000}});
    CHECK(store.cache_lookup(start_key, 3, 20) == cached(fixture_search().lines, 3000));
    CHECK(store.spar_sessions(1).empty());
    CHECK(store.spar_sessions(2).empty());
    CHECK(store.repertoire_nodes(1, Color::white).empty());
    CHECK(store.cards(1, Color::white).empty());
    CHECK(store.set_choice(2, Color::black, black_after_e4()) == ChoiceChange::added);
}

TEST_CASE("The committed version 2 file migrates to version 3 with its rows kept", "[profiles]") {
    const DataDir dir("fixture-v2");
    std::filesystem::create_directories(dir.path());
    std::filesystem::copy_file(std::filesystem::path(CHESSCOACH_FIXTURE_DIR) / "profiles-v2.db",
                               dir.file());
    TestClock clock{.now = 9999};
    auto store = open_store(dir, clock);
    CHECK(store.schema_version() == 3);
    CHECK(store.profiles().size() == 2);
    CHECK(store.cache_lookup(start_key, 3, 20) == cached(fixture_search().lines, 3000));
    CHECK(store.spar_sessions(1) == std::vector{fixture_session()});
    CHECK(store.repertoire_nodes(1, Color::white).empty());
    CHECK(store.set_choice(1, Color::white, white_start()) == ChoiceChange::added);
    CHECK(store.cards(1, Color::white) ==
          std::vector{new_card_for(1, 1, Color::white, white_start(), 9999)});
}

TEST_CASE("The committed version 3 file opens unchanged", "[profiles]") {
    const DataDir dir("fixture-v3");
    std::filesystem::create_directories(dir.path());
    std::filesystem::copy_file(std::filesystem::path(CHESSCOACH_FIXTURE_DIR) / "profiles-v3.db",
                               dir.file());
    const auto before = read_bytes(dir.file());
    {
        TestClock clock{.now = 9999};
        const auto store = open_store(dir, clock);
        CHECK(store.schema_version() == 3);
        CHECK(store.profiles().size() == 2);
        CHECK(store.cache_lookup(start_key, 3, 20) == cached(fixture_search().lines, 3000));
        CHECK(store.spar_sessions(1) == std::vector{fixture_session()});
        CHECK(store.repertoire_nodes(1, Color::white) ==
              by_key({
                  RepertoireNode{.choice = white_start(), .created_at = 5000},
                  RepertoireNode{.choice = white_after_e5(), .created_at = 5000},
              }));
        CHECK(store.repertoire_nodes(2, Color::black) ==
              std::vector{RepertoireNode{.choice = black_after_e4(), .created_at = 5000}});
        CHECK(store.repertoire_nodes(1, Color::black).empty());
        const Card e4 = card_at(store, 1, Color::white, white_start().key);
        CHECK(store.card_history(e4.id) == fixture_reviews());
        CHECK(e4.state == chesscoach::replay(5000, fixture_reviews()));
        const Card nf3 = card_at(store, 1, Color::white, white_after_e5().key);
        CHECK(nf3 == new_card_for(nf3.id, 1, Color::white, white_after_e5(), 5000));
        CHECK(store.card_history(nf3.id).empty());
        CHECK(store.cards(2, Color::black).size() == 1);
    }
    CHECK(read_bytes(dir.file()) == before);
}

TEST_CASE("Spar sessions are saved, read back and removed with their profile", "[profiles]") {
    const DataDir dir("spar");
    TestClock clock;
    auto store = open_store(dir, clock);
    const SparFixture fixture("profiles_spar");
    const OpeningStore openings = open_openings(fixture);

    // An ended session: the mate, its moves, result and both times.
    const SparSession mated = mated_session(openings, 0xFFFFFFFFFFFFFFFFULL);
    const auto id = store.save_spar_session(1, mated, 5000, 5100);
    REQUIRE(id.has_value());
    SparSessionRow expected = fixture_session();
    expected.id = *id;
    expected.seed = 0xFFFFFFFFFFFFFFFFULL; // the bit cast round-trips the top bit
    expected.started_at = 5000;
    expected.ended_at = 5100;
    CHECK(store.spar_session(*id) == expected);

    // A session stopped in book after a Dubious move: its phase, its interruption, no end time.
    auto stopped = SparSession::start(openings, Tier::r1600_1799, Color::white, 3);
    REQUIRE(stopped.has_value());
    REQUIRE(stopped->play_user_move("f2f3").has_value());
    const auto second = store.save_spar_session(1, *stopped, 6000, 6100);
    REQUIRE(second.has_value());
    const auto row = store.spar_session(*second);
    REQUIRE(row.has_value());
    if (row) {
        CHECK(row->phase == Phase::book);
        CHECK(row->colour == Color::white);
        CHECK(row->tier == Tier::r1600_1799);
        CHECK(row->interruptions == 1);
        CHECK(row->moves == std::vector<std::string>{"f2f3"});
        CHECK_FALSE(row->result.has_value());
        CHECK_FALSE(row->ended_at.has_value());
    }
    CHECK(store.spar_sessions(1).size() == 2);
    CHECK_FALSE(store.spar_session(999).has_value());
    CHECK(store.save_spar_session(77, mated, 1, 2).error() == ProfileError::no_such_profile);

    // Removing a profile removes its sessions, and only its.
    const auto club = store.create_profile("Club", Tier::r1800_1999);
    REQUIRE(club.has_value());
    REQUIRE(store.save_spar_session(club->id, mated, 7000, 7100).has_value());
    CHECK(store.spar_sessions(club->id).size() == 1);
    REQUIRE(store.remove_profile(club->id).has_value());
    CHECK(store.spar_sessions(club->id).empty());
    CHECK(store.spar_sessions(1).size() == 2);
}

TEST_CASE("A saved session replays with its own end move and refuses another database",
          "[profiles]") {
    const DataDir dir("spar-replay");
    TestClock clock;
    auto store = open_store(dir, clock);
    const SparFixture fixture("profiles_spar_replay", "3");
    const OpeningStore openings = open_openings(fixture);
    // A session played to move 2 at min_games 12: 1. Nc3 Nf6 (Nf6's 12 games), stopped there.
    auto session =
        SparSession::start(openings, Tier::r1600_1799, Color::white, 11,
                           chesscoach::SparConfig{.end_move = 2, .sampler = {.min_games = 12}});
    REQUIRE(session.has_value());
    REQUIRE(session->play_user_move("b1c3").has_value());
    Scripted unused({});
    REQUIRE(session->coach_reply(unused) == "g8f6");
    REQUIRE(session->phase() == Phase::ended);
    const auto id = store.save_spar_session(1, *session, 100, 200);
    REQUIRE(id.has_value());
    const auto row = store.spar_session(*id);
    REQUIRE(row.has_value());
    if (row) {
        CHECK(row->end_move == 2);
        CHECK(row->min_games == 12);
        CHECK(row->data_version == 3);
        const auto replayed = chesscoach::replay_spar_session(openings, *row);
        REQUIRE(replayed.has_value());
        CHECK(*replayed == session->summary());
        // Another build of the database: refused, its book replies may differ.
        const SparFixture other("profiles_spar_replay_other", "4");
        const OpeningStore other_openings = open_openings(other);
        CHECK(chesscoach::replay_spar_session(other_openings, *row).error() ==
              chesscoach::SparError::replay_database_mismatch);
    }
}

TEST_CASE("A choice keeps its card in step when it is added, kept, changed and removed",
          "[profiles][repertoire]") {
    const DataDir dir("choices");
    TestClock clock{.now = 5000};
    auto store = open_store(dir, clock);
    const RepertoireChoice e4 = white_start();

    // A new node gets a new card, due at once.
    CHECK(store.set_choice(1, Color::white, e4) == ChoiceChange::added);
    CHECK(store.repertoire_nodes(1, Color::white) ==
          std::vector{RepertoireNode{.choice = e4, .created_at = 5000}});
    const auto cards = store.cards(1, Color::white);
    REQUIRE(cards.size() == 1);
    const std::int64_t id = cards.front().id;
    CHECK(cards == std::vector{new_card_for(id, 1, Color::white, e4, 5000)});
    CHECK(store.due_cards(1, Color::white, 5000) == cards);
    CHECK(store.repertoire(1, Color::white) == chesscoach::Repertoire{{e4.key, "e2e4"}});

    // The same move again changes nothing, the card's reviews included.
    clock.now = 6000;
    REQUIRE(store.record_review(1, Color::white, id, e4.key, Grade::good, 6000).has_value());
    CHECK(store.set_choice(1, Color::white, e4) == ChoiceChange::unchanged);
    CHECK(store.repertoire_nodes(1, Color::white) ==
          std::vector{RepertoireNode{.choice = e4, .created_at = 5000}});
    CHECK(store.card_history(id).size() == 1);

    // Another move restarts the node and its card, whose id stays and whose reviews go.
    clock.now = 7000;
    RepertoireChoice d4 = e4;
    d4.move = "d2d4";
    CHECK(store.set_choice(1, Color::white, d4) == ChoiceChange::changed);
    CHECK(store.repertoire_nodes(1, Color::white) ==
          std::vector{RepertoireNode{.choice = d4, .created_at = 7000}});
    CHECK(store.cards(1, Color::white) == std::vector{new_card_for(id, 1, Color::white, d4, 7000)});
    CHECK(store.card_history(id).empty());

    // Removing the choice removes its card and the card's reviews; a second remove finds none.
    REQUIRE(store.record_review(1, Color::white, id, e4.key, Grade::easy, 7100).has_value());
    CHECK(store.remove_choice(1, Color::white, e4.key) == true);
    CHECK(store.repertoire_nodes(1, Color::white).empty());
    CHECK(store.cards(1, Color::white).empty());
    CHECK(store.card_history(id).empty());
    CHECK(store.remove_choice(1, Color::white, e4.key) == false);
    CHECK(store.repertoire(1, Color::white).empty());
}

TEST_CASE("A choice that is not valid is refused, and a valid one's FEN is stored normalized",
          "[profiles][repertoire]") {
    const DataDir dir("invalid-choices");
    TestClock clock;
    auto store = open_store(dir, clock);
    const RepertoireChoice e4 = white_start();
    RepertoireChoice wrong_key = e4;
    wrong_key.key ^= 1U;
    RepertoireChoice bad_fen = e4;
    bad_fen.fen = "not a position";
    RepertoireChoice illegal = e4;
    illegal.move = "e2e5";
    RepertoireChoice san = e4;
    san.move = "e4";
    for (const auto& choice : {wrong_key, bad_fen, illegal, san}) {
        CHECK(store.set_choice(1, Color::white, choice).error() == ProfileError::invalid_choice);
    }
    // White is to move at the start, so it is not a black choice.
    CHECK(store.set_choice(1, Color::black, e4).error() == ProfileError::invalid_choice);
    CHECK(store.set_choice(9, Color::white, e4).error() == ProfileError::no_such_profile);
    CHECK(store.repertoire_nodes(1, Color::white).empty());
    CHECK(store.cards(1, Color::white).empty());

    // The counters and an en passant square no capture uses are not part of the stored FEN.
    const RepertoireChoice spelled{
        .key = black_after_e4().key,
        .fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
        .move = "c7c5",
    };
    CHECK(store.set_choice(1, Color::black, spelled) == ChoiceChange::added);
    CHECK(store.repertoire_nodes(1, Color::black) ==
          std::vector{RepertoireNode{.choice = black_after_e4(), .created_at = 1000}});
}

TEST_CASE("An import adds only the positions the repertoire does not answer, or nothing",
          "[profiles][repertoire]") {
    const DataDir dir("import-choices");
    TestClock clock{.now = 5000};
    auto store = open_store(dir, clock);
    REQUIRE(store.set_choice(1, Color::white, white_start()) == ChoiceChange::added);

    clock.now = 6000;
    RepertoireChoice c4 = white_start();
    c4.move = "c2c4";
    const RepertoireChoice bb5 = choice_after({"e2e4", "e7e5", "g1f3", "b8c6"}, "f1b5");
    const std::vector choices{c4, white_after_e5(), white_start(), bb5};
    CHECK(store.import_choices(1, Color::white, choices) ==
          ChoiceImport{.added = 2, .unchanged = 1, .conflicts = 1});
    CHECK(store.repertoire_nodes(1, Color::white) ==
          by_key({
              RepertoireNode{.choice = white_start(), .created_at = 5000},
              RepertoireNode{.choice = white_after_e5(), .created_at = 6000},
              RepertoireNode{.choice = bb5, .created_at = 6000},
          }));
    const Card nf3 = card_at(store, 1, Color::white, white_after_e5().key);
    CHECK(nf3 == new_card_for(nf3.id, 1, Color::white, white_after_e5(), 6000));
    CHECK(store.cards(1, Color::white).size() == 3);

    // One choice that is not valid refuses the whole import.
    RepertoireChoice bad = choice_after({"d2d4", "d7d5"}, "e1e8");
    const std::vector refused{choice_after({"d2d4", "d7d5"}, "c2c4"), bad};
    CHECK(store.import_choices(1, Color::white, refused).error() == ProfileError::invalid_choice);
    CHECK(store.repertoire_nodes(1, Color::white).size() == 3);
    CHECK(store.cards(1, Color::white).size() == 3);
    CHECK(store.import_choices(9, Color::white, choices).error() == ProfileError::no_such_profile);
}

TEST_CASE("Syncing the cards repairs them to the nodes, and a second sync changes nothing",
          "[profiles][repertoire]") {
    const DataDir dir("sync");
    TestClock clock{.now = 5000};
    auto store = open_store(dir, clock);
    const RepertoireChoice bb5 = choice_after({"e2e4", "e7e5", "g1f3", "b8c6"}, "f1b5");
    for (const auto& choice : {white_start(), white_after_e5(), bb5}) {
        REQUIRE(store.set_choice(1, Color::white, choice) == ChoiceChange::added);
    }
    const Card nf3 = card_at(store, 1, Color::white, white_after_e5().key);
    REQUIRE(store.record_review(1, Color::white, nf3.id, nf3.key, Grade::good, 5500).has_value());
    CHECK(store.sync_cards(1, Color::white) == CardSync{});

    // Broken behind the store's back: e4's card missing, Nf3's card on another move, and a card
    // with a review whose node does not exist.
    {
        SQLite::Database db(dir.file(), SQLite::OPEN_READWRITE);
        db.exec("PRAGMA foreign_keys = ON");
        SQLite::Statement missing(db, "DELETE FROM cards WHERE key = ?1");
        missing.bind(1, std::bit_cast<std::int64_t>(white_start().key));
        REQUIRE(missing.exec() == 1);
        SQLite::Statement stale(db, "UPDATE cards SET move = 'b1c3' WHERE id = ?1");
        stale.bind(1, nf3.id);
        REQUIRE(stale.exec() == 1);
        db.exec("INSERT INTO cards (profile_id, colour, key, move, repetitions, interval_days, "
                "ease_milli, due_at, created_at) VALUES (1, 0, 12345, 'a2a3', 0, 0, 2500, 0, 0)");
        db.exec("INSERT INTO reviews (card_id, grade, reviewed_at) "
                "VALUES (last_insert_rowid(), 4, 10)");
    }

    clock.now = 8000;
    CHECK(store.sync_cards(1, Color::white) == CardSync{.created = 1, .reset = 1, .deleted = 1});
    const Card e4 = card_at(store, 1, Color::white, white_start().key);
    CHECK(e4 == new_card_for(e4.id, 1, Color::white, white_start(), 8000));
    CHECK(card_at(store, 1, Color::white, white_after_e5().key) ==
          new_card_for(nf3.id, 1, Color::white, white_after_e5(), 8000));
    CHECK(store.card_history(nf3.id).empty());
    const Card kept = card_at(store, 1, Color::white, bb5.key);
    CHECK(kept.created_at == 5000);
    CHECK(store.cards(1, Color::white).size() == 3);
    {
        SQLite::Database db(dir.file());
        CHECK(db.execAndGet("SELECT COUNT(*) FROM reviews").getInt() == 0);
    }
    CHECK(store.sync_cards(1, Color::white) == CardSync{});
    CHECK(store.sync_cards(1, Color::black) == CardSync{});
}

TEST_CASE("Due cards come by due time and then key, and a review is the scheduler's",
          "[profiles][repertoire]") {
    const DataDir dir("due");
    TestClock clock{.now = 5000};
    auto store = open_store(dir, clock);
    const RepertoireChoice bb5 = choice_after({"e2e4", "e7e5", "g1f3", "b8c6"}, "f1b5");
    for (const auto& choice : {white_start(), white_after_e5(), bb5}) {
        REQUIRE(store.set_choice(1, Color::white, choice) == ChoiceChange::added);
    }
    CHECK(store.due_cards(1, Color::white, 4999).empty());
    const auto due = store.due_cards(1, Color::white, 5000);
    REQUIRE(due.size() == 3);
    CHECK(std::ranges::is_sorted(due, {}, &Card::key));
    CHECK(due == store.cards(1, Color::white));

    // Good on the first, again on the second a little later: both leave the due list, and
    // come back in the order of their new due times.
    const auto first =
        store.record_review(1, Color::white, due.at(0).id, due.at(0).key, Grade::good, 6000);
    REQUIRE(first.has_value());
    CHECK(first->state == chesscoach::review(chesscoach::new_card(5000), Grade::good, 6000));
    CHECK(card_at(store, 1, Color::white, due.at(0).key) == *first);
    const auto second =
        store.record_review(1, Color::white, due.at(1).id, due.at(1).key, Grade::again, 6100);
    REQUIRE(second.has_value());
    CHECK(second->state == chesscoach::review(chesscoach::new_card(5000), Grade::again, 6100));
    CHECK(store.card_history(due.at(0).id) ==
          std::vector{Review{.grade = Grade::good, .reviewed_at = 6000}});
    CHECK(store.card_history(due.at(1).id) ==
          std::vector{Review{.grade = Grade::again, .reviewed_at = 6100}});

    CHECK(store.due_cards(1, Color::white, 6100) == std::vector{due.at(2)});
    REQUIRE(first->state.due_at < second->state.due_at);
    const auto later = store.due_cards(1, Color::white, second->state.due_at);
    REQUIRE(later.size() == 3);
    CHECK(later.at(0).id == due.at(2).id);
    CHECK(later.at(1).id == due.at(0).id);
    CHECK(later.at(2).id == due.at(1).id);

    // A second review applies to the stored state, which stays the replay of the history.
    const auto again = store.record_review(1, Color::white, due.at(0).id, due.at(0).key,
                                           Grade::hard, first->state.due_at);
    REQUIRE(again.has_value());
    CHECK(again->state == chesscoach::replay(5000, store.card_history(due.at(0).id)));
    CHECK(store.record_review(1, Color::white, 999, white_start().key, Grade::good, 1).error() ==
          ProfileError::no_such_card);
    CHECK(store.card_history(999).empty());
}

TEST_CASE("A card is reviewed through its profile and colour, and goes with its profile",
          "[profiles]") {
    const DataDir dir("remove-repertoire");
    TestClock clock;
    auto store = open_store(dir, clock);
    const auto club = store.create_profile("Club", Tier::r1800_1999);
    REQUIRE(club.has_value());
    REQUIRE(store.set_choice(club->id, Color::white, white_start()) == ChoiceChange::added);
    REQUIRE(store.set_choice(1, Color::white, white_start()) == ChoiceChange::added);
    const Card card = card_at(store, club->id, Color::white, white_start().key);
    REQUIRE(store.record_review(club->id, Color::white, card.id, card.key, Grade::good, 2000)
                .has_value());
    // A card is reviewed only through its own profile and colour.
    CHECK(store.record_review(1, Color::white, card.id, card.key, Grade::good, 2100).error() ==
          ProfileError::no_such_card);
    CHECK(
        store.record_review(club->id, Color::black, card.id, card.key, Grade::good, 2100).error() ==
        ProfileError::no_such_card);
    // Nor under another key: an id reused by a later card does not grade it.
    CHECK(
        store
            .record_review(club->id, Color::white, card.id, white_after_e5().key, Grade::good, 2100)
            .error() == ProfileError::no_such_card);
    CHECK(store.card_history(card.id).size() == 1);

    REQUIRE(store.remove_profile(club->id).has_value());
    CHECK(store.repertoire_nodes(club->id, Color::white).empty());
    CHECK(store.cards(club->id, Color::white).empty());
    CHECK(store.card_history(card.id).empty());
    CHECK(store.repertoire_nodes(1, Color::white).size() == 1);
    CHECK(store.cards(1, Color::white).size() == 1);
}

TEST_CASE("An export is a copy, and importing it replaces profiles.db and keeps the previous",
          "[profiles][export]") {
    const DataDir from("export-from");
    const DataDir to("export-to");
    const auto copy = from.path() / "export.db";
    TestClock clock{.now = 5000};
    std::int64_t card_id = 0;
    {
        // Exported while the store is open, as the app would.
        auto store = open_store(from, clock);
        REQUIRE(store.create_profile("Club", Tier::r1800_1999).has_value());
        REQUIRE(store.set_choice(1, Color::white, white_start()) == ChoiceChange::added);
        card_id = card_at(store, 1, Color::white, white_start().key).id;
        REQUIRE(store.record_review(1, Color::white, card_id, white_start().key, Grade::good, 6000)
                    .has_value());
        REQUIRE(ProfileStore::export_profiles(from.path(), copy).has_value());
        // Never over an existing file.
        CHECK(ProfileStore::export_profiles(from.path(), copy).error() == ProfileError::unwritable);
        CHECK(ProfileStore::export_profiles(from.path(), from.path() / "no" / "such" / "dir.db")
                  .error() == ProfileError::unwritable);
    }
    {
        auto store = open_store(to, clock);
        REQUIRE(store.create_profile("Mine", Tier::under_1200).has_value());
    }
    const auto replaced = read_bytes(to.file());
    REQUIRE(ProfileStore::import_profiles(to.path(), copy, clock.clock()).has_value());
    CHECK(read_bytes(to.path() / "profiles.db.previous") == replaced);
    CHECK_FALSE(std::filesystem::exists(to.path() / "profiles.db.importing"));

    const auto imported = open_store(to, clock);
    const auto original = open_store(from, clock);
    CHECK(imported.profiles() == original.profiles());
    CHECK(imported.repertoire_nodes(1, Color::white) == original.repertoire_nodes(1, Color::white));
    CHECK(imported.cards(1, Color::white) == original.cards(1, Color::white));
    CHECK(imported.card_history(card_id) == original.card_history(card_id));
    CHECK(imported.card_history(card_id).size() == 1);
}

TEST_CASE("A data directory without profiles.db has nothing to export, and nothing is created",
          "[profiles][export]") {
    const DataDir dir("export-nothing");
    const DataDir out("export-nothing-out");
    std::filesystem::create_directories(out.path());
    const auto copy = out.path() / "export.db";
    CHECK(ProfileStore::export_profiles(dir.path(), copy).error() == ProfileError::not_profiles);
    CHECK_FALSE(std::filesystem::exists(dir.path()));
    CHECK_FALSE(std::filesystem::exists(copy));
    // An empty file is not created into a profiles.db either.
    write_bytes(dir.file(), "");
    CHECK(ProfileStore::export_profiles(dir.path(), copy).error() == ProfileError::not_profiles);
    CHECK(std::filesystem::file_size(dir.file()) == 0);
    CHECK_FALSE(std::filesystem::exists(copy));
}

TEST_CASE("An older file is migrated as it is imported, and the source is left as it was",
          "[profiles][export]") {
    const DataDir dir("import-v1");
    const auto source = std::filesystem::path(CHESSCOACH_FIXTURE_DIR) / "profiles-v1.db";
    const auto before = read_bytes(source);
    TestClock clock{.now = 9999};
    REQUIRE(ProfileStore::import_profiles(dir.path(), source, clock.clock()).has_value());
    CHECK(read_bytes(source) == before);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "profiles.db.previous"));
    const auto store = open_store(dir, clock);
    CHECK(store.schema_version() == 3);
    CHECK(store.profiles().size() == 2);
    CHECK(store.cache_lookup(start_key, 3, 20) == cached(fixture_search().lines, 3000));
}

TEST_CASE("An import that cannot be used is refused and changes nothing", "[profiles][export]") {
    const DataDir dir("import-refused");
    const DataDir sources("import-sources");
    std::filesystem::create_directories(sources.path());
    TestClock clock;
    {
        static_cast<void>(open_store(dir, clock));
    }
    const auto before = read_bytes(dir.file());
    const auto source = sources.path() / "source.db";
    ProfileError expected = ProfileError::not_profiles;

    SECTION("missing") {}
    SECTION("not SQLite") { write_bytes(source, "a shopping list\n"); }
    SECTION("SQLite without meta") {
        SQLite::Database db(source, create_flags());
        db.exec("CREATE TABLE notes (text TEXT)");
    }
    SECTION("a version that is not decimal") {
        SQLite::Database db(source, create_flags());
        db.exec("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
                "INSERT INTO meta VALUES ('schema_version', 'three')");
    }
    SECTION("newer") {
        SQLite::Database db(source, create_flags());
        db.exec("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
                "INSERT INTO meta VALUES ('schema_version', '4')");
        expected = ProfileError::newer_version;
    }
    SECTION("a migration that fails") {
        std::filesystem::copy_file(std::filesystem::path(CHESSCOACH_FIXTURE_DIR) / "profiles-v2.db",
                                   source);
        SQLite::Database db(source, SQLite::OPEN_READWRITE);
        db.exec("CREATE TABLE repertoire_nodes (x INTEGER)");
        expected = ProfileError::migration_failed;
    }
    CHECK(ProfileStore::import_profiles(dir.path(), source, clock.clock()).error() == expected);
    CHECK(read_bytes(dir.file()) == before);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "profiles.db.importing"));
    CHECK_FALSE(std::filesystem::exists(dir.path() / "profiles.db.previous"));
}

TEST_CASE("An import rolls back a journal left beside the current file before replacing it",
          "[profiles][export]") {
    const DataDir crashed("import-hot-journal");
    const DataDir writer_dir("import-hot-journal-writer");
    const DataDir from("import-hot-journal-source");
    TestClock clock{.now = 5000};
    const auto source = from.path() / "export.db";
    {
        auto store = open_store(from, clock);
        REQUIRE(store.create_profile("Mine", Tier::under_1200).has_value());
    }
    REQUIRE(ProfileStore::export_profiles(from.path(), source).has_value());
    {
        auto store = open_store(writer_dir, clock);
        REQUIRE(store.create_profile("Club", Tier::r1800_1999).has_value());
    }
    auto crashed_journal = crashed.file();
    crashed_journal += "-journal";
    copy_mid_transaction(writer_dir.file(), crashed.file());

    REQUIRE(ProfileStore::import_profiles(crashed.path(), source, clock.clock()).has_value());
    CHECK_FALSE(std::filesystem::exists(crashed_journal));
    {
        SQLite::Database imported(crashed.file(), SQLite::OPEN_READONLY);
        CHECK(imported.execAndGet("PRAGMA integrity_check").getString() == "ok");
    }
    {
        const auto store = open_store(crashed, clock);
        const auto profiles = store.profiles();
        REQUIRE(profiles.size() == 2);
        CHECK(profiles.at(0).name == "Default");
        CHECK(profiles.at(1).name == "Mine");
    }
    // The previous file is the current one rolled back: the transaction never happened.
    SQLite::Database previous(crashed.path() / "profiles.db.previous", SQLite::OPEN_READONLY);
    CHECK(previous.execAndGet("PRAGMA integrity_check").getString() == "ok");
    CHECK(previous
              .execAndGet("SELECT group_concat(name, ',') FROM "
                          "(SELECT name FROM profiles ORDER BY id)")
              .getString() == "Default,Club");
    CHECK(previous.execAndGet("SELECT COUNT(*) FROM engine_cache").getInt() == 0);
}

TEST_CASE("A file locked by another connection makes an export or an import busy, changing nothing",
          "[profiles][export]") {
    const DataDir dir("busy-data");
    const DataDir from("busy-source");
    TestClock clock;
    {
        static_cast<void>(open_store(dir, clock));
    }
    {
        static_cast<void>(open_store(from, clock));
    }
    const auto source = from.path() / "export.db";
    REQUIRE(ProfileStore::export_profiles(from.path(), source).has_value());
    const auto before = read_bytes(dir.file());
    const auto exclusive = [](const std::filesystem::path& file) {
        auto holder = std::make_unique<SQLite::Database>(file, SQLite::OPEN_READWRITE);
        holder->exec("BEGIN EXCLUSIVE");
        return holder;
    };
    SECTION("an export of a locked profiles.db") {
        const auto holder = exclusive(dir.file());
        const auto copy = from.path() / "copy.db";
        CHECK(ProfileStore::export_profiles(dir.path(), copy).error() == ProfileError::busy);
        CHECK_FALSE(std::filesystem::exists(copy));
    }
    SECTION("an import of a locked source") {
        const auto holder = exclusive(source);
        CHECK(ProfileStore::import_profiles(dir.path(), source, clock.clock()).error() ==
              ProfileError::busy);
    }
    SECTION("an import over a locked profiles.db") {
        const auto holder = exclusive(dir.file());
        CHECK(ProfileStore::import_profiles(dir.path(), source, clock.clock()).error() ==
              ProfileError::busy);
    }
    CHECK(read_bytes(dir.file()) == before);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "profiles.db.importing"));
    CHECK_FALSE(std::filesystem::exists(dir.path() / "profiles.db.previous"));
}

TEST_CASE("An import never takes the data directory's staging file as its source",
          "[profiles][export]") {
    const DataDir dir("import-staging");
    std::filesystem::create_directories(dir.path());
    const auto staging = dir.path() / "profiles.db.importing";
    std::filesystem::copy_file(std::filesystem::path(CHESSCOACH_FIXTURE_DIR) / "profiles-v3.db",
                               staging);
    const auto before = read_bytes(staging);
    TestClock clock;
    CHECK(ProfileStore::import_profiles(dir.path(), staging, clock.clock()).error() ==
          ProfileError::unwritable);
    CHECK(read_bytes(staging) == before);
    CHECK_FALSE(std::filesystem::exists(dir.file()));
}

TEST_CASE("A stray journal or WAL beside the replaced file is removed, unless another writes",
          "[profiles][export]") {
    const DataDir dir("import-stray");
    const DataDir from("import-stray-source");
    TestClock clock;
    {
        static_cast<void>(open_store(dir, clock));
    }
    {
        static_cast<void>(open_store(from, clock));
    }
    const auto source = from.path() / "export.db";
    REQUIRE(ProfileStore::export_profiles(from.path(), source).has_value());
    auto journal = dir.file();
    journal += "-journal";
    auto wal = dir.file();
    wal += "-wal";
    write_bytes(journal, "");
    write_bytes(wal, "");
    SECTION("no other connection: removed, and the import goes on") {
        REQUIRE(ProfileStore::import_profiles(dir.path(), source, clock.clock()).has_value());
        CHECK_FALSE(std::filesystem::exists(journal));
        CHECK_FALSE(std::filesystem::exists(wal));
        CHECK(std::filesystem::exists(dir.path() / "profiles.db.previous"));
    }
    SECTION("a journal whose header PERSIST mode zeroed: removed too") {
        write_bytes(journal, std::string(512, '\0'));
        REQUIRE(ProfileStore::import_profiles(dir.path(), source, clock.clock()).has_value());
        CHECK_FALSE(std::filesystem::exists(journal));
        CHECK(std::filesystem::exists(dir.path() / "profiles.db.previous"));
    }
    SECTION("another connection writing: kept, and the import is busy") {
        const auto before = read_bytes(dir.file());
        {
            SQLite::Database writer(dir.file(), SQLite::OPEN_READWRITE);
            writer.exec("BEGIN IMMEDIATE");
            CHECK(ProfileStore::import_profiles(dir.path(), source, clock.clock()).error() ==
                  ProfileError::busy);
            CHECK(std::filesystem::exists(journal));
            writer.exec("ROLLBACK");
        }
        CHECK(read_bytes(dir.file()) == before);
        CHECK_FALSE(std::filesystem::exists(dir.path() / "profiles.db.previous"));
    }
}

TEST_CASE("An import that fails at its swap leaves profiles.db as it was", "[profiles][export]") {
    const DataDir dir("import-swap-fails");
    const DataDir from("import-swap-fails-source");
    TestClock clock;
    {
        static_cast<void>(open_store(dir, clock));
    }
    {
        auto store = open_store(from, clock);
        REQUIRE(store.create_profile("Mine", Tier::under_1200).has_value());
    }
    const auto source = from.path() / "export.db";
    REQUIRE(ProfileStore::export_profiles(from.path(), source).has_value());
    // A directory that is not empty where profiles.db.previous goes: it cannot be removed to keep
    // the current file there, so the swap fails before the rename that would replace profiles.db
    // (replace_keeping_previous's own tests fail that rename itself).
    write_bytes(dir.path() / "profiles.db.previous" / "kept.txt", "mine");
    const auto before = read_bytes(dir.file());
    CHECK(ProfileStore::import_profiles(dir.path(), source, clock.clock()).error() ==
          ProfileError::unwritable);
    CHECK(read_bytes(dir.file()) == before);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "profiles.db.importing"));
    CHECK(std::filesystem::is_directory(dir.path() / "profiles.db.previous"));
    CHECK(open_store(dir, clock).profiles().size() == 1);
}

TEST_CASE("A journal left beside a profiles.db that is gone is not played onto the import",
          "[profiles][export]") {
    const DataDir dir("import-orphan-journal");
    const DataDir writer_dir("import-orphan-journal-writer");
    const DataDir from("import-orphan-journal-source");
    TestClock clock{.now = 5000};
    {
        auto store = open_store(from, clock);
        REQUIRE(store.create_profile("Mine", Tier::under_1200).has_value());
    }
    const auto source = from.path() / "export.db";
    REQUIRE(ProfileStore::export_profiles(from.path(), source).has_value());
    {
        static_cast<void>(open_store(writer_dir, clock));
    }
    // A writer stopped mid-transaction, then its profiles.db deleted: the hot journal stays.
    copy_mid_transaction(writer_dir.file(), dir.file());
    std::filesystem::remove(dir.file());
    auto journal = dir.file();
    journal += "-journal";
    auto wal = dir.file();
    wal += "-wal";
    auto shm = dir.file();
    shm += "-shm";
    REQUIRE(std::filesystem::exists(journal));
    write_bytes(wal, "not mine");
    write_bytes(shm, "not mine");

    REQUIRE(ProfileStore::import_profiles(dir.path(), source, clock.clock()).has_value());
    CHECK_FALSE(std::filesystem::exists(journal));
    CHECK_FALSE(std::filesystem::exists(wal));
    CHECK_FALSE(std::filesystem::exists(shm));
    {
        SQLite::Database imported(dir.file(), SQLite::OPEN_READONLY);
        CHECK(imported.execAndGet("PRAGMA integrity_check").getString() == "ok");
    }
    const auto store = open_store(dir, clock);
    const auto profiles = store.profiles();
    REQUIRE(profiles.size() == 2);
    CHECK(profiles.at(1).name == "Mine");
}

TEST_CASE("A source in WAL mode is imported through its -wal, in rollback mode",
          "[profiles][export]") {
    const DataDir dir("import-wal-source");
    const DataDir from("import-wal-source-from");
    TestClock clock{.now = 5000};
    {
        static_cast<void>(open_store(dir, clock));
    }
    {
        static_cast<void>(open_store(from, clock));
    }
    const auto source = from.path() / "export.db";
    REQUIRE(ProfileStore::export_profiles(from.path(), source).has_value());
    // A profile committed to the -wal and not yet in the file: the writer stays open, so the WAL
    // is not checkpointed before the import reads it.
    SQLite::Database writer(source, SQLite::OPEN_READWRITE);
    REQUIRE(writer.execAndGet("PRAGMA journal_mode = WAL").getString() == "wal");
    writer.exec("INSERT INTO profiles (name, tier, created_at) VALUES ('Walled', 3, 7)");
    auto source_wal = source;
    source_wal += "-wal";
    REQUIRE(std::filesystem::file_size(source_wal) > 0);

    REQUIRE(ProfileStore::import_profiles(dir.path(), source, clock.clock()).has_value());
    auto wal = dir.file();
    wal += "-wal";
    CHECK_FALSE(std::filesystem::exists(wal));
    {
        SQLite::Database imported(dir.file(), SQLite::OPEN_READONLY);
        CHECK(imported.execAndGet("PRAGMA journal_mode").getString() == "delete");
    }
    const auto store = open_store(dir, clock);
    const auto profiles = store.profiles();
    REQUIRE(profiles.size() == 2);
    CHECK(profiles.at(1).name == "Walled");
}

TEST_CASE("A source with a journal a read-only connection cannot roll back is busy",
          "[profiles][export]") {
    const DataDir dir("import-hot-source");
    const DataDir writer_dir("import-hot-source-writer");
    TestClock clock;
    {
        static_cast<void>(open_store(dir, clock));
    }
    {
        static_cast<void>(open_store(writer_dir, clock));
    }
    const auto source = writer_dir.path() / "stopped" / "profiles.db";
    copy_mid_transaction(writer_dir.file(), source);
    const auto before = read_bytes(dir.file());
    CHECK(ProfileStore::import_profiles(dir.path(), source, clock.clock()).error() ==
          ProfileError::busy);
    CHECK(read_bytes(dir.file()) == before);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "profiles.db.importing"));
    CHECK_FALSE(std::filesystem::exists(dir.path() / "profiles.db.previous"));
}

TEST_CASE("A card stored out of range is reviewed without overflow", "[profiles][repertoire]") {
    const DataDir dir("out-of-range-card");
    TestClock clock{.now = 5000};
    auto store = open_store(dir, clock);
    REQUIRE(store.set_choice(1, Color::white, white_start()) == ChoiceChange::added);
    const Card card = card_at(store, 1, Color::white, white_start().key);
    {
        SQLite::Database db(dir.file(), SQLite::OPEN_READWRITE);
        db.exec("UPDATE cards SET ease_milli = 2147483647, repetitions = 2147483647");
    }
    const auto reviewed =
        store.record_review(1, Color::white, card.id, card.key, Grade::easy, 6000);
    REQUIRE(reviewed.has_value());
    CHECK(reviewed->state.ease_milli == 10000);
    CHECK(reviewed->state.repetitions == 2147483647);
}

// Writes the committed fixtures: run by hand with `core_profile_store_test "[.fixture]"` when a
// version's fixture is made; ctest never runs a hidden test. Each writer opens the file with the
// migrations up to its version only, so it writes the same file whatever version this build is.
TEST_CASE("Write the version 1 fixture", "[.fixture]") {
    const DataDir dir("fixture-writer");
    TestClock clock;
    {
        auto opened = ProfileStore::open(dir.path(), clock.clock(),
                                         chesscoach::profile_migrations().first(1));
        REQUIRE(opened.has_value());
        auto& store = *opened;
        clock.now = 2000;
        REQUIRE(store.create_profile("Club", Tier::r1800_1999).has_value());
        clock.now = 3000;
        const auto search = fixture_search();
        REQUIRE(store.cache_store(start_key, 3, search).has_value());
    }
    std::filesystem::copy_file(dir.file(),
                               std::filesystem::path(CHESSCOACH_FIXTURE_DIR) / "profiles-v1.db",
                               std::filesystem::copy_options::overwrite_existing);
}

TEST_CASE("Write the version 2 fixture", "[.fixture]") {
    const DataDir dir("fixture-writer-v2");
    TestClock clock;
    const SparFixture fixture("profiles_fixture_v2");
    const OpeningStore openings = open_openings(fixture);
    {
        auto opened = ProfileStore::open(dir.path(), clock.clock(),
                                         chesscoach::profile_migrations().first(2));
        REQUIRE(opened.has_value());
        auto& store = *opened;
        clock.now = 2000;
        REQUIRE(store.create_profile("Club", Tier::r1800_1999).has_value());
        clock.now = 3000;
        REQUIRE(store.cache_store(start_key, 3, fixture_search()).has_value());
        REQUIRE(store.save_spar_session(1, mated_session(openings, 7), 4000, 4100).has_value());
    }
    std::filesystem::copy_file(dir.file(),
                               std::filesystem::path(CHESSCOACH_FIXTURE_DIR) / "profiles-v2.db",
                               std::filesystem::copy_options::overwrite_existing);
}

TEST_CASE("Write the version 3 fixture", "[.fixture]") {
    const DataDir dir("fixture-writer-v3");
    TestClock clock;
    const SparFixture fixture("profiles_fixture_v3");
    const OpeningStore openings = open_openings(fixture);
    {
        auto opened = ProfileStore::open(dir.path(), clock.clock(),
                                         chesscoach::profile_migrations().first(3));
        REQUIRE(opened.has_value());
        auto& store = *opened;
        clock.now = 2000;
        REQUIRE(store.create_profile("Club", Tier::r1800_1999).has_value());
        clock.now = 3000;
        REQUIRE(store.cache_store(start_key, 3, fixture_search()).has_value());
        REQUIRE(store.save_spar_session(1, mated_session(openings, 7), 4000, 4100).has_value());
        clock.now = 5000;
        REQUIRE(store.set_choice(1, Color::white, white_start()) == ChoiceChange::added);
        REQUIRE(store.set_choice(1, Color::white, white_after_e5()) == ChoiceChange::added);
        REQUIRE(store.set_choice(2, Color::black, black_after_e4()) == ChoiceChange::added);
        const Card e4 = card_at(store, 1, Color::white, white_start().key);
        for (const Review& review : fixture_reviews()) {
            REQUIRE(
                store
                    .record_review(1, Color::white, e4.id, e4.key, review.grade, review.reviewed_at)
                    .has_value());
        }
    }
    std::filesystem::copy_file(dir.file(),
                               std::filesystem::path(CHESSCOACH_FIXTURE_DIR) / "profiles-v3.db",
                               std::filesystem::copy_options::overwrite_existing);
}

// Unit tests for chesscoach/profile_store.hpp (core; unit; docs/database.md §12): creating a
// profiles.db at version 1 with the Default profile, every ProfileError, the migration framework
// over longer test lists (in order, one transaction each, a failing script, a newer file refused
// unmodified), the files refused without being touched, the engine cache's hit, miss and replace
// rules, and the committed version 1 fixture opened unchanged. The clock is injected.
#include <chesscoach/profile_store.hpp>

#include <SQLiteCpp/SQLiteCpp.h>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using chesscoach::CachedSearch;
using chesscoach::Key;
using chesscoach::Migration;
using chesscoach::Profile;
using chesscoach::ProfileError;
using chesscoach::ProfileStore;
using chesscoach::SearchLine;
using chesscoach::SearchResult;
using chesscoach::Tier;

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

// Test migrations after the real version 1: version 2 adds a table and calls chesscoach_now(),
// version 3 alters it.
constexpr Migration version_2{
    .version = 2,
    .sql = "CREATE TABLE extra (made_at INTEGER NOT NULL);\n"
           "INSERT INTO extra VALUES (chesscoach_now());",
};
constexpr Migration version_3{.version = 3, .sql = "ALTER TABLE extra ADD COLUMN note TEXT;"};
constexpr Migration failing_version_3{
    .version = 3,
    .sql = "CREATE TABLE half (x INTEGER); INSERT INTO no_such_table VALUES (1);",
};

std::array<Migration, 3> three_versions() {
    return {chesscoach::profile_migrations().front(), version_2, version_3};
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

} // namespace

TEST_CASE("A new data directory gets profiles.db at version 1 with the Default profile",
          "[profiles]") {
    const DataDir dir("fresh");
    TestClock clock;
    const auto store = open_store(dir, clock);
    CHECK(std::filesystem::is_regular_file(dir.file()));
    CHECK(store.schema_version() == 1);
    CHECK(chesscoach::profile_schema_version == 1);
    CHECK(store.profiles() ==
          std::vector{
              Profile{.id = 1, .name = "Default", .tier = Tier::r1400_1599, .created_at = 1000}});
}

TEST_CASE("A zero-length profiles.db is created at version 1", "[profiles]") {
    const DataDir dir("zero-length");
    write_bytes(dir.file(), "");
    TestClock clock;
    const auto store = open_store(dir, clock);
    CHECK(store.schema_version() == 1);
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
        const auto store = ProfileStore::open(dir.path(), clock.clock(), three_versions());
        REQUIRE(store.has_value());
        CHECK(store->schema_version() == 3);
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
        static_cast<void>(open_store(dir, clock));
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
        static_cast<void>(open_store(dir, clock));
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

TEST_CASE("The committed version 1 file opens unchanged", "[profiles]") {
    const DataDir dir("fixture");
    std::filesystem::create_directories(dir.path());
    std::filesystem::copy_file(std::filesystem::path(CHESSCOACH_FIXTURE_DIR) / "profiles-v1.db",
                               dir.file());
    const auto before = read_bytes(dir.file());
    {
        TestClock clock{.now = 9999};
        const auto store = open_store(dir, clock);
        CHECK(store.schema_version() == 1);
        CHECK(store.profiles() ==
              std::vector{
                  Profile{.id = 1, .name = "Default", .tier = Tier::r1400_1599, .created_at = 1000},
                  Profile{.id = 2, .name = "Club", .tier = Tier::r1800_1999, .created_at = 2000}});
        CHECK(store.cache_lookup(start_key, 3, 20) == cached(fixture_search().lines, 3000));
    }
    CHECK(read_bytes(dir.file()) == before);
}

// Writes the committed fixture: run by hand with `core_profile_store_test "[.fixture]"` when a
// version's fixture is made; ctest never runs a hidden test.
TEST_CASE("Write the version 1 fixture", "[.fixture]") {
    const DataDir dir("fixture-writer");
    TestClock clock;
    {
        auto store = open_store(dir, clock);
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

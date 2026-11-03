// Unit tests for chesscoach/opening_store.hpp and opening_schema.hpp (core; unit): a small
// openings.db built in a scratch directory from openings_schema_sql(), with keys on both sides of
// the sign bit, NULL and non-NULL evaluations, several tiers and moves inserted out of order; the
// lookups, the canonical dump against a golden text written by hand from docs/database.md §11,
// and every file the store refuses.
#include <chesscoach/opening_schema.hpp>
#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>

#include <SQLiteCpp/SQLiteCpp.h>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

using chesscoach::Badge;
using chesscoach::Key;
using chesscoach::MoveRow;
using chesscoach::NodeRow;
using chesscoach::OpeningStore;
using chesscoach::PositionRow;
using chesscoach::StoreError;
using chesscoach::Tier;

namespace {

// The keys of the test database: three real positions and four on the sign bit's edges, so that
// the dump's unsigned order differs from SQL's signed one.
constexpr Key one = 1;
constexpr Key en_passant = 5954240292975517309ULL; // 1. e4 d5 2. e5 f5, f6 kept
constexpr Key max_positive = 9223372036854775807ULL;
constexpr Key min_negative = 9223372036854775808ULL;
constexpr Key start = 10026961946044190907ULL;
constexpr Key after_e4 = 11769534657094819628ULL;
constexpr Key all_ones = 18446744073709551615ULL;

constexpr std::string_view start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
constexpr std::string_view e4_fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -";
constexpr std::string_view ep_fen = "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6";

// A scratch directory of its own for each test, under the build directory (so two lanes or two
// presets never share one), removed when the test ends.
class ScratchDir {
  public:
    explicit ScratchDir(std::string_view name)
        : path_(std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / name),
          database_(path_ / "openings.db") {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    ScratchDir(ScratchDir&&) = delete;
    ScratchDir& operator=(ScratchDir&&) = delete;
    ~ScratchDir() {
        std::error_code ignored;
        std::filesystem::permissions(database_, std::filesystem::perms::owner_all, ignored);
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] std::filesystem::path file(std::string_view name) const { return path_ / name; }

  private:
    std::filesystem::path path_;
    std::filesystem::path database_; // made read-only by a test, so writable again before removal
};

// SQLite's open flags are ints, combined with | as its API documents.
int create_flags() {
    return SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE; // NOLINT(bugprone-signed-bitwise)
}

// "-" for an absent text, as the dump writes it.
std::optional<std::string> text_or_null(std::string_view text) {
    return text == "-" ? std::nullopt : std::optional(std::string(text));
}

PositionRow position_row(Key key, std::string_view fen, std::string_view eco, std::string_view name,
                         std::optional<std::int64_t> cp, std::optional<std::int64_t> mate,
                         std::optional<std::int64_t> depth, std::string_view best_move) {
    return {
        .key = key,
        .fen = std::string(fen),
        .eco = text_or_null(eco),
        .name = text_or_null(name),
        .eval_cp = cp,
        .eval_mate = mate,
        .eval_depth = depth,
        .best_move = text_or_null(best_move),
    };
}

NodeRow node_row(Key key, Tier tier, std::int64_t white, std::int64_t draws, std::int64_t black) {
    return {
        .key = key,
        .tier = tier,
        .games = white + draws + black,
        .white = white,
        .draws = draws,
        .black = black,
    };
}

MoveRow move_row(Key key, Tier tier, std::string_view move, Key child, std::int64_t white,
                 std::int64_t draws, std::int64_t black, std::uint8_t badges) {
    return {
        .key = key,
        .tier = tier,
        .move = std::string(move),
        .child = child,
        .white = white,
        .draws = draws,
        .black = black,
        .badges = chesscoach::Badges{badges},
    };
}

void bind_text(SQLite::Statement& statement, int index, const std::optional<std::string>& text) {
    if (text) {
        statement.bind(index, *text);
    } else {
        statement.bind(index);
    }
}

void bind_integer(SQLite::Statement& statement, int index, std::optional<std::int64_t> value) {
    if (value) {
        statement.bind(index, *value);
    } else {
        statement.bind(index);
    }
}

void insert(const SQLite::Database& db, const PositionRow& row) {
    SQLite::Statement insert(db, "INSERT INTO positions VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    insert.bind(1, chesscoach::key_to_sql(row.key));
    insert.bind(2, row.fen);
    bind_text(insert, 3, row.eco);
    bind_text(insert, 4, row.name);
    bind_integer(insert, 5, row.eval_cp);
    bind_integer(insert, 6, row.eval_mate);
    bind_integer(insert, 7, row.eval_depth);
    bind_text(insert, 8, row.best_move);
    insert.exec();
}

void insert(const SQLite::Database& db, const NodeRow& row) {
    SQLite::Statement insert(db, "INSERT INTO nodes VALUES (?, ?, ?, ?, ?, ?)");
    insert.bind(1, chesscoach::key_to_sql(row.key));
    insert.bind(2, chesscoach::tier_index(row.tier));
    insert.bind(3, row.games);
    insert.bind(4, row.white);
    insert.bind(5, row.draws);
    insert.bind(6, row.black);
    insert.exec();
}

void insert(const SQLite::Database& db, const MoveRow& row) {
    SQLite::Statement insert(db, "INSERT INTO moves VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    insert.bind(1, chesscoach::key_to_sql(row.key));
    insert.bind(2, chesscoach::tier_index(row.tier));
    insert.bind(3, row.move);
    insert.bind(4, chesscoach::key_to_sql(row.child));
    insert.bind(5, row.white);
    insert.bind(6, row.draws);
    insert.bind(7, row.black);
    insert.bind(8, static_cast<int>(row.badges.raw()));
    insert.exec();
}

void insert_meta(const SQLite::Database& db, std::string_view key, std::string_view value) {
    SQLite::Statement insert(db, "INSERT INTO meta VALUES (?, ?)");
    insert.bind(1, std::string(key));
    insert.bind(2, std::string(value));
    insert.exec();
}

PositionRow start_row() {
    return position_row(start, start_fen, "-", "-", 19, std::nullopt, 75, "e2e4");
}

MoveRow start_d4() { return move_row(start, Tier::r1400_1599, "d2d4", min_negative, 10, 2, 10, 2); }

MoveRow start_e4() { return move_row(start, Tier::r1400_1599, "e2e4", after_e4, 30, 5, 25, 3); }

MoveRow start_nf3() { return move_row(start, Tier::r1400_1599, "g1f3", max_positive, 3, 1, 2, 0); }

// The test database: the meta rows a pipeline writes, and the rows above, inserted out of order.
void build(const std::filesystem::path& path) {
    SQLite::Database db(path, create_flags());
    db.exec(chesscoach::openings_schema_sql());
    SQLite::Transaction transaction(db);
    for (const auto& [key, value] : {
             std::pair{"tiers", "1200,1400,1600,1800,2000,2200"},
             std::pair{"schema_version", "1"},
             std::pair{"data_version", "0"},
             std::pair{"months", "2013-01"},
             std::pair{"min_games", "1"},
             std::pair{"max_ply", "30"},
             std::pair{"built_at", "2026-11-03T12:00:00Z"},
             std::pair{"pipeline_version", "0.1.0"},
             std::pair{"eval_db", "none"},
             std::pair{"openings_tsv", "a.tsv,b.tsv,c.tsv,d.tsv,e.tsv"},
             std::pair{"games_admitted", "3"},
             std::pair{"games_skipped", "1"},
             std::pair{"skipped_moves", "1"},
         }) {
        insert_meta(db, key, value);
    }
    const auto none = std::nullopt;
    insert(db, position_row(all_ones, "7k/8/8/8/8/8/8/K7 b - -", "-", "-", -45, none, 20, "h8g8"));
    insert(db, start_row());
    insert(db, position_row(after_e4, e4_fen, "B00", "King's Pawn Game", 30, none, 60, "c7c5"));
    insert(db, position_row(one, "7k/8/8/8/8/8/8/K7 w - -", "-", "-", none, none, none, "-"));
    insert(db,
           position_row(min_negative, "k7/8/8/8/8/8/8/7K w - -", "-", "-", none, -2, 30, "h1g1"));
    insert(db, position_row(en_passant, ep_fen, "B01", "Scandinavian Defense: Test, Variation",
                            none, 3, 40, "e5f6"));
    insert(db,
           position_row(max_positive, "k7/8/8/8/8/8/8/7K b - -", "-", "-", none, none, none, "-"));

    insert(db, node_row(all_ones, Tier::r2200_up, 0, 2, 0));
    insert(db, node_row(start, Tier::r2200_up, 20, 15, 15));
    insert(db, node_row(start, Tier::r1400_1599, 45, 10, 45));
    insert(db, node_row(after_e4, Tier::r1400_1599, 30, 5, 25));
    insert(db, node_row(min_negative, Tier::r1600_1799, 2, 3, 2));
    insert(db, node_row(one, Tier::under_1200, 1, 0, 0));
    insert(db, node_row(en_passant, Tier::r1400_1599, 2, 1, 1));

    insert(db, move_row(after_e4, Tier::r1400_1599, "e7e5", one, 10, 2, 8, 6));
    insert(db, start_nf3());
    insert(db, move_row(start, Tier::r2200_up, "e2e4", after_e4, 20, 10, 10, 3));
    insert(db, start_e4());
    insert(db, move_row(min_negative, Tier::r1600_1799, "a1a2", start, 1, 0, 0, 0));
    insert(db, start_d4());
    insert(db, move_row(after_e4, Tier::r1400_1599, "c7c5", all_ones, 15, 1, 9, 2));
    transaction.commit();
}

// The dump of the test database, written by hand from §11: meta by key bytewise without built_at
// and pipeline_version, then each table by key as unsigned, tier and move.
constexpr std::string_view golden_dump =
    "meta\tdata_version\t0\n"
    "meta\teval_db\tnone\n"
    "meta\tgames_admitted\t3\n"
    "meta\tgames_skipped\t1\n"
    "meta\tmax_ply\t30\n"
    "meta\tmin_games\t1\n"
    "meta\tmonths\t2013-01\n"
    "meta\topenings_tsv\ta.tsv,b.tsv,c.tsv,d.tsv,e.tsv\n"
    "meta\tschema_version\t1\n"
    "meta\tskipped_moves\t1\n"
    "meta\ttiers\t1200,1400,1600,1800,2000,2200\n"
    "position\t1\t7k/8/8/8/8/8/8/K7 w - -\t-\t-\t-\t-\t-\t-\n"
    "position\t5954240292975517309\trnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6\t"
    "B01\tScandinavian Defense: Test, Variation\t-\t3\t40\te5f6\n"
    "position\t9223372036854775807\tk7/8/8/8/8/8/8/7K b - -\t-\t-\t-\t-\t-\t-\n"
    "position\t9223372036854775808\tk7/8/8/8/8/8/8/7K w - -\t-\t-\t-\t-2\t30\th1g1\n"
    "position\t10026961946044190907\trnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -\t"
    "-\t-\t19\t-\t75\te2e4\n"
    "position\t11769534657094819628\trnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -\t"
    "B00\tKing's Pawn Game\t30\t-\t60\tc7c5\n"
    "position\t18446744073709551615\t7k/8/8/8/8/8/8/K7 b - -\t-\t-\t-45\t-\t20\th8g8\n"
    "node\t1\t0\t1\t1\t0\t0\n"
    "node\t5954240292975517309\t2\t4\t2\t1\t1\n"
    "node\t9223372036854775808\t3\t7\t2\t3\t2\n"
    "node\t10026961946044190907\t2\t100\t45\t10\t45\n"
    "node\t10026961946044190907\t6\t50\t20\t15\t15\n"
    "node\t11769534657094819628\t2\t60\t30\t5\t25\n"
    "node\t18446744073709551615\t6\t2\t0\t2\t0\n"
    "move\t9223372036854775808\t3\ta1a2\t10026961946044190907\t1\t0\t0\t0\n"
    "move\t10026961946044190907\t2\td2d4\t9223372036854775808\t10\t2\t10\t2\n"
    "move\t10026961946044190907\t2\te2e4\t11769534657094819628\t30\t5\t25\t3\n"
    "move\t10026961946044190907\t2\tg1f3\t9223372036854775807\t3\t1\t2\t0\n"
    "move\t10026961946044190907\t6\te2e4\t11769534657094819628\t20\t10\t10\t3\n"
    "move\t11769534657094819628\t2\tc7c5\t18446744073709551615\t15\t1\t9\t2\n"
    "move\t11769534657094819628\t2\te7e5\t1\t10\t2\t8\t6\n";

std::string read_bytes(const std::filesystem::path& path) {
    const std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

StoreError open_error(const std::filesystem::path& path) {
    const auto store = OpeningStore::open(path);
    REQUIRE_FALSE(store.has_value());
    return store.error();
}

} // namespace

TEST_CASE("The test database's real keys are Position's", "[store]") {
    auto position = chesscoach::Position::start();
    CHECK(position.key() == start);
    CHECK(position.fen() == start_fen);
    REQUIRE(position.play_san("e4").has_value());
    CHECK(position.key() == after_e4);
    CHECK(position.fen() == e4_fen);
    const auto ep = chesscoach::Position::from_fen(ep_fen);
    REQUIRE(ep.has_value());
    CHECK(ep->key() == en_passant);
}

TEST_CASE("The store reads meta, positions, nodes and moves by key and tier", "[store]") {
    const ScratchDir dir("store-lookups");
    build(dir.file("openings.db"));
    const auto store = OpeningStore::open(dir.file("openings.db"));
    REQUIRE(store.has_value());

    const auto& meta = store->meta();
    CHECK(meta.entries.size() == 13);
    REQUIRE(meta.find("schema_version") != nullptr);
    CHECK(*meta.find("schema_version") == "1");
    REQUIRE(meta.find("built_at") != nullptr);
    CHECK(*meta.find("built_at") == "2026-11-03T12:00:00Z");
    CHECK(meta.find("eval") == nullptr);

    CHECK(store->position(start) == start_row());
    const auto none = std::nullopt;
    CHECK(store->position(after_e4) ==
          position_row(after_e4, e4_fen, "B00", "King's Pawn Game", 30, none, 60, "c7c5"));
    CHECK(store->position(min_negative) ==
          position_row(min_negative, "k7/8/8/8/8/8/8/7K w - -", "-", "-", none, -2, 30, "h1g1"));
    CHECK(store->position(all_ones) ==
          position_row(all_ones, "7k/8/8/8/8/8/8/K7 b - -", "-", "-", -45, none, 20, "h8g8"));
    CHECK(store->position(one) ==
          position_row(one, "7k/8/8/8/8/8/8/K7 w - -", "-", "-", none, none, none, "-"));
    CHECK(store->position(2) == std::nullopt);

    CHECK(store->node(start, Tier::r1400_1599) == node_row(start, Tier::r1400_1599, 45, 10, 45));
    CHECK(store->node(start, Tier::r2200_up) == node_row(start, Tier::r2200_up, 20, 15, 15));
    CHECK(store->node(start, Tier::under_1200) == std::nullopt);
    CHECK(store->node(all_ones, Tier::r2200_up) == node_row(all_ones, Tier::r2200_up, 0, 2, 0));
    CHECK(store->node(2, Tier::r1400_1599) == std::nullopt);

    const auto moves = store->moves(start, Tier::r1400_1599);
    REQUIRE(moves.size() == 3);
    CHECK(moves.at(0) == start_d4());
    CHECK(moves.at(1) == start_e4());
    CHECK(moves.at(2) == start_nf3());
    CHECK(moves.at(1).total() == 60);
    CHECK(moves.at(1).badges.has(Badge::book));
    CHECK(moves.at(1).badges.has(Badge::popular));
    CHECK_FALSE(moves.at(1).badges.has(Badge::dubious));
    CHECK(store->moves(start, Tier::r2200_up).size() == 1);
    CHECK(store->moves(start, Tier::r1600_1799).empty());
    CHECK(store->moves(after_e4, Tier::r1400_1599).at(1).badges.has(Badge::dubious));
    CHECK(store->moves(2, Tier::r1400_1599).empty());

    // A lookup can be repeated, interleaved with the others.
    CHECK(store->position(start) == start_row());
    CHECK(store->moves(start, Tier::r1400_1599) == moves);
}

TEST_CASE("The store counts rows per tier, the start position's games and the positions",
          "[store]") {
    const ScratchDir dir("store-counts");
    build(dir.file("openings.db"));
    const auto store = OpeningStore::open(dir.file("openings.db"));
    REQUIRE(store.has_value());
    CHECK(store->counts() == chesscoach::StoreCounts{
                                 .nodes = {1, 0, 3, 1, 0, 0, 2},
                                 .moves = {0, 0, 5, 1, 0, 0, 1},
                                 .start_games = {0, 0, 100, 0, 0, 0, 50},
                                 .positions = 7,
                                 .named = 2,
                                 .evaluated = 5,
                             });

    // An empty database counts zero everywhere.
    {
        SQLite::Database db(dir.file("empty.db"), create_flags());
        db.exec(chesscoach::openings_schema_sql());
        insert_meta(db, "schema_version", "1");
    }
    const auto empty = OpeningStore::open(dir.file("empty.db"));
    REQUIRE(empty.has_value());
    CHECK(empty->counts() == chesscoach::StoreCounts{});
}

TEST_CASE("The dump is the canonical text of §11", "[store]") {
    const ScratchDir dir("store-dump");
    build(dir.file("openings.db"));
    const auto store = OpeningStore::open(dir.file("openings.db"));
    REQUIRE(store.has_value());
    std::ostringstream dump;
    store->dump(dump);
    CHECK(dump.str() == golden_dump);
    std::ostringstream again;
    store->dump(again);
    CHECK(again.str() == golden_dump);
}

TEST_CASE("The store opens a read-only file and leaves it unchanged", "[store]") {
    const ScratchDir dir("store-read-only");
    const auto path = dir.file("openings.db");
    build(path);
    std::filesystem::permissions(path, std::filesystem::perms::owner_read |
                                           std::filesystem::perms::group_read |
                                           std::filesystem::perms::others_read);
    const auto before = read_bytes(path);
    {
        const auto store = OpeningStore::open(path);
        REQUIRE(store.has_value());
        CHECK(store->node(start, Tier::r1400_1599).has_value());
        std::ostringstream dump;
        store->dump(dump);
        CHECK(dump.str() == golden_dump);
    }
    CHECK(read_bytes(path) == before);
    CHECK_FALSE(std::filesystem::exists(dir.file("openings.db-journal")));
    CHECK_FALSE(std::filesystem::exists(dir.file("openings.db-wal")));
}

TEST_CASE("The store refuses a file it does not understand", "[store]") {
    const ScratchDir dir("store-refusals");

    CHECK(open_error(dir.file("missing.db")) == StoreError::missing_file);
    CHECK(open_error(dir.file("")) == StoreError::missing_file);

    {
        std::ofstream text(dir.file("text.db"), std::ios::binary);
        text << "this is not an SQLite database, only text long enough to have a header\n";
    }
    CHECK(open_error(dir.file("text.db")) == StoreError::unreadable);

    {
        SQLite::Database db(dir.file("empty.db"), create_flags());
        db.exec("CREATE TABLE positions (key INTEGER PRIMARY KEY) WITHOUT ROWID");
    }
    CHECK(open_error(dir.file("empty.db")) == StoreError::no_meta);
    {
        const std::ofstream zero(dir.file("zero.db"), std::ios::binary);
    }
    CHECK(open_error(dir.file("zero.db")) == StoreError::no_meta);

    {
        SQLite::Database db(dir.file("unversioned.db"), create_flags());
        db.exec(chesscoach::openings_schema_sql());
        insert_meta(db, "data_version", "0");
    }
    CHECK(open_error(dir.file("unversioned.db")) == StoreError::no_meta);

    // A meta table without key and value columns is no meta table.
    {
        SQLite::Database db(dir.file("misshapen-meta.db"), create_flags());
        db.exec("CREATE TABLE meta (name TEXT, data TEXT);"
                "INSERT INTO meta VALUES ('schema_version', '1');");
    }
    CHECK(open_error(dir.file("misshapen-meta.db")) == StoreError::no_meta);

    // A meta table whose page is corrupt on disk is unreadable, not missing: the first bytes of
    // its b-tree page, the page type among them, are overwritten.
    {
        std::int64_t root_page = 0;
        std::int64_t page_size = 0;
        {
            SQLite::Database db(dir.file("corrupt-meta.db"), create_flags());
            db.exec(chesscoach::openings_schema_sql());
            insert_meta(db, "schema_version", "1");
            root_page =
                db.execAndGet("SELECT rootpage FROM sqlite_master WHERE name = 'meta'").getInt64();
            page_size = db.execAndGet("PRAGMA page_size").getInt64();
        }
        REQUIRE(root_page > 1);
        std::fstream file(dir.file("corrupt-meta.db"),
                          std::ios::binary | std::ios::in | std::ios::out);
        file.seekp((root_page - 1) * page_size);
        const std::string garbage(16, '\xff');
        file.write(garbage.data(), static_cast<std::streamsize>(garbage.size()));
    }
    CHECK(open_error(dir.file("corrupt-meta.db")) == StoreError::unreadable);

    for (const std::string_view version : {"2", "0", "01", " 1", ""}) {
        CAPTURE(version);
        const auto path = dir.file("versioned.db");
        std::filesystem::remove(path);
        {
            SQLite::Database db(path, create_flags());
            db.exec(chesscoach::openings_schema_sql());
            insert_meta(db, "schema_version", version);
        }
        CHECK(open_error(path) == StoreError::wrong_version);
    }

    {
        SQLite::Database db(dir.file("partial.db"), create_flags());
        db.exec("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
        db.exec("CREATE TABLE positions (key INTEGER PRIMARY KEY, fen TEXT NOT NULL)");
        insert_meta(db, "schema_version", "1");
    }
    CHECK(open_error(dir.file("partial.db")) == StoreError::schema_mismatch);

    CHECK(chesscoach::opening_schema_version == 1);
}

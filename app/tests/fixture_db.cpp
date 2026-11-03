// The app tests' opening database (fixture_db.hpp): the CLI fixture's rows (cli/tests/fixture.cpp,
// which app/ cannot link), and a small tree at tier 4 for the Spar tests. The counts are consistent
// with the counting rule of docs/database.md §6.4 at tier 3; tiers 0 and 6 hold only a few rows.
// Badges are set by hand, as §7.3 gives them for these rows: Book only where the parent has a
// tier-6 node, the move at least 5% of it and both positions evaluated within 50 cp.
#include "fixture_db.hpp"

#include <chesscoach/opening_schema.hpp>
#include <chesscoach/position.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <bit>
#include <cstdint>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace chesscoach::app::test {

namespace {

std::int64_t sql(Key key) { return std::bit_cast<std::int64_t>(key); }

// The position after the moves, which the fixture spells correctly.
Position position_after(std::string_view moves) {
    Position position = Position::start();
    std::size_t begin = 0;
    while (begin < moves.size()) {
        std::size_t end = moves.find(' ', begin);
        if (end == std::string_view::npos) {
            end = moves.size();
        }
        if (end > begin && !position.play_san(moves.substr(begin, end - begin))) {
            throw std::logic_error("fixture move does not play");
        }
        begin = end + 1;
    }
    return position;
}

struct PositionRow {
    std::string_view moves;
    std::optional<std::string_view> eco;
    std::optional<std::string_view> name;
    std::optional<std::int64_t> eval_cp;
    std::optional<std::int64_t> eval_mate;
    std::optional<std::int64_t> eval_depth;
    std::optional<std::string_view> best_move;
};

struct NodeRow {
    std::string_view moves;
    int tier;
    std::int64_t white;
    std::int64_t draws;
    std::int64_t black;
};

struct MoveRow {
    std::string_view moves; // the parent
    int tier;
    std::string_view move;  // UCI
    std::string_view child; // the child's moves from the start
    std::int64_t white;
    std::int64_t draws;
    std::int64_t black;
    int badges;
};

// Rows of the three tables, built by name so that each table below reads as one line per row.
PositionRow position(std::string_view moves, std::optional<std::string_view> eco,
                     std::optional<std::string_view> name, std::optional<std::int64_t> eval_cp,
                     std::optional<std::int64_t> eval_mate, std::optional<std::int64_t> eval_depth,
                     std::optional<std::string_view> best_move) {
    return PositionRow{
        .moves = moves,
        .eco = eco,
        .name = name,
        .eval_cp = eval_cp,
        .eval_mate = eval_mate,
        .eval_depth = eval_depth,
        .best_move = best_move,
    };
}

PositionRow unevaluated(std::string_view moves, std::optional<std::string_view> eco,
                        std::optional<std::string_view> name) {
    return position(moves, eco, name, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
}

NodeRow node(std::string_view moves, int tier, std::int64_t white, std::int64_t draws,
             std::int64_t black) {
    return NodeRow{
        .moves = moves,
        .tier = tier,
        .white = white,
        .draws = draws,
        .black = black,
    };
}

MoveRow move(std::string_view moves, int tier, std::string_view uci, std::string_view child,
             std::int64_t white, std::int64_t draws, std::int64_t black, int badges) {
    return MoveRow{
        .moves = moves,
        .tier = tier,
        .move = uci,
        .child = child,
        .white = white,
        .draws = draws,
        .black = black,
        .badges = badges,
    };
}

constexpr auto none = std::nullopt;

// Every key with a nodes row has a positions row (§6.2).
const std::vector<PositionRow>& positions() {
    static const std::vector<PositionRow> rows{
        position("", none, none, 19, none, 40, "e2e4"),
        position("e4", "B00", "King's Pawn Game", 30, none, 36, "c7c5"),
        position("e4 c5", "B20", "Sicilian Defense", 35, none, 38, "g1f3"),
        position("e4 e5", "C20", "King's Pawn Game", 40, none, 34, "g1f3"),
        position("d4", "A40", "Queen's Pawn Game", 20, none, 35, "d7d5"),
        position("d4 d5", "D00", "Queen's Pawn Game", 18, none, 33, "c2c4"),
        unevaluated("Nf3", "A04", "Zukertort Opening"),
        unevaluated("Nf3 d5", none, none),
        position("d4 d5 Nf3", "D02", "Queen's Pawn Game: Zukertort Variation", 15, none, 32,
                 "g8f6"),
        position("f3 e5 g4", none, none, none, -1, 99, "d8h4"),
        // Spar's tier-4 tree: 1. a3 is Dubious, losing 109 cp from the start position's +19.
        position("a3", none, none, -90, none, 30, "e7e5"),
        unevaluated("a3 e5", none, none),
    };
    return rows;
}

const std::vector<NodeRow>& nodes() {
    static const std::vector<NodeRow> rows{
        node("", 0, 9, 1, 10),          node("", 3, 455, 100, 445),
        node("", 6, 45, 30, 25),        node("e4", 3, 260, 60, 280),
        node("e4", 6, 20, 15, 10),      node("e4 c5", 3, 110, 20, 120),
        node("e4 e5", 3, 140, 30, 130), node("d4", 3, 150, 30, 120),
        node("d4 d5", 3, 100, 20, 80),  node("Nf3", 3, 40, 10, 30),
        node("Nf3 d5", 3, 30, 5, 25),   node("d4 d5 Nf3", 3, 80, 15, 65),
        node("f3 e5 g4", 0, 0, 0, 10),  node("", 4, 44, 17, 38),
        node("e4", 4, 30, 10, 20),      node("d4", 4, 4, 2, 3),
        node("a3", 4, 10, 5, 15),       node("a3 e5", 4, 5, 3, 12),
    };
    return rows;
}

const std::vector<MoveRow>& moves() {
    static const std::vector<MoveRow> rows{
        move("", 0, "e2e4", "e4", 5, 1, 6, 3),
        move("", 0, "f2f3", "f3", 4, 0, 4, 2),
        move("", 3, "e2e4", "e4", 260, 60, 280, 3),
        move("", 3, "d2d4", "d4", 150, 30, 120, 3),
        move("", 3, "g1f3", "Nf3", 40, 10, 30, 0),
        move("", 3, "f2f3", "f3", 5, 0, 15, 0),
        move("", 6, "e2e4", "e4", 20, 15, 10, 3),
        move("", 6, "d2d4", "d4", 25, 15, 15, 3),
        move("e4", 3, "e7e5", "e4 e5", 140, 30, 130, 2),
        move("e4", 3, "c7c5", "e4 c5", 110, 20, 120, 3),
        move("e4", 3, "e7e6", "e4 e6", 10, 10, 30, 0),
        move("e4", 6, "c7c5", "e4 c5", 12, 8, 6, 3),
        move("e4 c5", 3, "g1f3", "e4 c5 Nf3", 70, 10, 70, 2),
        move("e4 c5", 3, "b1c3", "e4 c5 Nc3", 40, 10, 50, 2),
        move("d4", 3, "d7d5", "d4 d5", 100, 20, 80, 2),
        move("d4", 3, "g8f6", "d4 Nf6", 50, 10, 40, 2),
        move("d4 d5", 3, "g1f3", "d4 d5 Nf3", 50, 10, 40, 2),
        move("d4 d5", 3, "c2c4", "d4 d5 c4", 50, 10, 40, 2),
        move("Nf3", 3, "d7d5", "Nf3 d5", 30, 5, 25, 2),
        move("Nf3", 3, "g8f6", "Nf3 Nf6", 10, 5, 5, 2),
        move("Nf3 d5", 3, "d2d4", "Nf3 d5 d4", 30, 5, 25, 2),
        move("f3 e5 g4", 0, "d8h4", "f3 e5 g4 Qh4#", 0, 0, 8, 2),
        // Tier 4, for Spar: Book and Popular 1. e4, Dubious 1. a3, Book 1. d4 (9% of the plays,
        // so not Popular, and below the sampler's 10 games); after 1. a3 black's one candidate is
        // e5; after 1. a3 e5 only 2. Nf3 was played, so anything else leaves the book.
        move("", 4, "e2e4", "e4", 30, 10, 20, 3),
        move("", 4, "a2a3", "a3", 10, 5, 15, 6),
        move("", 4, "d2d4", "d4", 4, 2, 3, 1),
        move("a3", 4, "e7e5", "a3 e5", 5, 3, 12, 2),
        move("a3 e5", 4, "g1f3", "a3 e5 Nf3", 3, 2, 5, 2),
    };
    return rows;
}

// meta as the pipeline writes it (§6.1), at the fixture's knobs.
const std::vector<std::pair<std::string_view, std::string_view>>& meta() {
    static const std::vector<std::pair<std::string_view, std::string_view>> rows{
        {"schema_version", "1"},
        {"data_version", "0"},
        {"months", "2013-01"},
        {"min_games", "10"},
        {"max_ply", "8"},
        {"tiers", "1200,1400,1600,1800,2000,2200"},
        {"built_at", "2026-11-03T12:00:00Z"},
        {"pipeline_version", "0.1.0"},
        {"eval_db", "none"},
        {"openings_tsv", "a.tsv,b.tsv,c.tsv,d.tsv,e.tsv"},
        {"games_admitted", "1220"},
        {"admitted_tier_0", "20"},
        {"admitted_tier_1", "0"},
        {"admitted_tier_2", "0"},
        {"admitted_tier_3", "1000"},
        {"admitted_tier_4", "100"},
        {"admitted_tier_5", "0"},
        {"admitted_tier_6", "100"},
        {"games_skipped", "0"},
        {"skipped_malformed", "0"},
        {"skipped_unrated", "0"},
        {"skipped_speed", "0"},
        {"skipped_variant", "0"},
        {"skipped_rating", "0"},
        {"skipped_result", "0"},
        {"skipped_moves", "0"},
    };
    return rows;
}

template <typename T> void bind_optional(SQLite::Statement& statement, int index, const T& value) {
    if (value) {
        statement.bind(index, *value);
    } else {
        statement.bind(index);
    }
}

} // namespace

Key key_after(std::string_view moves) { return position_after(moves).key(); }

void set_data_version(const std::filesystem::path& openings_db, int data_version) {
    const SQLite::Database db(openings_db.string(), SQLite::OPEN_READWRITE);
    SQLite::Statement update(db, "UPDATE meta SET value = ? WHERE key = 'data_version'");
    update.bind(1, std::to_string(data_version));
    update.exec();
}

void drop_cards(const std::filesystem::path& profiles_db) {
    SQLite::Database db(profiles_db.string(), SQLite::OPEN_READWRITE);
    db.exec("DELETE FROM cards");
}

void write_fixture_db(const std::filesystem::path& path) {
    const int flags =
        SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE; // NOLINT(bugprone-signed-bitwise)
    SQLite::Database db(path.string(), flags);
    db.exec(openings_schema_sql());
    SQLite::Transaction transaction(db);

    SQLite::Statement meta_row(db, "INSERT INTO meta (key, value) VALUES (?, ?)");
    for (const auto& [key, value] : meta()) {
        meta_row.bind(1, std::string(key));
        meta_row.bind(2, std::string(value));
        meta_row.exec();
        meta_row.reset();
    }

    SQLite::Statement position_row(
        db, "INSERT INTO positions (key, fen, eco, name, eval_cp, eval_mate, eval_depth, "
            "best_move) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const PositionRow& row : positions()) {
        const Position position = position_after(row.moves);
        position_row.bind(1, sql(position.key()));
        position_row.bind(2, position.fen());
        bind_optional(position_row, 3,
                      row.eco ? std::optional(std::string(*row.eco)) : std::nullopt);
        bind_optional(position_row, 4,
                      row.name ? std::optional(std::string(*row.name)) : std::nullopt);
        bind_optional(position_row, 5, row.eval_cp);
        bind_optional(position_row, 6, row.eval_mate);
        bind_optional(position_row, 7, row.eval_depth);
        bind_optional(position_row, 8,
                      row.best_move ? std::optional(std::string(*row.best_move)) : std::nullopt);
        position_row.exec();
        position_row.reset();
    }

    SQLite::Statement node_row(db, "INSERT INTO nodes (key, tier, games, white, draws, black) "
                                   "VALUES (?, ?, ?, ?, ?, ?)");
    for (const NodeRow& row : nodes()) {
        node_row.bind(1, sql(key_after(row.moves)));
        node_row.bind(2, row.tier);
        node_row.bind(3, row.white + row.draws + row.black);
        node_row.bind(4, row.white);
        node_row.bind(5, row.draws);
        node_row.bind(6, row.black);
        node_row.exec();
        node_row.reset();
    }

    SQLite::Statement move_row(db, "INSERT INTO moves (key, tier, move, child, white, draws, "
                                   "black, badges) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const MoveRow& row : moves()) {
        move_row.bind(1, sql(key_after(row.moves)));
        move_row.bind(2, row.tier);
        move_row.bind(3, std::string(row.move));
        move_row.bind(4, sql(key_after(row.child)));
        move_row.bind(5, row.white);
        move_row.bind(6, row.draws);
        move_row.bind(7, row.black);
        move_row.bind(8, row.badges);
        move_row.exec();
        move_row.reset();
    }
    transaction.commit();
}

FixtureDb::FixtureDb() {
    std::random_device random;
    directory_ = std::filesystem::temp_directory_path() /
                 ("chess-coach-app-test-" + std::to_string(random()) + std::to_string(random()));
    std::filesystem::create_directories(directory_);
    file_ = directory_ / "openings.db";
    write_fixture_db(file_);
}

FixtureDb::~FixtureDb() {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
}

} // namespace chesscoach::app::test

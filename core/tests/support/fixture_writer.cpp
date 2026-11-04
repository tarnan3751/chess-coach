// The writer the CLI's and the app's test fixtures share (fixture_writer.hpp).
#include "fixture_writer.hpp"

#include <chesscoach/opening_schema.hpp>
#include <chesscoach/position.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <bit>
#include <random>
#include <stdexcept>
#include <system_error>

namespace chesscoach::testing {

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

template <typename T> void bind_optional(SQLite::Statement& statement, int index, const T& value) {
    if (value) {
        statement.bind(index, *value);
    } else {
        statement.bind(index);
    }
}

} // namespace

FixturePosition position(std::string_view moves, std::optional<std::string_view> eco,
                         std::optional<std::string_view> name, std::optional<std::int64_t> eval_cp,
                         std::optional<std::int64_t> eval_mate,
                         std::optional<std::int64_t> eval_depth,
                         std::optional<std::string_view> best_move) {
    return FixturePosition{
        .moves = moves,
        .eco = eco,
        .name = name,
        .eval_cp = eval_cp,
        .eval_mate = eval_mate,
        .eval_depth = eval_depth,
        .best_move = best_move,
    };
}

FixturePosition unevaluated(std::string_view moves, std::optional<std::string_view> eco,
                            std::optional<std::string_view> name) {
    return position(moves, eco, name, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
}

FixtureNode node(std::string_view moves, int tier, std::int64_t white, std::int64_t draws,
                 std::int64_t black) {
    return FixtureNode{
        .moves = moves,
        .tier = tier,
        .white = white,
        .draws = draws,
        .black = black,
    };
}

FixtureMove move(std::string_view moves, int tier, std::string_view uci, std::string_view child,
                 std::int64_t white, std::int64_t draws, std::int64_t black, int badges) {
    return FixtureMove{
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

Key key_after(std::string_view moves) { return position_after(moves).key(); }

void write_fixture_db(const std::filesystem::path& path, const FixtureRows& rows) {
    const int flags =
        SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE; // NOLINT(bugprone-signed-bitwise)
    SQLite::Database db(path.string(), flags);
    db.exec(openings_schema_sql());
    SQLite::Transaction transaction(db);

    SQLite::Statement meta_row(db, "INSERT INTO meta (key, value) VALUES (?, ?)");
    for (const auto& [key, value] : rows.meta) {
        meta_row.bind(1, std::string(key));
        meta_row.bind(2, std::string(value));
        meta_row.exec();
        meta_row.reset();
    }

    SQLite::Statement position_row(
        db, "INSERT INTO positions (key, fen, eco, name, eval_cp, eval_mate, eval_depth, "
            "best_move) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const FixturePosition& row : rows.positions) {
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
    for (const FixtureNode& row : rows.nodes) {
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
    for (const FixtureMove& row : rows.moves) {
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

FixtureDb::FixtureDb(std::string_view prefix, const FixtureRows& rows) {
    std::random_device random;
    directory_ = std::filesystem::temp_directory_path() /
                 (std::string(prefix) + std::to_string(random()) + std::to_string(random()));
    std::filesystem::create_directories(directory_);
    file_ = directory_ / "openings.db";
    write_fixture_db(file_, rows);
}

FixtureDb::~FixtureDb() {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
}

} // namespace chesscoach::testing

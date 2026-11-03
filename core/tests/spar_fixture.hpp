// The spar tests' opening database (core; tests): a small tree at tier 3 (1600-1799) written into
// core's schema (openings_schema_sql) in a scratch directory under the build, with Book, Popular
// and Dubious moves, a move into a position off book at the tier, positions in book without
// moves, and evaluations for the Dubious move's loss. Keys come from Position, so they are the
// real keys of the moves. Tier 0 has no row at all, so a session there starts off book.
#pragma once

#include <chesscoach/key.hpp>
#include <chesscoach/opening_schema.hpp>
#include <chesscoach/position.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace chesscoach::testing {

// The position after the moves (SAN, separated by spaces) from the start position.
inline Position spar_position(std::string_view moves) {
    Position position = Position::start();
    std::size_t begin = 0;
    while (begin < moves.size()) {
        std::size_t end = moves.find(' ', begin);
        if (end == std::string_view::npos) {
            end = moves.size();
        }
        if (end > begin && !position.play_san(moves.substr(begin, end - begin))) {
            throw std::logic_error("spar fixture move does not play");
        }
        begin = end + 1;
    }
    return position;
}

struct SparFixtureMove {
    std::string_view parent; // SAN from the start
    std::string_view uci;
    std::string_view child; // SAN from the start
    std::int64_t white;
    int badges;
};

inline SparFixtureMove row(std::string_view parent, std::string_view uci, std::string_view child,
                           std::int64_t white, int badges) {
    return SparFixtureMove{
        .parent = parent,
        .uci = uci,
        .child = child,
        .white = white,
        .badges = badges,
    };
}

// The fixture database in a fresh scratch directory, removed with the object.
class SparFixture {
  public:
    // data_version is meta's (§6.1), which a saved session records.
    explicit SparFixture(std::string_view name, std::string_view data_version = "0")
        : directory_(std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / name),
          file_(directory_ / "openings.db") {
        std::filesystem::remove_all(directory_);
        std::filesystem::create_directories(directory_);
        write(data_version);
    }
    SparFixture(const SparFixture&) = delete;
    SparFixture& operator=(const SparFixture&) = delete;
    SparFixture(SparFixture&&) = delete;
    SparFixture& operator=(SparFixture&&) = delete;
    ~SparFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& file() const { return file_; }

    // Tier 3's moves: start e4 (Book, Popular), d4 (Popular), f3 (Popular, Dubious: 20 cp to
    // -60), Nf3 (Book, its child off book); after f3, e5 and d5; after f3 e5, g4 (Dubious) and
    // e4 (off book); after f3 d5, e4 (off book); after e4, e5 and c5; and 1. Nc3 (no badge) Nf6
    // 2. Nf3, which reaches a book node with no moves.
    static const std::vector<SparFixtureMove>& moves() {
        static const std::vector<SparFixtureMove> rows{
            row("", "e2e4", "e4", 60, 3),
            row("", "d2d4", "d4", 30, 2),
            row("", "f2f3", "f3", 15, 6),
            row("", "g1f3", "Nf3", 12, 1),
            row("e4", "e7e5", "e4 e5", 40, 3),
            row("e4", "c7c5", "e4 c5", 20, 3),
            row("d4", "d7d5", "d4 d5", 30, 3),
            row("f3", "e7e5", "f3 e5", 20, 3),
            row("f3", "d7d5", "f3 d5", 10, 2),
            row("f3 e5", "g2g4", "f3 e5 g4", 12, 6),
            row("f3 e5", "e2e4", "f3 e5 e4", 8, 2),
            row("f3 d5", "e2e4", "f3 d5 e4", 10, 2),
            row("", "b1c3", "Nc3", 10, 0),
            row("Nc3", "g8f6", "Nc3 Nf6", 12, 2),
            row("Nc3 Nf6", "g1f3", "Nc3 Nf6 Nf3", 11, 2),
        };
        return rows;
    }

  private:
    void write(std::string_view data_version) const {
        // NOLINTNEXTLINE(bugprone-signed-bitwise): SQLiteCpp's flags are ints
        SQLite::Database db(file_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(openings_schema_sql());
        db.exec("INSERT INTO meta (key, value) VALUES ('schema_version', '1')");
        SQLite::Statement meta(db, "INSERT INTO meta (key, value) VALUES ('data_version', ?)");
        meta.bind(1, std::string(data_version));
        meta.exec();
        const std::vector<std::pair<std::string_view, std::optional<std::int64_t>>> nodes{
            {"", 20},
            {"e4", 30},
            {"d4", 15},
            {"f3", -60},
            {"f3 e5", -80},
            {"f3 d5", -40},
            {"e4 e5", 25},
            {"e4 c5", 35},
            {"d4 d5", 20},
            {"f3 e5 g4", std::nullopt},
            {"Nc3", std::nullopt},
            {"Nc3 Nf6", std::nullopt},
            {"Nc3 Nf6 Nf3", std::nullopt},
        };
        SQLite::Statement position(db, "INSERT INTO positions (key, fen, eval_cp, eval_depth, "
                                       "best_move) VALUES (?, ?, ?, ?, ?)");
        SQLite::Statement node(db, "INSERT INTO nodes (key, tier, games, white, draws, black) "
                                   "VALUES (?, 3, 100, 100, 0, 0)");
        for (const auto& [moves, cp] : nodes) {
            const Position at = spar_position(moves);
            position.bind(1, key_to_sql(at.key()));
            position.bind(2, at.fen());
            if (cp) {
                position.bind(3, *cp);
                position.bind(4, std::int64_t{30});
                position.bind(5, at.legal_moves_uci().front());
            } else {
                position.bind(3);
                position.bind(4);
                position.bind(5);
            }
            position.exec();
            position.reset();
            node.bind(1, key_to_sql(at.key()));
            node.exec();
            node.reset();
        }
        SQLite::Statement move(db, "INSERT INTO moves (key, tier, move, child, white, draws, "
                                   "black, badges) VALUES (?, 3, ?, ?, ?, 0, 0, ?)");
        for (const SparFixtureMove& row : moves()) {
            move.bind(1, key_to_sql(spar_position(row.parent).key()));
            move.bind(2, std::string(row.uci));
            move.bind(3, key_to_sql(spar_position(row.child).key()));
            move.bind(4, row.white);
            move.bind(5, row.badges);
            move.exec();
            move.reset();
        }
    }

    std::filesystem::path directory_;
    std::filesystem::path file_;
};

} // namespace chesscoach::testing

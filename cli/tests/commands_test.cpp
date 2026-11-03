// Unit tests of the CLI's commands (cli; unit), each run on the fixture database (fixture.hpp)
// and compared with its whole expected output: dump, stats, explore and name, their error paths,
// --moves in SAN and in UCI, and transpositions; and explore on a corrupted copy, whose failure
// after the header lines writes nothing.
#include <chesscoach/opening_store.hpp>

#include "commands/common.hpp"
#include "commands/dump.hpp"
#include "commands/explore.hpp"
#include "commands/name.hpp"
#include "commands/stats.hpp"
#include "fixture.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <bit>
#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>

using chesscoach::cli::test::FixtureDb;
using chesscoach::cli::test::key_after;

namespace {

// A command's output, or its error message prefixed with "error: ".
std::string run(const std::function<chesscoach::cli::Result<void>(std::ostream&)>& command) {
    std::ostringstream out;
    const auto result = command(out);
    if (!result) {
        return "error: " + result.error().message;
    }
    return out.str();
}

std::string explore(const std::string& db, int tier, std::optional<std::string> moves,
                    std::optional<std::string> fen = std::nullopt, bool score = false) {
    const chesscoach::cli::ExploreOptions options{
        .db = db,
        .tier = tier,
        .fen = std::move(fen),
        .moves = std::move(moves),
        .score = score,
    };
    return run([&](std::ostream& out) { return chesscoach::cli::run_explore(options, out); });
}

std::string explore(const FixtureDb& db, int tier, std::optional<std::string> moves,
                    std::optional<std::string> fen = std::nullopt) {
    return explore(db.path(), tier, std::move(moves), std::move(fen));
}

std::string name(const std::string& db, std::optional<std::string> moves,
                 std::optional<std::string> fen = std::nullopt) {
    const chesscoach::cli::NameOptions options{
        .db = db,
        .fen = std::move(fen),
        .moves = std::move(moves),
    };
    return run([&](std::ostream& out) { return chesscoach::cli::run_name(options, out); });
}

std::string name(const FixtureDb& db, std::optional<std::string> moves,
                 std::optional<std::string> fen = std::nullopt) {
    return name(db.path(), std::move(moves), std::move(fen));
}

constexpr const char* start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
constexpr const char* sicilian_fen = "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -";
constexpr const char* zukertort_fen = "rnbqkbnr/ppp1pppp/8/3p4/3P4/5N2/PPP1PPPP/RNBQKB1R b KQkq -";

} // namespace

TEST_CASE("percent rounds half up", "[cli]") {
    using chesscoach::cli::percent;
    CHECK(percent(0, 0) == 0);
    CHECK(percent(0, 7) == 0);
    CHECK(percent(1, 8) == 13);
    CHECK(percent(3, 8) == 38);
    CHECK(percent(1, 3) == 33);
    CHECK(percent(2, 3) == 67);
    CHECK(percent(1, 200) == 1);
    CHECK(percent(1, 201) == 0);
    CHECK(percent(5, 5) == 100);
}

TEST_CASE("dump writes the store's canonical dump and nothing else", "[cli]") {
    const FixtureDb db;
    auto store = chesscoach::OpeningStore::open(db.path());
    REQUIRE(store.has_value());
    std::ostringstream expected;
    store->dump(expected);
    const chesscoach::cli::DumpOptions options{.db = db.path()};
    CHECK(run([&](std::ostream& out) { return chesscoach::cli::run_dump(options, out); }) ==
          expected.str());
    CHECK(expected.str().starts_with("meta\tadmitted_tier_0\t20\n"));
}

TEST_CASE("stats prints meta, the counts per tier and the named and evaluated positions", "[cli]") {
    const FixtureDb db;
    const chesscoach::cli::StatsOptions options{.db = db.path()};
    CHECK(run([&](std::ostream& out) { return chesscoach::cli::run_stats(options, out); }) ==
          "admitted_tier_0: 20\n"
          "admitted_tier_1: 0\n"
          "admitted_tier_2: 0\n"
          "admitted_tier_3: 1000\n"
          "admitted_tier_4: 0\n"
          "admitted_tier_5: 0\n"
          "admitted_tier_6: 100\n"
          "data_version: 0\n"
          "eval_db: none\n"
          "games_admitted: 1120\n"
          "games_skipped: 0\n"
          "max_ply: 8\n"
          "min_games: 10\n"
          "months: 2013-01\n"
          "openings_tsv: a.tsv,b.tsv,c.tsv,d.tsv,e.tsv\n"
          "schema_version: 1\n"
          "skipped_malformed: 0\n"
          "skipped_moves: 0\n"
          "skipped_rating: 0\n"
          "skipped_result: 0\n"
          "skipped_speed: 0\n"
          "skipped_unrated: 0\n"
          "skipped_variant: 0\n"
          "tiers: 1200,1400,1600,1800,2000,2200\n"
          "tier 0 (<1200): 2 nodes, 3 moves, 20 games at the start\n"
          "tier 1 (1200-1399): 0 nodes, 0 moves, 0 games at the start\n"
          "tier 2 (1400-1599): 0 nodes, 0 moves, 0 games at the start\n"
          "tier 3 (1600-1799): 9 nodes, 16 moves, 1000 games at the start\n"
          "tier 4 (1800-1999): 0 nodes, 0 moves, 0 games at the start\n"
          "tier 5 (2000-2199): 0 nodes, 0 moves, 0 games at the start\n"
          "tier 6 (2200+): 2 nodes, 3 moves, 100 games at the start\n"
          "named positions: 7\n"
          "evaluated positions: 8\n");
}

TEST_CASE("explore prints the start position's moves at a tier", "[cli]") {
    const FixtureDb db;
    CHECK(explore(db, 3, std::nullopt) == fmt::format("fen: {}\n"
                                                      "key: {}\n"
                                                      "eval: cp 19 depth 40 best e4 e2e4\n"
                                                      "e4 e2e4 BP 600 43% 10% 47% 60% yes\n"
                                                      "d4 d2d4 BP 300 50% 10% 40% 30% yes\n"
                                                      "Nf3 g1f3 B 80 50% 13% 38% 8% yes\n"
                                                      "f3 f2f3 - 20 25% 0% 75% 2% no\n",
                                                      start_fen, key_after("")));
    CHECK(explore(db, 6, std::nullopt) == fmt::format("fen: {}\n"
                                                      "key: {}\n"
                                                      "eval: cp 19 depth 40 best e4 e2e4\n"
                                                      "d4 d2d4 BP 55 45% 27% 27% 55% no\n"
                                                      "e4 e2e4 BP 45 44% 33% 22% 45% yes\n",
                                                      start_fen, key_after("")));
}

TEST_CASE("explore --score appends the score's components to each move", "[cli]") {
    const FixtureDb db;
    // At tier 3, white to move, the start position at 19 cp:
    // e4: (520 + 60 + 30) / 1260 = 0.484; e4 at 30 cp gains, loss 0; e4's replies 300, 250, 50:
    //     two reach 80% of 600, penalty 0.020, score 0.464.
    // d4: (300 + 30 + 30) / 660 = 0.545; d4 at 20 cp, loss 0; replies 200, 100: two of 300,
    //     penalty 0.020, score 0.525.
    // Nf3: (80 + 10 + 30) / 220 = 0.545; Nf3 unevaluated, loss -; replies 60, 20: two of 80.
    // f3: (10 + 0 + 30) / 100 = 0.400; f3 off book and unevaluated: no replies, no penalty.
    CHECK(explore(db.path(), 3, std::nullopt, std::nullopt, true) ==
          fmt::format("fen: {}\n"
                      "key: {}\n"
                      "eval: cp 19 depth 40 best e4 e2e4\n"
                      "e4 e2e4 BP 600 43% 10% 47% 60% yes yes 0 0.484 2 0.020 0.464\n"
                      "d4 d2d4 BP 300 50% 10% 40% 30% yes yes 0 0.545 2 0.020 0.525\n"
                      "Nf3 g1f3 B 80 50% 13% 38% 8% yes yes - 0.545 2 0.020 0.525\n"
                      "f3 f2f3 - 20 25% 0% 75% 2% no yes - 0.400 0 0.000 0.400\n",
                      start_fen, key_after("")));
}

TEST_CASE("explore orders equal totals by move text", "[cli]") {
    const FixtureDb db;
    CHECK(explore(db, 3, "d4 d5") ==
          fmt::format("fen: {}\n"
                      "key: {}\n"
                      "name: D00 Queen's Pawn Game\n"
                      "eval: cp 18 depth 33 best c4 c2c4\n"
                      "c4 c2c4 BP 100 50% 10% 40% 50% no\n"
                      "Nf3 g1f3 BP 100 50% 10% 40% 50% yes\n",
                      "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq -",
                      key_after("d4 d5")));
}

TEST_CASE("explore reads --moves in SAN and in UCI alike", "[cli]") {
    const FixtureDb db;
    const std::string expected = fmt::format("fen: {}\n"
                                             "key: {}\n"
                                             "name: B20 Sicilian Defense\n"
                                             "eval: cp 35 depth 38 best Nf3 g1f3\n"
                                             "Nf3 g1f3 BP 150 47% 7% 47% 60% no\n"
                                             "Nc3 b1c3 P 100 40% 10% 50% 40% no\n",
                                             sicilian_fen, key_after("e4 c5"));
    CHECK(explore(db, 3, "e4 c5") == expected);
    CHECK(explore(db, 3, "e2e4 c7c5") == expected);
    CHECK(explore(db, 3, "e2e4 c5") == expected);
    CHECK(explore(db, 3, "  e4\tc5 ") == expected);
    CHECK(explore(db, 3, std::nullopt, sicilian_fen) == expected);
}

TEST_CASE("explore prints the same key for two move orders to one position", "[cli]") {
    const FixtureDb db;
    const std::string expected = fmt::format("fen: {}\n"
                                             "key: {}\n"
                                             "name: D02 Queen's Pawn Game: Zukertort Variation\n"
                                             "eval: cp 15 depth 32 best Nf6 g8f6\n",
                                             zukertort_fen, key_after("d4 d5 Nf3"));
    CHECK(key_after("d4 d5 Nf3") == key_after("Nf3 d5 d4"));
    CHECK(explore(db, 3, "d4 d5 Nf3") == expected);
    CHECK(explore(db, 3, "Nf3 d5 d4") == expected);
}

TEST_CASE("explore shows badges, a mate score and moves that leave the book", "[cli]") {
    const FixtureDb db;
    CHECK(explore(db, 3, "e4") ==
          fmt::format("fen: {}\n"
                      "key: {}\n"
                      "name: B00 King's Pawn Game\n"
                      "eval: cp 30 depth 36 best c5 c7c5\n"
                      "e5 e7e5 P 300 47% 10% 43% 50% yes\n"
                      "c5 c7c5 BP 250 44% 8% 48% 42% yes\n"
                      "e6 e7e6 PD 50 20% 20% 60% 8% no\n",
                      "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -", key_after("e4")));
    CHECK(explore(db, 0, "f3 e5 g4") ==
          fmt::format("fen: {}\n"
                      "key: {}\n"
                      "eval: mate -1 depth 99 best Qh4# d8h4\n"
                      "Qh4# d8h4 P 8 0% 0% 100% 100% no\n",
                      "rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq -",
                      key_after("f3 e5 g4")));
}

TEST_CASE("explore of a position with no moves prints its header only", "[cli]") {
    const FixtureDb db;
    CHECK(explore(db, 3, "e4 e5") ==
          fmt::format("fen: {}\n"
                      "key: {}\n"
                      "name: C20 King's Pawn Game\n"
                      "eval: cp 40 depth 34 best Nf3 g1f3\n",
                      "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -",
                      key_after("e4 e5")));
}

TEST_CASE("explore of an off-book position prints one line after the header", "[cli]") {
    const FixtureDb db;
    CHECK(explore(db, 3, "e4 e6") ==
          fmt::format("fen: {}\n"
                      "key: {}\n"
                      "name: B00 King's Pawn Game\n"
                      "off book at tier 3 (1600-1799)\n",
                      "rnbqkbnr/pppp1ppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -",
                      key_after("e4 e6")));
    // In book at tier 3, absent at tier 5.
    CHECK(explore(db, 5, "e4 c5") == fmt::format("fen: {}\n"
                                                 "key: {}\n"
                                                 "name: B20 Sicilian Defense\n"
                                                 "eval: cp 35 depth 38 best Nf3 g1f3\n"
                                                 "off book at tier 5 (2000-2199)\n",
                                                 sicilian_fen, key_after("e4 c5")));
}

TEST_CASE("explore reports a bad tier, FEN, move or database", "[cli]") {
    const FixtureDb db;
    CHECK(explore(db, 7, std::nullopt) == "error: tier 7 is not one of 0 to 6");
    CHECK(explore(db, -1, std::nullopt) == "error: tier -1 is not one of 0 to 6");
    CHECK(explore(db, 3, std::nullopt, "not a fen") ==
          "error: bad FEN \"not a fen\": not a FEN of four or six fields");
    CHECK(explore(db, 3, "e4 e9") == "error: bad move 2 \"e9\": neither UCI nor SAN");
    CHECK(explore(db, 3, "e4 e4") == "error: bad move 2 \"e4\": not legal in the position");
    CHECK(explore(db, 3, "e2e5") == "error: bad move 1 \"e2e5\": not legal in the position");
    const std::string missing = db.path() + ".missing";
    CHECK(explore(missing, 3, std::nullopt) ==
          "error: cannot open the opening database " + missing + ": no such file");
}

TEST_CASE("explore that fails after its header lines writes nothing", "[cli]") {
    const FixtureDb db;
    const auto start = std::bit_cast<std::int64_t>(key_after(""));
    const auto failing = [&db](bool score) {
        const chesscoach::cli::ExploreOptions options{
            .db = db.path(),
            .tier = 3,
            .fen = std::nullopt,
            .moves = std::nullopt,
            .score = score,
        };
        std::ostringstream out;
        const auto result = chesscoach::cli::run_explore(options, out);
        CHECK_FALSE(result.has_value());
        return out.str();
    };
    SECTION("a stored move that is not legal") {
        {
            const SQLite::Database file(db.path(), SQLite::OPEN_READWRITE);
            SQLite::Statement corrupt(file, "UPDATE moves SET move = 'e2e5' WHERE key = ?1 AND "
                                            "tier = 3 AND move = 'f2f3'");
            corrupt.bind(1, start);
            REQUIRE(corrupt.exec() == 1);
        }
        CHECK(failing(false).empty());
    }
    SECTION("--score at a node without its positions row") {
        {
            const SQLite::Database file(db.path(), SQLite::OPEN_READWRITE);
            SQLite::Statement corrupt(file, "DELETE FROM positions WHERE key = ?1");
            corrupt.bind(1, start);
            REQUIRE(corrupt.exec() == 1);
        }
        CHECK(failing(true).empty());
    }
}

TEST_CASE("name prints the deepest named position along the moves", "[cli]") {
    const FixtureDb db;
    CHECK(name(db, "e4 c5 Nf3") == "B20 Sicilian Defense\n");
    CHECK(name(db, "e2e4 c7c5 g1f3") == "B20 Sicilian Defense\n");
    CHECK(name(db, "e4 c5") == "B20 Sicilian Defense\n");
    CHECK(name(db, "e4 e6") == "B00 King's Pawn Game\n");
    CHECK(name(db, "Nf3 d5") == "A04 Zukertort Opening\n");
    CHECK(name(db, "Nf3 d5 d4") == "D02 Queen's Pawn Game: Zukertort Variation\n");
    CHECK(name(db, "") == "unnamed\n");
    CHECK(name(db, std::nullopt, zukertort_fen) == "D02 Queen's Pawn Game: Zukertort Variation\n");
    CHECK(name(db, std::nullopt, start_fen) == "unnamed\n");
}

TEST_CASE("name reports missing input, a bad FEN, a bad move or database", "[cli]") {
    const FixtureDb db;
    CHECK(name(db, std::nullopt) == "error: name needs --fen or --moves");
    CHECK(name(db, std::nullopt, "8/8/8/8/8/8/8/8 w - -") ==
          "error: bad FEN \"8/8/8/8/8/8/8/8 w - -\": a side does not have exactly one king");
    CHECK(name(db, "e4 e5 Ke2 Ke7 Bb4") == "error: bad move 5 \"Bb4\": not legal in the position");
    const std::string missing = db.path() + ".missing";
    CHECK(name(missing, "e4") ==
          "error: cannot open the opening database " + missing + ": no such file");
}

// Unit tests for GameScanner on hand-written PGN (docs/database.md §3, §6.4): every §3 row, the
// tokens Lichess writes between moves (clock and eval comments, NAGs, annotation glyphs), a bad
// move before and after the depth cap, a game with no moves, and keys equal to core's replay
// (pipeline; unit).
#include "game_scanner.hpp"

#include <chesscoach/position.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

using chesscoach::Key;
using chesscoach::Position;
using chesscoach::pipeline::GameResult;
using chesscoach::pipeline::GameScanner;
using chesscoach::pipeline::ScannedGame;
using chesscoach::pipeline::SkipReason;
using chesscoach::pipeline::strip_glyphs;

namespace {

std::string game(std::string_view event, std::string_view white_elo, std::string_view result,
                 std::string_view moves, std::string_view extra = "") {
    std::string text = "[Event \"" + std::string(event) + "\"]\n[Site \"https://lichess.org/x\"]\n";
    text += "[Result \"" + std::string(result) + "\"]\n";
    text += "[WhiteElo \"" + std::string(white_elo) + "\"]\n[BlackElo \"1700\"]\n";
    text += extra;
    text += "\n" + std::string(moves) + "\n\n";
    return text;
}

struct Scan {
    std::vector<ScannedGame> games;
    chesscoach::pipeline::ScanCounts counts;
};

Scan scan(const std::string& text, std::uint32_t max_ply = 30) {
    GameScanner scanner(max_ply, nullptr);
    Scan result;
    scanner.scan(text, [&](const ScannedGame& scanned) { result.games.push_back(scanned); });
    result.counts = scanner.counts();
    return result;
}

std::vector<Key> replay(const std::vector<std::string>& sans) {
    Position position = Position::start();
    std::vector<Key> keys{position.key()};
    for (const auto& san : sans) {
        REQUIRE(position.play_san(san).has_value());
        keys.push_back(position.key());
    }
    return keys;
}

} // namespace

TEST_CASE("strip_glyphs removes one annotation glyph after the move", "[scanner]") {
    CHECK(strip_glyphs("c4?!") == "c4");
    CHECK(strip_glyphs("Nf3+!!") == "Nf3+");
    CHECK(strip_glyphs("Qxf7#?") == "Qxf7#");
    CHECK(strip_glyphs("e4") == "e4");
    CHECK(strip_glyphs("O-O!?") == "O-O");
    CHECK(strip_glyphs("") == "");
}

TEST_CASE("GameScanner admits a game with comments, NAGs and glyphs", "[scanner]") {
    const auto result =
        scan(game("Rated Blitz game", "1500", "0-1",
                  "1. e4 { [%clk 0:03:00] } 1... e5 { [%eval 0.17] [%clk 0:03:00] } 2. Nf3 $1 "
                  "Nc6 3. Bb5?! a6!? 4. Ba4 Nf6 0-1"));
    REQUIRE(result.games.size() == 1);
    const ScannedGame& scanned = result.games.front();
    CHECK(scanned.result == GameResult::black);
    CHECK(scanned.tier == 3); // the average of 1500 and 1700
    CHECK(scanned.moves.size() == 8);
    CHECK(scanned.keys == replay({"e4", "e5", "Nf3", "Nc6", "Bb5", "a6", "Ba4", "Nf6"}));
    CHECK(result.counts.admitted_total() == 1);
    CHECK(result.counts.skipped.total() == 0);
}

TEST_CASE("GameScanner skips each §3 row with its counter", "[scanner]") {
    const std::string moves = "1. e4 e5 1-0";
    struct Row {
        std::string text;
        SkipReason reason;
    };
    const std::vector<Row> rows{
        {.text = game("Casual Blitz game", "1500", "1-0", moves), .reason = SkipReason::unrated},
        {.text = game("Rated Bullet game", "1500", "1-0", moves), .reason = SkipReason::speed},
        {
            .text = game("Rated Blitz game", "1500", "1-0", moves, "[Variant \"Chess960\"]\n"),
            .reason = SkipReason::variant,
        },
        {.text = game("Rated Blitz game", "?", "1-0", moves), .reason = SkipReason::rating},
        {.text = game("Rated Blitz game", "1500", "*", "1. e4 e5 *"), .reason = SkipReason::result},
        {
            .text = game("Rated Blitz game", "1500", "1-0", "1. e4 e5 2. Ke3 1-0"),
            .reason = SkipReason::moves,
        },
    };
    for (const Row& row : rows) {
        const auto result = scan(row.text);
        CHECK(result.games.empty());
        CHECK(result.counts.skipped.count(row.reason) == 1);
        CHECK(result.counts.skipped.total() == 1);
    }
    const auto tournament = scan(
        game("Rated Blitz tournament https://lichess.org/tournament/abc", "1500", "1-0", moves));
    CHECK(tournament.games.size() == 1);
}

TEST_CASE("GameScanner counts a game whose header cannot be read as malformed", "[scanner]") {
    // A header value longer than a PGN string token's 255 characters.
    const auto result = scan("[Event \"Rated Blitz game\"]\n[Site \"" + std::string(300, 'x') +
                             "\"]\n\n1. e4 1-0\n\n");
    CHECK(result.games.empty());
    CHECK(result.counts.skipped.count(SkipReason::malformed) == 1);
}

TEST_CASE("GameScanner skips a bad move before the cap and keeps one past it", "[scanner]") {
    const std::string bad_at_five = "1. e4 e5 2. Nf3 Nc6 3. Qxh7 a6 1-0";
    const auto skipped = scan(game("Rated Blitz game", "1500", "1-0", bad_at_five));
    CHECK(skipped.games.empty());
    CHECK(skipped.counts.skipped.count(SkipReason::moves) == 1);

    const auto kept = scan(game("Rated Blitz game", "1500", "1-0", bad_at_five), 4);
    REQUIRE(kept.games.size() == 1);
    CHECK(kept.games.front().moves.size() == 4);
    CHECK(kept.games.front().keys.size() == 5);
}

TEST_CASE("GameScanner admits a game with no moves and one with only a comment", "[scanner]") {
    const auto empty = scan(game("Rated Rapid game", "1500", "1-0", "1-0"));
    REQUIRE(empty.games.size() == 1);
    CHECK(empty.games.front().keys == std::vector<Key>{Position::start().key()});
    const auto comment = scan(game("Rated Rapid game", "1500", "1-0", "{ abandoned } 1-0"));
    REQUIRE(comment.games.size() == 1);
    CHECK(comment.games.front().moves.empty());
}

TEST_CASE("GameScanner records the FENs fen_wanted asks for", "[scanner]") {
    GameScanner scanner(30, [](std::uint8_t, Key key) { return key == Position::start().key(); });
    std::vector<ScannedGame> games;
    scanner.scan(game("Rated Blitz game", "1500", "1-0", "1. d4 1-0"),
                 [&](const ScannedGame& scanned) { games.push_back(scanned); });
    REQUIRE(games.size() == 1);
    REQUIRE(games.front().fens.size() == 1);
    CHECK(games.front().fens.front().first == Position::start().key());
    CHECK(games.front().fens.front().second == Position::start().fen());
}

// Unit tests for chesscoach/pgn_import.hpp (core; unit): a hand-written PGN with nested variations,
// a NAG and a comment, a conflict, a move that does not play inside a line with a variation, a
// [FEN] game and a [FEN] that does not parse, imported for white and for black, each choice and
// count worked out by hand; an empty stream; a header without its closing quote.
#include <chesscoach/pgn_import.hpp>

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using chesscoach::Color;
using chesscoach::PgnImport;
using chesscoach::PgnImportError;
using chesscoach::Position;
using chesscoach::RepertoireChoice;

namespace {

constexpr std::string_view repertoire_pgn = R"([Event "Repertoire"]
[Site "?"]

1. e4 e5 (1... c5 2. Nf3 d6 (2... Nc6 3. d4) 3. d4) (1... e6 2. d4) 2. Nf3 $1 Nc6
3. Bb5 {the Ruy Lopez} a6 *

[Event "Second"]

1. e4 e5 2. Nc3 Nf6 *

[Event "Broken"]

1. d4 d5 2. Qx9 c6 (2... e6) 3. c4 *

[Event "From a position"]
[SetUp "1"]
[FEN "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2"]

2. c4 e6 3. Nc3 *

[Event "Bad position"]
[SetUp "1"]
[FEN "not a position"]

1. e4 *
)";

// The choice at the position after the SAN moves from the start position (or from fen).
RepertoireChoice choice(std::string_view moves, std::string_view uci, std::string_view fen = "") {
    auto position = fen.empty() ? Position::start() : Position::from_fen(fen).value();
    std::istringstream words{std::string(moves)};
    for (std::string san; words >> san;) {
        REQUIRE(position.play_san(san).has_value());
    }
    return {.key = position.key(), .fen = position.fen(), .move = std::string(uci)};
}

PgnImport import(std::string_view pgn, Color colour) {
    std::istringstream in{std::string(pgn)};
    const auto result = chesscoach::import_repertoire_pgn(in, colour);
    REQUIRE(result.has_value());
    return *result;
}

} // namespace

TEST_CASE("A white repertoire comes from every game and variation", "[pgn]") {
    // Game 1, in document order: 1. e4; the c5 variation's 2. Nf3, its nested 3. d4 after Nc6 and
    // its 3. d4 after d6; the e6 variation's 2. d4; the main line's 2. Nf3 and 3. Bb5. Game 2's 1.
    // e4 repeats a choice; its 2. Nc3 is a conflict. Game 3's 1. d4 is a conflict, Qx9 does not
    // play and ends the line with its variation. Game 4 starts from its FEN; game 5's FEN does not
    // parse.
    constexpr std::string_view d4_d5 =
        "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2";
    const auto result = import(repertoire_pgn, Color::white);
    CHECK(result.choices == std::vector<RepertoireChoice>{
                                choice("", "e2e4"),
                                choice("e4 c5", "g1f3"),
                                choice("e4 c5 Nf3 Nc6", "d2d4"),
                                choice("e4 c5 Nf3 d6", "d2d4"),
                                choice("e4 e6", "d2d4"),
                                choice("e4 e5", "g1f3"),
                                choice("e4 e5 Nf3 Nc6", "f1b5"),
                                choice("", "c2c4", d4_d5),
                                choice("c4 e6", "b1c3", d4_d5),
                            });
    CHECK(result.games == 5);
    CHECK(result.variations == 4);
    CHECK(result.conflicts == 2);
    CHECK(result.unparsable == 2);
}

TEST_CASE("A black repertoire takes the main line's move before its variations", "[pgn]") {
    // 1... e5 comes before the variations that replace it, so c5 and e6 are conflicts, and so is
    // the nested 2... Nc6 against 2... d6 after 1. e4 c5 2. Nf3.
    const auto game = repertoire_pgn.substr(0, repertoire_pgn.find("[Event \"Second\"]"));
    const auto result = import(game, Color::black);
    CHECK(result.choices == std::vector<RepertoireChoice>{
                                choice("e4", "e7e5"),
                                choice("e4 c5 Nf3", "d7d6"),
                                choice("e4 e5 Nf3", "b8c6"),
                                choice("e4 e5 Nf3 Nc6 Bb5", "a7a6"),
                            });
    CHECK(result.games == 1);
    CHECK(result.variations == 3);
    CHECK(result.conflicts == 3);
    CHECK(result.unparsable == 0);
}

TEST_CASE("An empty stream has no games, and a broken header is an error", "[pgn]") {
    CHECK(import("", Color::white) == PgnImport{});
    std::istringstream broken{"[Event \"no closing quote]\n\n1. e4 *\n"};
    const auto result = chesscoach::import_repertoire_pgn(broken, Color::white);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == PgnImportError::malformed_header);
}

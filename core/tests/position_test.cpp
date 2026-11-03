// Unit tests for chesscoach/position.hpp (core; unit): what from_fen accepts, normalizes and
// rejects (docs/database.md §5.1), SAN and UCI in both directions with castling, promotions,
// disambiguation and suffixes (§5.3), and each error leaving the position as it was.
#include <chesscoach/position.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using chesscoach::Color;
using chesscoach::FenError;
using chesscoach::MoveError;
using chesscoach::PlayedMove;
using chesscoach::Position;

namespace {

constexpr std::string_view start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";

Position parse(std::string_view fen) {
    auto position = Position::from_fen(fen);
    REQUIRE(position.has_value());
    return *position;
}

FenError fen_error(std::string_view fen) {
    const auto position = Position::from_fen(fen);
    REQUIRE_FALSE(position.has_value());
    return position.error();
}

MoveError san_error(Position position, std::string_view san) {
    const auto key = position.key();
    const auto fen = position.fen();
    const auto played = position.play_san(san);
    REQUIRE_FALSE(played.has_value());
    CHECK(position.key() == key);
    CHECK(position.fen() == fen);
    return played.error();
}

MoveError uci_error(Position position, std::string_view uci) {
    const auto key = position.key();
    const auto played = position.play_uci(uci);
    REQUIRE_FALSE(played.has_value());
    CHECK(position.key() == key);
    return played.error();
}

PlayedMove play_san(Position& position, std::string_view san) {
    const auto played = position.play_san(san);
    REQUIRE(played.has_value());
    return *played;
}

} // namespace

TEST_CASE("The start position", "[position]") {
    const auto start = Position::start();
    CHECK(start.fen() == start_fen);
    CHECK(start.side_to_move() == Color::white);
    CHECK_FALSE(start.in_check());
    CHECK(start.legal_moves_uci() ==
          std::vector<std::string>{"a2a3", "a2a4", "b1a3", "b1c3", "b2b3", "b2b4", "c2c3",
                                   "c2c4", "d2d3", "d2d4", "e2e3", "e2e4", "f2f3", "f2f4",
                                   "g1f3", "g1h3", "g2g3", "g2g4", "h2h3", "h2h4"});
}

TEST_CASE("from_fen reads four or six fields and prints the normalized four", "[position]") {
    CHECK(parse(start_fen).fen() == start_fen);
    CHECK(parse("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1").fen() == start_fen);
    CHECK(parse("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 12 40").fen() == start_fen);
    CHECK(parse("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w kqQK -").fen() == start_fen);
    CHECK(parse("rnbqkbnr/pppppppp/44/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -").fen() == start_fen);
    for (const std::string_view normalized : {
             "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -",
             "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6",
             "rnbqkbnr/pppp1ppp/8/8/3pP3/8/PPP2PPP/RNBQKBNR b KQkq e3",
             "r3k2r/8/8/8/8/8/8/R3K2R b Kq -",
             "8/8/8/KPp4r/8/8/8/7k w - -",
         }) {
        CAPTURE(normalized);
        const auto position = parse(normalized);
        CHECK(position.fen() == normalized);
        CHECK(parse(position.fen()).key() == position.key());
    }
    const auto black = parse("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -");
    CHECK(black.side_to_move() == Color::black);
}

TEST_CASE("from_fen drops an en passant target with no legal capture", "[position]") {
    // No black pawn beside e4.
    CHECK(parse("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3").fen() ==
          "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -");
    // The target is on the wrong rank for the side to move.
    CHECK(parse("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f3").fen() ==
          "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq -");
    // A white pawn on e5 could capture on d6, but no black pawn stands on d5.
    CHECK(parse("rnbqkbnr/ppp1pppp/8/4P3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6").fen() ==
          "rnbqkbnr/ppp1pppp/8/4P3/8/8/PPPP1PPP/RNBQKBNR w KQkq -");
    // The pawn is there, but the square it passed is occupied, so it did not just double push.
    CHECK(parse("rnbqkbnr/ppp1pppp/3b4/3pP3/8/8/PPPP1PPP/RNBQK1NR w KQkq d6").fen() ==
          "rnbqkbnr/ppp1pppp/3b4/3pP3/8/8/PPPP1PPP/RNBQK1NR w KQkq -");
    // The push was impossible: with the pawn back on g2, white to move would have the black king
    // on h3 in check, so the position before it was illegal.
    const auto impossible = parse("2Q5/K1Q5/8/8/5pPp/7k/8/8 b - g3");
    CHECK(impossible.fen() == "2Q5/K1Q5/8/8/5pPp/7k/8/8 b - -");
    const auto moves = impossible.legal_moves_uci();
    CHECK(std::ranges::find(moves, "f4g3") == moves.end());
    CHECK(std::ranges::find(moves, "h4g3") == moves.end());
    CHECK(impossible.key() == parse("2Q5/K1Q5/8/8/5pPp/7k/8/8 b - -").key());
    // A double check by the pushed pawn and a queen: with the pawn back on e2 the queen alone
    // checks the black king, white to move, so the push never happened.
    CHECK(parse("8/3Q4/1q6/5k2/4Pp2/6PK/8/8 b - e3").fen() == "8/3Q4/1q6/5k2/4Pp2/6PK/8/8 b - -");
    // The capture would expose the king along the rank.
    CHECK(parse("8/8/8/KPp4r/8/8/8/7k w - c6").fen() == "8/8/8/KPp4r/8/8/8/7k w - -");
    CHECK(parse("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3").key() ==
          parse("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -").key());
}

TEST_CASE("from_fen rejects a text that is not a FEN as malformed", "[position]") {
    for (const std::string_view fen : {
             "",
             " ",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 x",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR  w KQkq -",
             " rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - ",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR\tw KQkq -",
             "rnbqkbnr/pppppppp/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
             "rnbqkbnr/pppppppp/8/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
             "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
             "rnbqkbnr/ppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
             "rnbqkbnr/ppppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
             "rnbqkbnr/pppppppp/81/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR/ w KQkq -",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNX w KQkq -",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN0 w KQkq -",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR W KQkq -",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR white KQkq -",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KKkq -",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkqX -",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w -- -",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e4",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq i6",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e66",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - a 1",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 -1",
             "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 +1",
             "garbage",
         }) {
        CAPTURE(fen);
        CHECK(fen_error(fen) == FenError::malformed);
    }
}

TEST_CASE("from_fen rejects a board without exactly one king per side", "[position]") {
    CHECK(fen_error("rnbq1bnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - -") == FenError::kings);
    CHECK(fen_error("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQ1BNR w - -") == FenError::kings);
    CHECK(fen_error("8/8/8/8/8/8/8/8 w - -") == FenError::kings);
    CHECK(fen_error("k6k/8/8/8/8/8/8/K7 w - -") == FenError::kings);
    CHECK(fen_error("k7/8/8/8/8/8/8/K6K w - -") == FenError::kings);
}

TEST_CASE("from_fen rejects a position that cannot arise as illegal", "[position]") {
    // A castling right without its king and rook on their home squares.
    CHECK(fen_error("4k3/8/8/8/8/8/8/4K1R1 w K -") == FenError::illegal);
    CHECK(fen_error("4k3/8/8/8/8/8/8/R4K2 w Q -") == FenError::illegal);
    CHECK(fen_error("r2k4/8/8/8/8/8/8/4K3 b q -") == FenError::illegal);
    CHECK(fen_error("4k3/8/8/8/8/8/8/4K3 b k -") == FenError::illegal);
    // The side not to move in check.
    CHECK(fen_error("4k3/8/8/8/8/8/8/4R1K1 w - -") == FenError::illegal);
    CHECK(fen_error("4k3/8/8/8/8/8/3q4/4K3 b - -") == FenError::illegal);
    // A pawn on the first or last rank.
    CHECK(fen_error("P3k3/8/8/8/8/8/8/4K3 w - -") == FenError::illegal);
    CHECK(fen_error("4k3/8/8/8/8/8/8/p3K3 w - -") == FenError::illegal);
    // The side to move may be in check.
    CHECK(parse("4k3/8/8/8/8/8/8/4R1K1 b - -").in_check());
}

TEST_CASE("play_san plays SAN and gives the move's UCI, SAN and child key", "[position]") {
    auto position = Position::start();
    const auto e4 = play_san(position, "e4");
    CHECK(e4.uci == "e2e4");
    CHECK(e4.san == "e4");
    CHECK(e4.child == position.key());
    CHECK(play_san(position, "Nf6").uci == "g8f6");
    CHECK(play_san(position, "e5").uci == "e4e5");
    CHECK(play_san(position, "d5").uci == "d7d5");
    const auto en_passant = play_san(position, "exd6");
    CHECK(en_passant.uci == "e5d6");
    CHECK(en_passant.san == "exd6");
    CHECK(position.fen() == "rnbqkb1r/ppp1pppp/3P1n2/8/8/8/PPPP1PPP/RNBQKBNR b KQkq -");
}

TEST_CASE("Castling is written O-O or 0-0 and played as the king's two-square move", "[position]") {
    const auto castling = parse("r3k2r/8/8/8/8/8/8/R3K2R w KQkq -");
    for (const std::string_view san : {"O-O", "0-0", "O-O+", "O-O!"}) {
        CAPTURE(san);
        auto position = castling;
        const auto played = play_san(position, san);
        CHECK(played.uci == "e1g1");
        CHECK(played.san == "O-O");
        CHECK(position.fen() == "r3k2r/8/8/8/8/8/8/R4RK1 b kq -");
    }
    for (const std::string_view san : {"O-O-O", "0-0-0"}) {
        CAPTURE(san);
        // The d-pawns keep each rook on d1 or d8 off the other king's path.
        auto position = parse("r3k2r/3p4/8/8/8/8/3P4/R3K2R w KQkq -");
        CHECK(play_san(position, san).uci == "e1c1");
        CHECK(play_san(position, san).uci == "e8c8");
        CHECK(position.fen() == "2kr3r/3p4/8/8/8/8/3P4/2KR3R w - -");
    }
    // Castling through an attacked square is illegal.
    CHECK(san_error(parse("r3k2r/8/8/8/8/8/8/2KR3R b kq -"), "O-O-O") == MoveError::illegal);
    CHECK(castling.uci_of("O-O").value() == "e1g1");
    CHECK(castling.san_of("e1c1").value() == "O-O-O");
    CHECK(uci_error(castling, "e1h1") == MoveError::illegal);
    CHECK(uci_error(castling, "e1a1") == MoveError::illegal);
    CHECK(san_error(Position::start(), "O-O") == MoveError::illegal);
    CHECK(san_error(Position::start(), "0-0-0") == MoveError::illegal);
}

TEST_CASE("Promotions name their piece", "[position]") {
    const auto promotion = parse("3r4/4P3/8/8/8/k7/8/7K w - -");
    struct Case {
        std::string_view san;
        std::string_view uci;
        std::string_view written;
    };
    for (const auto& [san, uci, written] : {
             Case{.san = "e8=Q", .uci = "e7e8q", .written = "e8=Q"},
             Case{.san = "e8=R", .uci = "e7e8r", .written = "e8=R"},
             Case{.san = "e8=B", .uci = "e7e8b", .written = "e8=B"},
             Case{.san = "e8=N", .uci = "e7e8n", .written = "e8=N"},
             Case{.san = "exd8=Q", .uci = "e7d8q", .written = "exd8=Q"},
             Case{.san = "exd8=N", .uci = "e7d8n", .written = "exd8=N"},
         }) {
        CAPTURE(san);
        auto position = promotion;
        const auto played = play_san(position, san);
        CHECK(played.uci == uci);
        CHECK(played.san == written);
        CHECK(promotion.san_of(uci).value() == written);
    }
    CHECK(san_error(promotion, "e8") == MoveError::ambiguous);
    CHECK(san_error(promotion, "exd8") == MoveError::ambiguous);
    CHECK(san_error(promotion, "e8Q") == MoveError::malformed);
    CHECK(san_error(promotion, "e8=K") == MoveError::malformed);
    CHECK(san_error(promotion, "e8=P") == MoveError::malformed);
    CHECK(san_error(promotion, "e8=") == MoveError::malformed);
    CHECK(san_error(promotion, "e8=q") == MoveError::malformed);
    CHECK(uci_error(promotion, "e7e8") == MoveError::illegal);
    CHECK(uci_error(promotion, "e7e8k") == MoveError::malformed);
    CHECK(uci_error(promotion, "e7e8Q") == MoveError::malformed);
}

TEST_CASE("SAN disambiguates by file, rank or square, and only when it must", "[position]") {
    const auto knights = parse("4k3/8/8/8/8/5N2/8/1N2K3 w - -");
    CHECK(san_error(knights, "Nd2") == MoveError::ambiguous);
    CHECK(knights.uci_of("Nbd2").value() == "b1d2");
    CHECK(knights.uci_of("Nfd2").value() == "f3d2");
    CHECK(knights.uci_of("Nf3d2").value() == "f3d2");
    CHECK(knights.san_of("b1d2").value() == "Nbd2");
    CHECK(knights.san_of("f3h4").value() == "Nh4");
    CHECK(knights.uci_of("Nfh4").value() == "f3h4");

    const auto rooks = parse("4k3/8/8/R7/8/8/8/R3K3 w - -");
    CHECK(san_error(rooks, "Ra3") == MoveError::ambiguous);
    CHECK(rooks.uci_of("R1a3").value() == "a1a3");
    CHECK(rooks.uci_of("R5a3").value() == "a5a3");
    CHECK(rooks.san_of("a5a3").value() == "R5a3");

    // Two knights reach d2, but the one on b1 is pinned, so Nd2 is the f3 knight's.
    const auto pinned = parse("4k3/8/8/8/8/5N2/8/qN2K3 w - -");
    CHECK(pinned.uci_of("Nd2").value() == "f3d2");
    CHECK(pinned.san_of("f3d2").value() == "Nd2");
}

TEST_CASE("Check and mate suffixes are written, and ignored when read", "[position]") {
    auto position = Position::start();
    for (const std::string_view san : {"e4", "e5", "Bc4", "Nc6", "Qh5", "Nf6"}) {
        play_san(position, san);
    }
    CHECK(position.san_of("h5f7").value() == "Qxf7#");
    CHECK(position.uci_of("Qxf7").value() == "h5f7");
    CHECK(position.uci_of("Qxf7+").value() == "h5f7");
    CHECK(position.uci_of("Qxf7#!!").value() == "h5f7");
    CHECK(position.uci_of("Qxf7?!").value() == "h5f7");
    CHECK(san_error(position, "Qxf7#!!!") == MoveError::malformed);
    CHECK(san_error(position, "Qxf7++") == MoveError::malformed);
    const auto mate = play_san(position, "Qxf7");
    CHECK(mate.san == "Qxf7#");
    CHECK(position.in_check());
    CHECK(position.legal_moves_uci().empty());

    auto check = parse("4k3/8/8/8/8/8/8/R3K3 w - -");
    CHECK(play_san(check, "Ra8").san == "Ra8+");
    CHECK(check.in_check());
}

TEST_CASE("play_san_fast plays the same move without writing its SAN", "[position]") {
    auto slow = Position::start();
    auto fast = Position::start();
    for (const std::string_view san : {"e4", "e5", "Nf3", "Nc6", "Bb5", "a6", "O-O"}) {
        const auto played = play_san(slow, san);
        const auto quick = fast.play_san_fast(san);
        REQUIRE(quick.has_value());
        CHECK(quick->uci == played.uci);
        CHECK(quick->child == played.child);
        CHECK(quick->san.empty());
    }
    CHECK(fast.fen() == slow.fen());
    CHECK(fast.play_san_fast("Qd9").error() == MoveError::malformed);
}

TEST_CASE("Each MoveError, and the position unchanged after it", "[position]") {
    const auto start = Position::start();
    for (const std::string_view san : {
             "",   "e",   "e9",   "i4",  "E4",   "Pe4",   "Xe4",   "O",  "0", "Ox",  "O-",  "O-O-",
             "OO", "O-0", "e4e5", "exd", "Nxx4", "N11c3", "Nbbc3", "Nx", "+", "e4 ", " e4",
         }) {
        CAPTURE(san);
        CHECK(san_error(start, san) == MoveError::malformed);
    }
    for (const std::string_view san : {"e5", "Ke2", "Kee2", "Nd2", "Bc4", "exd3", "Qh5", "Ra3"}) {
        CAPTURE(san);
        CHECK(san_error(start, san) == MoveError::illegal);
    }
    for (const std::string_view uci :
         {"", "e2", "e2e", "e2e4 ", "E2E4", "e2-e4", "e2e4e5", "i2i4", "e0e4", "e2e9", "0000"}) {
        CAPTURE(uci);
        CHECK(uci_error(start, uci) == MoveError::malformed);
    }
    for (const std::string_view uci : {"e2e5", "e1e2", "e7e5", "g1g3", "e2e4q", "a1a1"}) {
        CAPTURE(uci);
        CHECK(uci_error(start, uci) == MoveError::illegal);
    }
    CHECK(start.san_of("e2e5").error() == MoveError::illegal);
    CHECK(start.san_of("e2").error() == MoveError::malformed);
    CHECK(start.uci_of("Nd2").error() == MoveError::illegal);
    CHECK(start.uci_of("N").error() == MoveError::malformed);
    CHECK(start.san_of("g1f3").value() == "Nf3");
    CHECK(start.uci_of("Nf3").value() == "g1f3");
    CHECK(start.uci_of("Nb1c3").value() == "b1c3");
    CHECK(start.uci_of("Nc3+").value() == "b1c3");
}

TEST_CASE("Positions copy and assign by value", "[position]") {
    auto original = Position::start();
    auto copy = original;
    play_san(copy, "e4");
    CHECK(original.fen() == start_fen);
    CHECK(copy.key() != original.key());
    original = copy;
    CHECK(original.key() == copy.key());
    const auto moved = std::move(copy);
    CHECK(moved.key() == original.key());
    copy = Position::start();
    CHECK(copy.fen() == start_fen);
}

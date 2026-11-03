// Unit tests for the key (core; unit): the tables against the values pinned in docs/database.md
// §5.2 and by independent implementations of the contract (in C and Python), the bit cast
// SQLite stores, and the rules of §5.1 and §5.4 as Position applies them: en passant only when a
// capture is legal, transpositions, castling rights, and the counters left out.
#include <chesscoach/key.hpp>
#include <chesscoach/position.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string_view>

using chesscoach::Key;
using chesscoach::Position;
namespace zobrist = chesscoach::zobrist;

namespace {

// The XOR of the piece-square values of a FEN placement field, from the tables alone.
Key placement_hash(std::string_view placement) {
    constexpr std::string_view pieces = "PNBRQKpnbrqk";
    Key key = 0;
    int rank = 7;
    int file = 0;
    for (const char c : placement) {
        if (c == '/') {
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else {
            key ^= zobrist::piece_square(static_cast<int>(pieces.find(c)), (rank * 8) + file);
            ++file;
        }
    }
    return key;
}

// The position after SAN moves from the start position.
Position after(std::initializer_list<std::string_view> moves) {
    auto position = Position::start();
    for (const auto san : moves) {
        REQUIRE(position.play_san(san).has_value());
    }
    return position;
}

Position parse(std::string_view fen) {
    auto position = Position::from_fen(fen);
    REQUIRE(position.has_value());
    return *position;
}

Key all_castling_rights() {
    return zobrist::castling(zobrist::CastlingRight::white_king_side) ^
           zobrist::castling(zobrist::CastlingRight::white_queen_side) ^
           zobrist::castling(zobrist::CastlingRight::black_king_side) ^
           zobrist::castling(zobrist::CastlingRight::black_queen_side);
}

} // namespace

TEST_CASE("The table is splitmix64 from the contract's seed", "[key]") {
    STATIC_CHECK(zobrist::table.size() == 781);
    STATIC_CHECK(zobrist::table.front() == 0xf777eaa02d011cb2ULL);
    STATIC_CHECK(zobrist::table.back() == 0xba55f004c0776ec4ULL);
    CHECK(zobrist::piece_square(0, 0) == zobrist::table.at(0));
    CHECK(zobrist::piece_square(11, 63) == zobrist::table.at(767));
    CHECK(zobrist::black_to_move() == zobrist::table.at(768));
    CHECK(zobrist::castling(zobrist::CastlingRight::white_king_side) == zobrist::table.at(769));
    CHECK(zobrist::castling(zobrist::CastlingRight::black_queen_side) == zobrist::table.at(772));
    CHECK(zobrist::en_passant_file(0) == zobrist::table.at(773));
    CHECK(zobrist::en_passant_file(7) == zobrist::table.at(780));
}

TEST_CASE("The tables give the pinned keys", "[key]") {
    const Key start =
        placement_hash("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR") ^ all_castling_rights();
    CHECK(start == 0x8b26ecc3ca52d0bbULL);
    CHECK(start == 10026961946044190907ULL);

    // 1. e4: no black pawn can capture on e3, so no en passant value; black to move.
    const Key after_e4 = placement_hash("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR") ^
                         zobrist::black_to_move() ^ all_castling_rights();
    CHECK(after_e4 == 0xa355c969b7c91f2cULL);

    // 1. e4 d5 2. e5 f5: white can capture on f6, so the f file's value; white to move.
    const Key en_passant = placement_hash("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR") ^
                           all_castling_rights() ^ zobrist::en_passant_file(5);
    CHECK(en_passant == 0x52a1b5ffe21f9e7dULL);
    CHECK(en_passant == 5954240292975517309ULL);
}

TEST_CASE("A key is stored as the int64 with the same bits", "[key]") {
    CHECK(chesscoach::key_to_sql(0x8b26ecc3ca52d0bbULL) == -8419782127665360709LL);
    CHECK(chesscoach::key_from_sql(-8419782127665360709LL) == 0x8b26ecc3ca52d0bbULL);
    CHECK(chesscoach::key_to_sql(0) == 0);
    CHECK(chesscoach::key_to_sql(std::numeric_limits<Key>::max()) == -1);
    CHECK(chesscoach::key_to_sql(Key{1} << 63U) == std::numeric_limits<std::int64_t>::min());
    for (const Key key : {
             Key{0},
             Key{1},
             Key{0x7fffffffffffffffULL},
             Key{0x8000000000000000ULL},
             Key{0xa355c969b7c91f2cULL},
             std::numeric_limits<Key>::max(),
         }) {
        CHECK(chesscoach::key_from_sql(chesscoach::key_to_sql(key)) == key);
    }
}

TEST_CASE("Position gives the pinned keys", "[key]") {
    auto position = Position::start();
    CHECK(position.key() == 0x8b26ecc3ca52d0bbULL);
    const auto played = position.play_san("e4");
    REQUIRE(played.has_value());
    CHECK(played->child == 0xa355c969b7c91f2cULL);
    CHECK(position.key() == 0xa355c969b7c91f2cULL);
    CHECK(position.fen() == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -");
    CHECK(parse("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -").key() ==
          0x8b26ecc3ca52d0bbULL);
}

TEST_CASE("A double push keys its en passant file only when a capture is legal", "[key]") {
    SECTION("no pawn beside the pushed one") {
        const auto position = after({"e4"});
        CHECK(position.fen() == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -");
    }
    SECTION("a pawn beside it that can capture") {
        const auto position = after({"e4", "d5", "e5", "f5"});
        CHECK(position.fen() == "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6");
        CHECK(position.key() == 0x52a1b5ffe21f9e7dULL);
        const auto without = parse("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq -");
        CHECK((position.key() ^ without.key()) == zobrist::en_passant_file(5));
        CHECK(parse(position.fen()).key() == position.key());
    }
    SECTION("a pawn beside it pinned along the rank, so the capture is illegal") {
        auto position = parse("8/2p5/8/KP5r/8/8/8/7k b - -");
        REQUIRE(position.play_uci("c7c5").has_value());
        CHECK(position.fen() == "8/8/8/KPp4r/8/8/8/7k w - -");
        CHECK(position.key() == parse("8/8/8/KPp4r/8/8/8/7k w - -").key());
        CHECK(parse("8/8/8/KPp4r/8/8/8/7k w - c6").fen() == "8/8/8/KPp4r/8/8/8/7k w - -");
    }
}

TEST_CASE("Move orders that transpose reach one key", "[key]") {
    const auto one = after({"e4", "e5", "Nf3", "Nc6"});
    const auto other = after({"Nf3", "Nc6", "e4", "e5"});
    CHECK(one.key() == other.key());
    CHECK(one.fen() == other.fen());
    CHECK(one.fen() == "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq -");
}

TEST_CASE("Losing a castling right changes the key by that right's value", "[key]") {
    const auto position = after({"Nf3", "Nf6", "Rg1", "Rg8", "Rh1", "Rh8"});
    CHECK(position.fen() == "rnbqkb1r/pppppppp/5n2/8/8/5N2/PPPPPPPP/RNBQKB1R w Qq -");
    const auto with_rights = parse("rnbqkb1r/pppppppp/5n2/8/8/5N2/PPPPPPPP/RNBQKB1R w KQkq -");
    CHECK((position.key() ^ with_rights.key()) ==
          (zobrist::castling(zobrist::CastlingRight::white_king_side) ^
           zobrist::castling(zobrist::CastlingRight::black_king_side)));

    const auto king_moved = after({"e4", "e5", "Ke2", "Ke7", "Ke1", "Ke8"});
    CHECK(king_moved.fen() == "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w - -");
    CHECK((king_moved.key() ^ after({"e4", "e5"}).key()) ==
          (zobrist::castling(zobrist::CastlingRight::white_king_side) ^
           zobrist::castling(zobrist::CastlingRight::white_queen_side) ^
           zobrist::castling(zobrist::CastlingRight::black_king_side) ^
           zobrist::castling(zobrist::CastlingRight::black_queen_side)));
}

TEST_CASE("The move counters are not part of the key", "[key]") {
    const auto start = Position::start();
    CHECK(after({"Nf3", "Nf6", "Ng1", "Ng8"}).key() == start.key());
    CHECK(parse("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1").key() == start.key());
    CHECK(parse("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 37 99").key() == start.key());
}

// Unit tests for the key (core; unit): the tables against the values pinned in docs/database.md
// §5.2 and by independent implementations of the contract (in C and Python), and the bit cast
// SQLite stores.
#include <chesscoach/key.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string_view>

using chesscoach::Key;
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

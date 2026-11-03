// The position key (docs/database.md §5.2): the Key type, the 781 Zobrist values, computed at
// compile time by splitmix64 from the contract's seed in its draw order, and the bit cast SQLite
// stores. Only these tables compute a key; Position (chesscoach/position.hpp) applies them, from
// scratch when a FEN is parsed and incrementally on every move. Pure, constexpr, and uint64_t
// arithmetic throughout, so nothing here can overflow a signed type.
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace chesscoach {

// The 64-bit Zobrist hash of a normalized position (§5.1). Shown as unsigned decimal; stored in
// SQLite as the int64 with the same bits (key_to_sql, key_from_sql).
using Key = std::uint64_t;

namespace zobrist {

// Where each group of values starts in the table, in the contract's draw order: piece-square
// values first (piece * 64 + square), then black to move, the four castling rights and the
// eight en passant files.
inline constexpr std::size_t black_to_move_index = 768;
inline constexpr std::size_t castling_offset = 769;
inline constexpr std::size_t en_passant_offset = 773;
inline constexpr std::size_t table_size = 781;

// The 781 consecutive outputs of splitmix64 from the state 0x434845535320434F ("CHESS CO").
[[nodiscard]] consteval std::array<Key, table_size> make_table() noexcept {
    std::array<Key, table_size> values{};
    Key state = 0x434845535320434FULL;
    for (auto& value : values) {
        state += 0x9E3779B97F4A7C15ULL;
        Key z = state;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        value = z ^ (z >> 31U);
    }
    return values;
}

inline constexpr std::array<Key, table_size> table = make_table();

// The four castling rights in the contract's order: K, Q, k, q.
enum class CastlingRight : std::uint8_t {
    white_king_side,
    white_queen_side,
    black_king_side,
    black_queen_side,
};

// piece 0 to 11 is white P, N, B, R, Q, K then black P, N, B, R, Q, K; square 0 to 63 is
// rank * 8 + file, a1 = 0, h8 = 63.
[[nodiscard]] constexpr Key piece_square(int piece, int square) {
    return table.at((static_cast<std::size_t>(piece) * 64) + static_cast<std::size_t>(square));
}

[[nodiscard]] constexpr Key black_to_move() { return table.at(black_to_move_index); }

[[nodiscard]] constexpr Key castling(CastlingRight right) {
    return table.at(castling_offset + static_cast<std::size_t>(right));
}

// file 0 to 7 is a to h.
[[nodiscard]] constexpr Key en_passant_file(int file) {
    return table.at(en_passant_offset + static_cast<std::size_t>(file));
}

} // namespace zobrist

// The signed 64-bit integer with the key's bits, as the key and child columns store it.
[[nodiscard]] constexpr std::int64_t key_to_sql(Key key) noexcept {
    return std::bit_cast<std::int64_t>(key);
}

// The key whose bits a stored int64 holds; key_from_sql(key_to_sql(k)) == k for every k.
[[nodiscard]] constexpr Key key_from_sql(std::int64_t value) noexcept {
    return std::bit_cast<Key>(value);
}

} // namespace chesscoach

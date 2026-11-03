// Squares and the piece placement as the board draws them: a square is rank * 8 + file with
// a1 = 0 and h8 = 63, the numbering of docs/database.md §5.2; its name is the file letter and
// the rank digit, as UCI writes it (§5.3). The placement is read from the first field of the
// position's normalized FEN, for display only: the rules stay core's Position's. Pure.
#pragma once

#include <chesscoach/position.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace chesscoach::app {

inline constexpr int square_count = 64;

[[nodiscard]] constexpr int file_of(int square) noexcept { return square % 8; }
[[nodiscard]] constexpr int rank_of(int square) noexcept { return square / 8; }
[[nodiscard]] constexpr int square_of(int file, int rank) noexcept { return (rank * 8) + file; }

// "e4" for 28.
[[nodiscard]] std::string square_name(int square);

// 28 for "e4"; nullopt for anything that is not a file letter and a rank digit.
[[nodiscard]] std::optional<int> square_from_name(std::string_view name);

// A UCI move's from and to squares and its promotion letter (0 when none).
struct UciMove {
    int from = 0;
    int to = 0;
    char promotion = 0;
};
[[nodiscard]] std::optional<UciMove> parse_uci(std::string_view uci);

// The piece on every square as its FEN letter (KQRBNP white, kqrbnp black), 0 when empty.
using Placement = std::array<char, square_count>;
[[nodiscard]] Placement placement_of(const Position& position);

// The square of the side to move's king, or nullopt when the placement has none.
[[nodiscard]] std::optional<int> king_square(const Placement& placement, Color side);

} // namespace chesscoach::app

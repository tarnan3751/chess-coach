// The key of a chess-library board computed from scratch with our tables (docs/database.md §5.2):
// what from_fen stores and what the incremental update is asserted equal to. Private to core,
// like every include of chess.hpp, which comes in as a system header.
#pragma once

#include <chesscoach/key.hpp>

#include <chess-library/chess.hpp>

namespace chesscoach {

// The XOR of every piece on its square, black to move, each castling right the board holds, and
// the en passant file when the board keeps a target. Boards are played with makeMove<true> and
// set from normalized FENs, so a kept target always has a legal capture.
[[nodiscard]] Key key_from_scratch(const chess::Board& board);

} // namespace chesscoach

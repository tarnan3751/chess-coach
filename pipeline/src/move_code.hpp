// MoveCode: a UCI move (docs/database.md §5.3) packed into 16 bits for pass two's counts, which
// hold one per (node, move): the from square in bits 0-5, the to square in bits 6-11 (a1 = 0 ...
// h8 = 63) and the promotion piece in bits 12-14 (0 none, then n, b, r, q). encode_move and
// decode_move round-trip every well-formed UCI move. Pure.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace chesscoach::pipeline {

using MoveCode = std::uint16_t;

// The code of a UCI move ("e2e4", "e7e8q"), or nullopt when the text is not one.
[[nodiscard]] std::optional<MoveCode> encode_move(std::string_view uci);

// The UCI text of a code encode_move produced.
[[nodiscard]] std::string decode_move(MoveCode code);

} // namespace chesscoach::pipeline

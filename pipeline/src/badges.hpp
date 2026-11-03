// The badges of docs/database.md §7, computed once, after the evaluation merge, for every move
// of every surviving node: Popular from the move's share at its tier, Book from its share at
// the top tier and its evaluation loss, Dubious from Popular and the loss. Shares are compared
// in integers (`sum > 0 and 100 * total >= x * sum`); the loss reads the evaluations of the
// parent and the child from the mover's side, mate m as sign(m) * (100000 - |m|). Pure.
#pragma once

#include "tree.hpp"

#include <cstdint>
#include <optional>

namespace chesscoach::pipeline {

// The thresholds of §7.1.
inline constexpr std::uint64_t popular_share_percent = 10;
inline constexpr std::uint64_t book_share_percent = 5;
inline constexpr std::int64_t book_margin_cp = 50;
inline constexpr std::int64_t dubious_margin_cp = 100;

// §7.2's value of an evaluation, in centipawns from white's side.
[[nodiscard]] std::int64_t evaluation_value(const Evaluation& evaluation);

// §7.2's loss of a move from parent to child, white_to_move at the parent; nullopt unless both
// are evaluated.
[[nodiscard]] std::optional<std::int64_t> evaluation_loss(const PositionRecord& parent,
                                                          const PositionRecord* child);

// Sets the badges of every move in tree.nodes, which must be sorted by key then tier.
void compute_badges(Tree& tree);

} // namespace chesscoach::pipeline

// The badges of docs/database.md §7, computed once, after the evaluation merge, for every move
// of every surviving node: Popular from the move's share at its tier, Book from its share at
// the top tier and its evaluation loss, Dubious from Popular and the loss. Shares are compared
// in integers (`sum > 0 and 100 * total >= x * sum`); the loss and the Book margin are core's
// (score.hpp's evaluation_value, evaluation_loss and book_margin_cp), so the badges and the
// recommendation score read evaluations by one rule. Pure.
#pragma once

#include "tree.hpp"

#include <cstdint>
#include <optional>

namespace chesscoach::pipeline {

// The thresholds of §7.1 but the Book margin, which is core's book_margin_cp (score.hpp).
inline constexpr std::uint64_t popular_share_percent = 10;
inline constexpr std::uint64_t book_share_percent = 5;
inline constexpr std::int64_t dubious_margin_cp = 100;

// §7.2's loss of a move from parent to child (core's evaluation_loss), the side to move read from
// the parent's FEN; nullopt unless both are evaluated.
[[nodiscard]] std::optional<std::int64_t> evaluation_loss(const PositionRecord& parent,
                                                          const PositionRecord* child);

// Sets the badges of every move in tree.nodes, which must be sorted by key then tier.
void compute_badges(Tree& tree);

} // namespace chesscoach::pipeline

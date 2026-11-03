// The recommendation score (PLAN.md §5.2; docs/database.md §13; docs/reference/core.md,
// **The recommendation score**): for a position at a tier, every move with a moves row there
// is a candidate, gated by soundness (its §7.2 loss within the Book margin, where the loss is
// defined) and ranked by its expected score at the tier, shrunk toward one half when it has few
// games, less a simplicity penalty for the replies it leaves to prepare. Every component is
// exposed, so the UI and the CLI can show why a move ranks where it does. Pure over the store's
// rows: score_moves takes the rows and a lookup for the children's, so it is tested without
// SQLite; the overload on an OpeningStore only reads them. Each value is one IEEE operation on
// integers or on another value, in its own statement, so the numbers, and the order, are the
// same on every platform.
#pragma once

#include <chesscoach/key.hpp>
#include <chesscoach/opening_store.hpp>
#include <chesscoach/tier.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace chesscoach {

// The numbers of the score; tier A to change (docs/reference/core.md).
inline constexpr std::int64_t soundness_margin_cp = 50; // §13: the Book margin of §7.1
inline constexpr std::int64_t shrink_games = 30;        // the games at which a move weighs half
inline constexpr std::int64_t coverage_percent = 80;    // the replies a move leaves to prepare
inline constexpr double penalty_per_reply = 0.02;       // per reply beyond the first
inline constexpr double penalty_cap = 0.3;

// One candidate's components and its score.
struct ScoreComponents {
    std::string move;                 // UCI (§5.3)
    bool sound = true;                // the loss is undefined or at most soundness_margin_cp
    std::optional<std::int64_t> loss; // §7.2's loss in centipawns, nullopt when undefined
    double expected_score = 0.5; // (wins + draws / 2 + 15) / (total + 30), from the mover's side
    std::int64_t total = 0;      // the move's plays at the tier
    int replies_needed = 0;      // the fewest of the child's most-played replies covering
                                 // coverage_percent of its move sum; 0, so no penalty, when
                                 // the child is off book at the tier or in book there with
                                 // no moves (visited only at the cap, or where games ended)
    bool child_in_book = false;  // the child has a nodes row at the tier (§7.3)
    double penalty = 0.0;        // min(penalty_cap, penalty_per_reply * max(0, replies - 1))
    double score = 0.0;          // sound ? max(0, expected_score - penalty) : 0
    double share = 0.0;          // the move's share of the position's plays at the tier,
                                 // exposed, not in the score

    friend bool operator==(const ScoreComponents&, const ScoreComponents&) = default;
};

// A child's rows at the tier the score is computed for.
struct ChildRows {
    std::optional<PositionRow> position; // for its evaluation
    std::optional<NodeRow> node;         // absent: the child is off book at the tier
    std::vector<MoveRow> moves;          // the replies played from it at the tier
};

// The rows of a child position, at the tier of the moves score_moves is given.
using ChildLookup = std::function<ChildRows(Key child)>;

// The §7.2 loss of a move from parent to child, in centipawns from the mover's side (the side to
// move in parent's FEN); nullopt unless both are evaluated.
[[nodiscard]] std::optional<std::int64_t> evaluation_loss(const PositionRow& parent,
                                                          const std::optional<PositionRow>& child);

// The components of every move in moves, the position's moves rows at one tier, sorted by score
// descending and then by move text; children's rows come from lookup at the same tier.
[[nodiscard]] std::vector<ScoreComponents> score_moves(const PositionRow& position,
                                                       const std::vector<MoveRow>& moves,
                                                       const ChildLookup& lookup);

// The same, reading the rows from the store: empty when the position has no positions row or no
// moves at the tier.
[[nodiscard]] std::vector<ScoreComponents> score_moves(const OpeningStore& store, Key position,
                                                       Tier tier);

} // namespace chesscoach

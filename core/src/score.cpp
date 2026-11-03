// The recommendation score (chesscoach/score.hpp). Integer arithmetic wherever the formula allows;
// each floating-point value is one operation in its own statement, so no compiler contracts two of
// them into a fused multiply-add and the results are the same on every platform.
#include <chesscoach/score.hpp>

#include <algorithm>
#include <cstddef>

namespace chesscoach {

namespace {

constexpr std::int64_t mate_value = 100000;

bool evaluated(const PositionRow& row) {
    return row.eval_depth.has_value() && (row.eval_cp.has_value() || row.eval_mate.has_value());
}

// §7.2's value of an evaluated position, in centipawns from white's side.
std::int64_t value_of(const PositionRow& row) {
    if (row.eval_mate) {
        const std::int64_t mate = *row.eval_mate;
        const std::int64_t magnitude = mate_value - (mate < 0 ? -mate : mate);
        return mate < 0 ? -magnitude : magnitude;
    }
    return row.eval_cp.value_or(0);
}

// The side to move in the row's FEN, from its second field: black for "b", white otherwise.
bool white_to_move(const PositionRow& row) {
    const std::size_t space = row.fen.find(' ');
    return space == std::string::npos || space + 1 >= row.fen.size() ||
           row.fen.at(space + 1) != 'b';
}

int count_replies(const ChildRows& child) {
    if (!child.node) {
        return 0;
    }
    std::vector<std::int64_t> totals;
    totals.reserve(child.moves.size());
    std::int64_t sum = 0;
    for (const MoveRow& reply : child.moves) {
        totals.push_back(reply.total());
        sum += reply.total();
    }
    if (sum == 0) {
        return 0;
    }
    std::ranges::sort(totals, std::greater<>{});
    std::int64_t covered = 0;
    int replies = 0;
    for (const std::int64_t total : totals) {
        covered += total;
        ++replies;
        if (100 * covered >= coverage_percent * sum) {
            break;
        }
    }
    return replies;
}

} // namespace

std::optional<std::int64_t> evaluation_loss(const PositionRow& parent,
                                            const std::optional<PositionRow>& child) {
    if (!evaluated(parent) || !child || !evaluated(*child)) {
        return std::nullopt;
    }
    const std::int64_t side = white_to_move(parent) ? 1 : -1;
    const std::int64_t before = side * value_of(parent);
    const std::int64_t after = side * value_of(*child);
    return std::max<std::int64_t>(0, before - after);
}

std::vector<ScoreComponents> score_moves(const PositionRow& position,
                                         const std::vector<MoveRow>& moves,
                                         const ChildLookup& lookup) {
    std::int64_t sum = 0;
    for (const MoveRow& move : moves) {
        sum += move.total();
    }
    const bool white = white_to_move(position);
    std::vector<ScoreComponents> scored;
    scored.reserve(moves.size());
    for (const MoveRow& move : moves) {
        const ChildRows child = lookup(move.child);
        ScoreComponents components;
        components.move = move.move;
        components.total = move.total();
        components.loss = evaluation_loss(position, child.position);
        components.sound = !components.loss || *components.loss <= soundness_margin_cp;
        // (wins + draws / 2 + shrink_games / 2) / (total + shrink_games), in whole numbers until
        // the one division.
        const std::int64_t wins = white ? move.white : move.black;
        const std::int64_t numerator = (2 * wins) + move.draws + shrink_games;
        const std::int64_t denominator = (2 * components.total) + (2 * shrink_games);
        components.expected_score =
            static_cast<double>(numerator) / static_cast<double>(denominator);
        components.child_in_book = child.node.has_value();
        components.replies_needed = count_replies(child);
        const double beyond = static_cast<double>(std::max(0, components.replies_needed - 1));
        const double uncapped = penalty_per_reply * beyond;
        components.penalty = std::min(uncapped, penalty_cap);
        const double net = components.expected_score - components.penalty;
        components.score = components.sound ? std::max(0.0, net) : 0.0;
        components.share =
            sum > 0 ? static_cast<double>(components.total) / static_cast<double>(sum) : 0.0;
        scored.push_back(std::move(components));
    }
    std::ranges::sort(scored, [](const ScoreComponents& a, const ScoreComponents& b) {
        return a.score != b.score ? a.score > b.score : a.move < b.move;
    });
    return scored;
}

std::vector<ScoreComponents> score_moves(const OpeningStore& store, Key position, Tier tier) {
    const auto row = store.position(position);
    if (!row) {
        return {};
    }
    const std::vector<MoveRow> moves = store.moves(position, tier);
    return score_moves(*row, moves, [&](Key child) {
        return ChildRows{
            .position = store.position(child),
            .node = store.node(child, tier),
            .moves = store.moves(child, tier),
        };
    });
}

} // namespace chesscoach

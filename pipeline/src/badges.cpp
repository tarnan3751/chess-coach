// The §7 badges (badges.hpp).
#include "badges.hpp"

#include <chesscoach/badges.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/score.hpp>
#include <chesscoach/tier.hpp>

#include <numeric>
#include <span>

namespace chesscoach::pipeline {

namespace {

std::uint64_t move_sum(const SurvivingNode& node) {
    return std::accumulate(
        node.moves.begin(), node.moves.end(), std::uint64_t{0},
        [](std::uint64_t sum, const MoveCounts& move) { return sum + move.results.total(); });
}

bool share_at_least(std::uint64_t total, std::uint64_t sum, std::uint64_t percent) {
    return sum > 0 && 100 * total >= percent * sum;
}

bool white_to_move(const PositionRecord& position) {
    const std::size_t space = position.fen.find(' ');
    return space != std::string::npos && space + 1 < position.fen.size() &&
           position.fen.at(space + 1) == 'w';
}

} // namespace

std::optional<std::int64_t> evaluation_loss(const PositionRecord& parent,
                                            const PositionRecord* child) {
    if (!parent.evaluation || child == nullptr || !child->evaluation) {
        return std::nullopt;
    }
    const auto value = [](const Evaluation& evaluation) {
        return chesscoach::evaluation_value(evaluation.cp, evaluation.mate);
    };
    return chesscoach::evaluation_loss(white_to_move(parent) ? Color::white : Color::black,
                                       value(*parent.evaluation), value(*child->evaluation));
}

void compute_badges(Tree& tree) {
    const auto top = static_cast<std::uint8_t>(tier_index(top_tier));
    std::size_t group = 0;
    while (group < tree.nodes.size()) {
        // The nodes of one key, one per tier it survives at.
        std::size_t end = group;
        while (end < tree.nodes.size() &&
               tree.nodes.at(end).id.key == tree.nodes.at(group).id.key) {
            ++end;
        }
        const std::span<SurvivingNode> nodes(tree.nodes.begin() +
                                                 static_cast<std::ptrdiff_t>(group),
                                             tree.nodes.begin() + static_cast<std::ptrdiff_t>(end));
        const SurvivingNode* top_node = nullptr;
        for (const SurvivingNode& node : nodes) {
            top_node = node.id.tier == top ? &node : top_node;
        }
        const std::uint64_t top_sum = top_node != nullptr ? move_sum(*top_node) : 0;
        const PositionRecord* parent = tree.find(nodes.front().id.key);
        for (SurvivingNode& node : nodes) {
            const std::uint64_t sum = move_sum(node);
            for (MoveCounts& move : node.moves) {
                Badges badges;
                const bool popular =
                    share_at_least(move.results.total(), sum, popular_share_percent);
                std::uint64_t top_total = 0;
                if (top_node != nullptr) {
                    for (const MoveCounts& top_move : top_node->moves) {
                        top_total =
                            top_move.move == move.move ? top_move.results.total() : top_total;
                    }
                }
                const auto loss = parent != nullptr
                                      ? evaluation_loss(*parent, tree.find(move.child))
                                      : std::nullopt;
                // GCC's -Wmaybe-uninitialized misreads *loss at -O3, so the optional is read once.
                const bool loss_defined = loss.has_value();
                const std::int64_t loss_cp = loss.value_or(0);
                if (top_node != nullptr && share_at_least(top_total, top_sum, book_share_percent) &&
                    loss_defined && loss_cp <= book_margin_cp) {
                    badges.set(Badge::book);
                }
                if (popular) {
                    badges.set(Badge::popular);
                }
                if (popular && loss_defined && loss_cp >= dubious_margin_cp) {
                    badges.set(Badge::dubious);
                }
                move.badges = static_cast<std::uint8_t>(badges.raw());
            }
        }
        group = end;
    }
}

} // namespace chesscoach::pipeline

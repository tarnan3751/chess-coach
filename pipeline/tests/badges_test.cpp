// Unit tests for the §7 badges on hand-built trees (docs/database.md §7): the share thresholds
// at their boundaries, Book's tier-6 reading and its zero-sum case, the loss from either side
// with mates, and moves with a missing evaluation (pipeline; unit).
#include "badges.hpp"

#include <chesscoach/badges.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using chesscoach::Key;
using chesscoach::pipeline::compute_badges;
using chesscoach::pipeline::Evaluation;
using chesscoach::pipeline::evaluation_loss;
using chesscoach::pipeline::MoveCounts;
using chesscoach::pipeline::NodeId;
using chesscoach::pipeline::PositionRecord;
using chesscoach::pipeline::Results;
using chesscoach::pipeline::SurvivingNode;
using chesscoach::pipeline::Tree;

namespace {

constexpr std::uint8_t book = 1;
constexpr std::uint8_t popular = 2;
constexpr std::uint8_t dubious = 4;

constexpr Key parent_key = 1;

Evaluation cp(std::int64_t value) {
    return Evaluation{.cp = value, .mate = std::nullopt, .depth = 30, .best_move = "e2e4"};
}

Evaluation mate(std::int64_t moves) {
    return Evaluation{.cp = std::nullopt, .mate = moves, .depth = 30, .best_move = "e2e4"};
}

MoveCounts move(std::uint16_t code, Key child, std::uint32_t games) {
    return MoveCounts{.move = code, .child = child, .results = Results{.white = games}};
}

// A tree with one parent, white to move unless black is, and its children 2, 3, 4, ...
Tree tree_with(std::optional<Evaluation> parent_eval,
               const std::vector<std::optional<Evaluation>>& children,
               std::vector<SurvivingNode> nodes, bool black_to_move = false) {
    Tree tree;
    tree.nodes = std::move(nodes);
    tree.positions.push_back(PositionRecord{
        .key = parent_key,
        .fen = black_to_move ? "8/8/8/8/8/8/8/8 b - -" : "8/8/8/8/8/8/8/8 w - -",
        .evaluation = std::move(parent_eval),
        .eco = std::nullopt,
        .name = std::nullopt,
    });
    for (std::size_t i = 0; i < children.size(); ++i) {
        tree.positions.push_back(PositionRecord{
            .key = parent_key + 1 + i,
            .fen = "8/8/8/8/8/8/8/8 b - -",
            .evaluation = children.at(i),
            .eco = std::nullopt,
            .name = std::nullopt,
        });
    }
    for (std::size_t i = 0; i < tree.positions.size(); ++i) {
        tree.by_key.emplace(tree.positions.at(i).key, i);
    }
    return tree;
}

SurvivingNode node(std::uint8_t tier, std::vector<MoveCounts> moves) {
    std::uint32_t games = 0;
    for (const auto& m : moves) {
        games += static_cast<std::uint32_t>(m.results.total());
    }
    return SurvivingNode{
        .id = NodeId{.key = parent_key, .tier = tier},
        .visits = Results{.white = games},
        .moves = std::move(moves),
    };
}

} // namespace

TEST_CASE("evaluation_loss reads both evaluations from the mover's side", "[badges]") {
    auto white = tree_with(cp(40), {cp(-20)}, {});
    CHECK(evaluation_loss(white.positions.at(0), &white.positions.at(1)) == 60);
    auto gain = tree_with(cp(40), {cp(90)}, {});
    CHECK(evaluation_loss(gain.positions.at(0), &gain.positions.at(1)) == 0);
    auto black = tree_with(cp(40), {cp(150)}, {}, true);
    CHECK(evaluation_loss(black.positions.at(0), &black.positions.at(1)) == 110);
    auto mated = tree_with(mate(2), {cp(0)}, {});
    CHECK(evaluation_loss(mated.positions.at(0), &mated.positions.at(1)) == 99998);
    auto missing = tree_with(std::nullopt, {cp(0)}, {});
    CHECK_FALSE(evaluation_loss(missing.positions.at(0), &missing.positions.at(1)).has_value());
    CHECK_FALSE(evaluation_loss(white.positions.at(0), nullptr).has_value());
}

TEST_CASE("Popular is a share of at least 10% at the tier, exactly at the boundary", "[badges]") {
    // Moves of 10 and 90 games: 10% exactly is Popular; 9 of 91 is not.
    auto tree = tree_with(std::nullopt, {std::nullopt, std::nullopt},
                          {node(2, {move(1, 2, 10), move(2, 3, 90)})});
    compute_badges(tree);
    CHECK(tree.nodes.at(0).moves.at(0).badges == popular);
    CHECK(tree.nodes.at(0).moves.at(1).badges == popular);
    auto below = tree_with(std::nullopt, {std::nullopt, std::nullopt},
                           {node(2, {move(1, 2, 9), move(2, 3, 82)})});
    compute_badges(below);
    CHECK(below.nodes.at(0).moves.at(0).badges == 0);
}

TEST_CASE("Book reads the top tier's share and the loss, at every tier", "[badges]") {
    // Tier 6: the move has 5 of 100 games, a 5% share; the loss is 50 cp.
    auto tree = tree_with(
        cp(30), {cp(-20), cp(0)},
        {node(2, {move(1, 2, 3), move(2, 3, 97)}), node(6, {move(1, 2, 5), move(2, 3, 95)})});
    compute_badges(tree);
    CHECK(tree.nodes.at(0).moves.at(0).badges == book);             // 3% at tier 2: not Popular
    CHECK(tree.nodes.at(0).moves.at(1).badges == (book | popular)); // loss 30
    CHECK(tree.nodes.at(1).moves.at(0).badges == book);
    // A loss of 51 is not Book.
    auto lossy = tree_with(cp(30), {cp(-21), cp(0)}, {node(6, {move(1, 2, 5), move(2, 3, 95)})});
    compute_badges(lossy);
    CHECK(lossy.nodes.at(0).moves.at(0).badges == 0);
}

TEST_CASE("Book needs a tier-6 node whose moves sum to more than zero", "[badges]") {
    // The parent survives at tier 6 as a leaf at the cap: its moves sum to zero.
    auto tree = tree_with(cp(30), {cp(30)}, {node(2, {move(1, 2, 50)}), node(6, {})});
    compute_badges(tree);
    CHECK(tree.nodes.at(0).moves.at(0).badges == popular);
    // No tier-6 node at all.
    auto none = tree_with(cp(30), {cp(30)}, {node(2, {move(1, 2, 50)})});
    compute_badges(none);
    CHECK(none.nodes.at(0).moves.at(0).badges == popular);
}

TEST_CASE("Dubious is Popular with a loss of at least 100 cp", "[badges]") {
    auto tree = tree_with(cp(50), {cp(-50), cp(-49)}, {node(2, {move(1, 2, 50), move(2, 3, 50)})});
    compute_badges(tree);
    CHECK(tree.nodes.at(0).moves.at(0).badges == (popular | dubious)); // loss 100
    CHECK(tree.nodes.at(0).moves.at(1).badges == popular);             // loss 99
    // Without an evaluation on either side: neither Book nor Dubious.
    auto missing =
        tree_with(cp(50), {std::nullopt}, {node(2, {move(1, 2, 50)}), node(6, {move(1, 2, 50)})});
    compute_badges(missing);
    CHECK(missing.nodes.at(0).moves.at(0).badges == popular);
}

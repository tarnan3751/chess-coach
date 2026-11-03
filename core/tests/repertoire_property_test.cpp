// Property tests for chesscoach/repertoire.hpp (core; property): on stores built from random games
// with random weights, and random repertoires that answer a position with a stored move, with a
// legal move the store does not have, with an illegal move or not at all, the covered mass and
// the gaps' masses add up to the whole within 1e-9, covered stays within [0, 1], every gap is a
// position where the repertoire's colour is to move, reached by its line, met with a positive
// mass, and listed in order, and the same inputs give the same coverage.
#include <chesscoach/repertoire.hpp>

#include "random_games.hpp"

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

using chesscoach::ChildRows;
using chesscoach::Color;
using chesscoach::Coverage;
using chesscoach::CoverageConfig;
using chesscoach::Key;
using chesscoach::Position;
using chesscoach::Repertoire;
using chesscoach::Tier;
using chesscoach::testing::chosen_move;
using chesscoach::testing::move_choices;

namespace {

constexpr Tier tier = Tier::r1400_1599;

// A store at one tier and the position of each of its keys.
struct Tree {
    std::map<Key, ChildRows> rows;
    std::map<Key, Position> positions;
};

// The games' positions and moves, each game counted weight times; a game's last position has a
// node and no moves of its own unless another game continues from it.
Tree build(const std::vector<std::pair<std::vector<std::size_t>, std::int64_t>>& games) {
    Tree tree;
    for (const auto& [choices, weight] : games) {
        auto position = Position::start();
        for (std::size_t ply = 0;; ++ply) {
            const Key key = position.key();
            tree.positions.emplace(key, position);
            auto& rows = tree.rows[key];
            if (!rows.node) {
                rows.node = chesscoach::NodeRow{.key = key, .tier = tier};
            }
            rows.node->games += weight;
            rows.node->white += weight;
            const auto uci =
                ply < choices.size() ? chosen_move(position, choices.at(ply)) : std::string();
            if (uci.empty()) {
                break;
            }
            const auto played = position.play_uci(uci);
            RC_ASSERT(played.has_value());
            auto found = std::ranges::find(rows.moves, uci, &chesscoach::MoveRow::move);
            if (found == rows.moves.end()) {
                rows.moves.push_back({
                    .key = key,
                    .tier = tier,
                    .move = uci,
                    .child = played->child,
                    .white = 0,
                    .draws = 0,
                    .black = 0,
                    .badges = chesscoach::Badges{},
                });
                found = std::prev(rows.moves.end());
            }
            found->white += weight;
        }
    }
    return tree;
}

// For each position of the tree where the colour is to move, by key: 0 no answer, 1 the first
// stored move, 2 the first legal move, 3 an illegal move.
Repertoire answer(const Tree& tree, Color colour, const std::vector<int>& kinds) {
    Repertoire repertoire;
    std::size_t next = 0;
    for (const auto& [key, position] : tree.positions) {
        if (position.side_to_move() != colour) {
            continue;
        }
        const int kind = kinds.empty() ? 0 : kinds.at(next++ % kinds.size());
        const auto& moves = tree.rows.at(key).moves;
        if (kind == 1 && !moves.empty()) {
            repertoire.emplace(key, std::ranges::min(moves, {}, &chesscoach::MoveRow::move).move);
        } else if (kind == 2 && !position.legal_moves_uci().empty()) {
            repertoire.emplace(key, position.legal_moves_uci().front());
        } else if (kind == 3) {
            repertoire.emplace(key, "a1a1");
        }
    }
    return repertoire;
}

void check(const Coverage& coverage, Color colour) {
    double gap_mass = 0.0;
    for (std::size_t i = 0; i < coverage.gaps.size(); ++i) {
        const auto& gap = coverage.gaps.at(i);
        RC_ASSERT(gap.mass > 0.0);
        gap_mass += gap.mass;
        const auto parsed = Position::from_fen(gap.fen);
        RC_ASSERT(parsed.has_value());
        RC_ASSERT(parsed->key() == gap.key);
        RC_ASSERT(parsed->side_to_move() == colour);
        auto replayed = Position::start();
        for (const auto& uci : gap.line) {
            RC_ASSERT(replayed.play_uci(uci).has_value());
        }
        RC_ASSERT(replayed.key() == gap.key);
        if (i > 0) {
            const auto& previous = coverage.gaps.at(i - 1);
            RC_ASSERT(previous.mass > gap.mass ||
                      (previous.mass == gap.mass && previous.line < gap.line));
        }
    }
    RC_ASSERT(coverage.covered >= 0.0);
    RC_ASSERT(coverage.covered <= 1.0 + 1e-9);
    RC_ASSERT(coverage.total == 1.0);
    RC_ASSERT(std::abs(coverage.covered + gap_mass - coverage.total) <= 1e-9);
    RC_ASSERT(coverage.positions_visited >= 1);
}

} // namespace

TEST_CASE("Covered and gaps add up to the whole", "[repertoire][property]") {
    REQUIRE(rc::check("covered + gaps = total", [] {
        const auto games =
            *rc::gen::container<std::vector<std::pair<std::vector<std::size_t>, std::int64_t>>>(
                rc::gen::pair(move_choices(10, 4), rc::gen::inRange<std::int64_t>(1, 20)));
        const auto colour = *rc::gen::element(Color::white, Color::black);
        const auto kinds = *rc::gen::container<std::vector<int>>(rc::gen::inRange(0, 4));
        const auto max_plies = *rc::gen::inRange(0, 13);
        const auto tree = build(games);
        const auto repertoire = answer(tree, colour, kinds);
        const chesscoach::ChildLookup lookup = [&](Key key) {
            const auto found = tree.rows.find(key);
            return found == tree.rows.end() ? ChildRows{} : found->second;
        };
        for (const double min_mass : {0.0, 0.001, 0.05}) {
            const CoverageConfig config{.max_plies = max_plies, .min_mass = min_mass};
            const auto coverage = chesscoach::compute_coverage(repertoire, colour, lookup, config);
            check(coverage, colour);
            RC_ASSERT(chesscoach::compute_coverage(repertoire, colour, lookup, config) == coverage);
        }
    }));
}

// Unit tests for chesscoach/score.hpp (core; unit): hand-built rows whose expected numbers are
// worked out by hand in the comments: the shrink toward one half from either side, the 80%
// coverage count at its boundary, the soundness gate with a defined, an undefined and a mate
// loss, the penalty and its cap, off-book and move-less children, the floor at zero, the shares
// and the sort.
#include <chesscoach/score.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

using Catch::Approx;
using chesscoach::ChildRows;
using chesscoach::evaluation_loss;
using chesscoach::Key;
using chesscoach::MoveRow;
using chesscoach::NodeRow;
using chesscoach::PositionRow;
using chesscoach::score_moves;
using chesscoach::ScoreComponents;
using chesscoach::Tier;

namespace {

constexpr Tier tier = Tier::r1600_1799;

PositionRow position(bool white_to_move, std::optional<std::int64_t> cp = std::nullopt,
                     std::optional<std::int64_t> mate = std::nullopt) {
    const bool evaluated = cp.has_value() || mate.has_value();
    return PositionRow{
        .key = 1,
        .fen = white_to_move ? "4k3/8/8/8/8/8/8/4K3 w - -" : "4k3/8/8/8/8/8/8/4K3 b - -",
        .eco = std::nullopt,
        .name = std::nullopt,
        .eval_cp = cp,
        .eval_mate = mate,
        .eval_depth = evaluated ? std::optional<std::int64_t>(30) : std::nullopt,
        .best_move = evaluated ? std::optional<std::string>("e1e2") : std::nullopt,
    };
}

MoveRow move(std::string uci, Key child, std::int64_t white, std::int64_t draws,
             std::int64_t black) {
    return MoveRow{
        .key = 1,
        .tier = tier,
        .move = std::move(uci),
        .child = child,
        .white = white,
        .draws = draws,
        .black = black,
        .badges = chesscoach::Badges{},
    };
}

// A child in book at the tier with replies of these totals, and an optional evaluation.
ChildRows in_book(const std::vector<std::int64_t>& replies,
                  std::optional<std::int64_t> cp = std::nullopt) {
    ChildRows rows;
    if (cp) {
        rows.position = position(false, cp);
    }
    rows.node = NodeRow{.key = 2, .tier = tier, .games = 1, .white = 1, .draws = 0, .black = 0};
    char file = 'a';
    for (const std::int64_t total : replies) {
        rows.moves.push_back(move(std::string{file, '7', file, '6'}, 3, total, 0, 0));
        ++file;
    }
    return rows;
}

// Scores the moves with each child's rows taken from children, by child key.
std::vector<ScoreComponents> score(const PositionRow& parent, const std::vector<MoveRow>& moves,
                                   const std::map<Key, ChildRows>& children) {
    return score_moves(parent, moves, [&](Key child) {
        const auto found = children.find(child);
        return found == children.end() ? ChildRows{} : found->second;
    });
}

const ScoreComponents& only(const std::vector<ScoreComponents>& scored) {
    REQUIRE(scored.size() == 1);
    return scored.front();
}

} // namespace

TEST_CASE("The expected score is shrunk toward one half, thirty games weighing half", "[score]") {
    // 20 wins, 10 losses for white: (40 + 0 + 30) / (60 + 60) = 70 / 120, halfway from 2/3.
    const auto white = only(score(position(true), {move("e2e4", 10, 20, 0, 10)}, {}));
    CHECK(white.expected_score == Approx(70.0 / 120.0));
    CHECK(white.total == 30);
    // The same games with black to move: black's 10 wins, (20 + 0 + 30) / 120.
    const auto black = only(score(position(false), {move("e7e5", 10, 20, 0, 10)}, {}));
    CHECK(black.expected_score == Approx(50.0 / 120.0));
    // Draws count half: (0 + 30 + 30) / 120 = 0.5; one game barely moves it: (2 + 30) / 62.
    CHECK(only(score(position(true), {move("e2e4", 10, 0, 30, 0)}, {})).expected_score ==
          Approx(0.5));
    CHECK(only(score(position(true), {move("e2e4", 10, 1, 0, 0)}, {})).expected_score ==
          Approx(32.0 / 62.0));
}

TEST_CASE("replies_needed counts the most-played replies up to 80% of the child's", "[score]") {
    const std::vector<MoveRow> moves{move("e2e4", 10, 30, 0, 0)};
    // 50 + 30 of 100 reach 80% exactly: two replies, penalty 0.02.
    const auto two = only(score(position(true), moves, {{10, in_book({30, 50, 5, 15})}}));
    CHECK(two.replies_needed == 2);
    CHECK(two.penalty == Approx(0.02));
    CHECK(two.child_in_book);
    // 50 + 29 of 100 fall short: three replies, penalty 0.04.
    const auto three = only(score(position(true), moves, {{10, in_book({50, 29, 21})}}));
    CHECK(three.replies_needed == 3);
    CHECK(three.penalty == Approx(0.04));
    // One reply covering everything: no penalty.
    const auto one = only(score(position(true), moves, {{10, in_book({40})}}));
    CHECK(one.replies_needed == 1);
    CHECK(one.penalty == 0.0);
}

TEST_CASE("An off-book child, and an in-book child without moves, need no replies", "[score]") {
    const std::vector<MoveRow> moves{move("e2e4", 10, 30, 0, 0)};
    const auto off = only(score(position(true), moves, {}));
    CHECK_FALSE(off.child_in_book);
    CHECK(off.replies_needed == 0);
    CHECK(off.penalty == 0.0);
    CHECK(off.score == off.expected_score);
    const auto leaf = only(score(position(true), moves, {{10, in_book({})}}));
    CHECK(leaf.child_in_book);
    CHECK(leaf.replies_needed == 0);
    CHECK(leaf.penalty == 0.0);
}

TEST_CASE("The penalty is 0.02 per reply beyond the first, capped at 0.3", "[score]") {
    const std::vector<MoveRow> moves{move("e2e4", 10, 300, 0, 0)};
    // Twenty equal replies: sixteen reach 80%, 15 beyond the first, 0.3 exactly the cap.
    const auto sixteen =
        only(score(position(true), moves, {{10, in_book(std::vector<std::int64_t>(20, 1))}}));
    CHECK(sixteen.replies_needed == 16);
    CHECK(sixteen.penalty == Approx(0.3));
    // Twenty-five: twenty reach 80%, 0.38 capped at 0.3.
    const auto twenty =
        only(score(position(true), moves, {{10, in_book(std::vector<std::int64_t>(25, 1))}}));
    CHECK(twenty.replies_needed == 20);
    CHECK(twenty.penalty == 0.3);
    // Expected (600 + 30) / 660 less 0.3.
    CHECK(twenty.score == Approx((630.0 / 660.0) - 0.3));
}

TEST_CASE("The score is floored at zero", "[score]") {
    // 100 losses: (0 + 0 + 30) / 260 = 0.115, less the capped 0.3.
    const auto lost = only(score(position(true), {move("e2e4", 10, 0, 0, 100)},
                                 {{10, in_book(std::vector<std::int64_t>(25, 1))}}));
    CHECK(lost.expected_score == Approx(30.0 / 260.0));
    CHECK(lost.score == 0.0);
}

TEST_CASE("An unsound move scores zero and lists its loss", "[score]") {
    const PositionRow parent = position(true, 30);
    // Child at -40 from white's side: loss 70, over the 50 cp margin.
    const auto unsound =
        only(score(parent, {move("e2e4", 10, 30, 0, 0)}, {{10, in_book({1}, -40)}}));
    CHECK_FALSE(unsound.sound);
    CHECK(unsound.loss == 70);
    CHECK(unsound.score == 0.0);
    // Child at -20: loss 50, at the margin, sound.
    const auto margin =
        only(score(parent, {move("e2e4", 10, 30, 0, 0)}, {{10, in_book({1}, -20)}}));
    CHECK(margin.sound);
    CHECK(margin.loss == 50);
    CHECK(margin.score == Approx(margin.expected_score));
    // A child that gains: loss 0.
    CHECK(only(score(parent, {move("e2e4", 10, 30, 0, 0)}, {{10, in_book({1}, 90)}})).loss == 0);
    // No evaluation of the child: the loss is undefined and the move sound.
    const auto undefined = only(score(parent, {move("e2e4", 10, 30, 0, 0)}, {{10, in_book({1})}}));
    CHECK(undefined.sound);
    CHECK_FALSE(undefined.loss.has_value());
}

TEST_CASE("evaluation_loss reads both evaluations from the mover's side, mates included",
          "[score]") {
    // Black to move at 40 for white; the child at 150 for white: black lost 110.
    CHECK(evaluation_loss(position(false, 40), position(true, 150)) == 110);
    // White mates in 2 (99998) and the child is level: 99998.
    CHECK(evaluation_loss(position(true, std::nullopt, 2), position(false, 0)) == 99998);
    // Black is mated in 3 after the move: from black's side -99997 against 0.
    CHECK(evaluation_loss(position(false, 0), position(true, std::nullopt, 3)) == 99997);
    CHECK_FALSE(evaluation_loss(position(true), position(false, 0)).has_value());
    CHECK_FALSE(evaluation_loss(position(true, 0), std::nullopt).has_value());
}

TEST_CASE("Moves are sorted by score, then by move text, with their shares", "[score]") {
    // b and a have the same games and children, so the same score: a first. c scores more; d
    // is unsound and last.
    const PositionRow parent = position(true, 0);
    const std::vector<MoveRow> moves{
        move("b2b3", 20, 10, 0, 10),
        move("a2a3", 20, 10, 0, 10),
        move("c2c3", 30, 40, 0, 0),
        move("d2d3", 40, 30, 0, 0),
    };
    const auto scored =
        score(parent, moves, {{20, in_book({1})}, {30, in_book({1})}, {40, in_book({1}, -100)}});
    REQUIRE(scored.size() == 4);
    CHECK(scored.at(0).move == "c2c3");
    CHECK(scored.at(1).move == "a2a3");
    CHECK(scored.at(2).move == "b2b3");
    CHECK(scored.at(3).move == "d2d3");
    CHECK(scored.at(0).share == Approx(40.0 / 110.0));
    CHECK(scored.at(1).share == Approx(20.0 / 110.0));
    CHECK(scored.at(3).share == Approx(30.0 / 110.0));
    CHECK(score(parent, {}, {}).empty());
}

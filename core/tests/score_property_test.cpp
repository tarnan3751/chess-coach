// Property tests for chesscoach/score.hpp (core; property): over generated positions, moves,
// evaluations and children, every score lies between 0 and 1, an unsound move scores 0, the
// expected score lies strictly between 0 and 1, a sound score is max(0, expected - penalty), the
// penalty never passes its cap, the shares sum to one, and the order is by score then move text.
#include <chesscoach/score.hpp>

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

using chesscoach::ChildRows;
using chesscoach::Key;
using chesscoach::MoveRow;
using chesscoach::NodeRow;
using chesscoach::PositionRow;
using chesscoach::Tier;

namespace {

constexpr Tier tier = Tier::r1400_1599;

PositionRow row(Key key, bool white_to_move, std::optional<std::int64_t> cp,
                std::optional<std::int64_t> mate) {
    const bool evaluated = cp.has_value() || mate.has_value();
    return PositionRow{
        .key = key,
        .fen = white_to_move ? "4k3/8/8/8/8/8/8/4K3 w - -" : "4k3/8/8/8/8/8/8/4K3 b - -",
        .eco = std::nullopt,
        .name = std::nullopt,
        .eval_cp = cp,
        .eval_mate = mate,
        .eval_depth = evaluated ? std::optional<std::int64_t>(20) : std::nullopt,
        .best_move = evaluated ? std::optional<std::string>("e1e2") : std::nullopt,
    };
}

// An optional evaluation: none, a centipawn score, or a mate.
rc::Gen<std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>> evaluation() {
    return rc::gen::oneOf(
        rc::gen::just(std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>{}),
        rc::gen::map(rc::gen::inRange<std::int64_t>(-2000, 2001),
                     [](std::int64_t cp) {
                         return std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>{
                             cp, std::nullopt};
                     }),
        rc::gen::map(rc::gen::nonZero<std::int8_t>(), [](std::int8_t mate) {
            return std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>{
                std::nullopt, std::int64_t{mate}};
        }));
}

} // namespace

TEST_CASE("Scores lie between 0 and 1, unsound moves score 0, and the components add up",
          "[score][property]") {
    REQUIRE(rc::check("score components", [] {
        const bool white = *rc::gen::arbitrary<bool>();
        const auto [parent_cp, parent_mate] = *evaluation();
        const PositionRow parent = row(1, white, parent_cp, parent_mate);
        const auto count = *rc::gen::inRange<std::size_t>(1, 8);
        std::vector<MoveRow> moves;
        std::map<Key, ChildRows> children;
        for (std::size_t i = 0; i < count; ++i) {
            const Key child = 100 + i;
            const auto white_wins = *rc::gen::inRange<std::int64_t>(0, 500);
            const auto draws = *rc::gen::inRange<std::int64_t>(0, 500);
            const auto black_wins =
                *rc::gen::inRange<std::int64_t>(white_wins + draws == 0 ? 1 : 0, 500);
            moves.push_back(MoveRow{
                .key = 1,
                .tier = tier,
                .move =
                    std::string{static_cast<char>('a' + i), '2', static_cast<char>('a' + i), '3'},
                .child = child,
                .white = white_wins,
                .draws = draws,
                .black = black_wins,
                .badges = chesscoach::Badges{},
            });
            ChildRows rows;
            const auto [child_cp, child_mate] = *evaluation();
            rows.position = row(child, !white, child_cp, child_mate);
            if (*rc::gen::arbitrary<bool>()) {
                rows.node = NodeRow{
                    .key = child, .tier = tier, .games = 1, .white = 1, .draws = 0, .black = 0};
                const auto replies = *rc::gen::container<std::vector<std::int64_t>>(
                    rc::gen::inRange<std::int64_t>(1, 300));
                for (const std::int64_t total : replies) {
                    rows.moves.push_back(MoveRow{.key = child,
                                                 .tier = tier,
                                                 .move = "a7a6",
                                                 .child = 0,
                                                 .white = total,
                                                 .draws = 0,
                                                 .black = 0,
                                                 .badges = chesscoach::Badges{}});
                }
            }
            children.emplace(child, std::move(rows));
        }
        const auto scored =
            chesscoach::score_moves(parent, moves, [&](Key child) { return children.at(child); });
        RC_ASSERT(scored.size() == moves.size());
        double shares = 0.0;
        for (const auto& c : scored) {
            RC_ASSERT(c.score >= 0.0);
            RC_ASSERT(c.score <= 1.0);
            RC_ASSERT(c.expected_score > 0.0);
            RC_ASSERT(c.expected_score < 1.0);
            RC_ASSERT(c.penalty >= 0.0);
            RC_ASSERT(c.penalty <= chesscoach::penalty_cap);
            RC_ASSERT(c.sound == (!c.loss || *c.loss <= chesscoach::soundness_margin_cp));
            if (c.sound) {
                const double net = c.expected_score - c.penalty;
                RC_ASSERT(c.score == (net > 0.0 ? net : 0.0));
            } else {
                RC_ASSERT(c.score == 0.0);
            }
            RC_ASSERT(c.child_in_book || c.replies_needed == 0);
            shares += c.share;
        }
        RC_ASSERT(std::abs(shares - 1.0) < 1e-9);
        for (std::size_t i = 1; i < scored.size(); ++i) {
            const auto& a = scored.at(i - 1);
            const auto& b = scored.at(i);
            RC_ASSERT(a.score > b.score || (a.score == b.score && a.move < b.move));
        }
    }));
}

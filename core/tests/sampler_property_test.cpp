// Property tests for chesscoach/sampler.hpp (core; property): over generated moves and seeds, a
// draw is always one of the candidates, and adding moves below min_games never changes it.
#include <chesscoach/sampler.hpp>

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using chesscoach::MoveRow;
using chesscoach::sample_reply;
using chesscoach::sampler_candidates;
using chesscoach::SamplerConfig;
using chesscoach::Splitmix64;

namespace {

MoveRow row(std::size_t index, std::int64_t total) {
    return MoveRow{
        .key = 1,
        .tier = chesscoach::Tier::r1400_1599,
        .move = "m" + std::to_string(index),
        .child = 2,
        .white = total,
        .draws = 0,
        .black = 0,
        .badges = chesscoach::Badges{},
    };
}

} // namespace

TEST_CASE("A draw is a candidate and ignores the moves below min_games", "[sampler][property]") {
    REQUIRE(rc::check("draws", [] {
        const auto min_games = *rc::gen::inRange<std::int64_t>(1, 50);
        const auto totals = *rc::gen::nonEmpty(rc::gen::container<std::vector<std::int64_t>>(
            rc::gen::inRange<std::int64_t>(min_games, 1000)));
        const auto small = *rc::gen::container<std::vector<std::int64_t>>(
            rc::gen::inRange<std::int64_t>(0, min_games));
        const auto seed = *rc::gen::arbitrary<std::uint64_t>();
        std::vector<MoveRow> moves;
        moves.reserve(totals.size());
        for (std::size_t i = 0; i < totals.size(); ++i) {
            moves.push_back(row(i, totals.at(i)));
        }
        std::vector<MoveRow> with_small = moves;
        for (std::size_t i = 0; i < small.size(); ++i) {
            with_small.push_back(row(1000 + i, small.at(i)));
        }
        const SamplerConfig config{.min_games = min_games};
        const auto candidates = sampler_candidates(moves, config);
        Splitmix64 generator(seed);
        Splitmix64 again(seed);
        const auto drawn = sample_reply(candidates, generator);
        RC_ASSERT(drawn.has_value());
        RC_ASSERT(std::ranges::any_of(candidates, [&](const auto& c) { return c.move == *drawn; }));
        RC_ASSERT(sample_reply(sampler_candidates(with_small, config), again) == drawn);
    }));
}

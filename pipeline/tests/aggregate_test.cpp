// Unit tests for pass two's counts (aggregate.hpp; pipeline; unit): a counter that would pass
// 2^32 - 1 stops there and reports it, in Results, in a move's counts and in the aggregate, which
// run_build turns into an error value.
#include "aggregate.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using chesscoach::pipeline::Aggregate;
using chesscoach::pipeline::GameResult;
using chesscoach::pipeline::NodeCounts;
using chesscoach::pipeline::NodeId;
using chesscoach::pipeline::Results;

namespace {

constexpr std::uint32_t max = std::numeric_limits<std::uint32_t>::max();

} // namespace

TEST_CASE("Results stop at 2^32 - 1 and report it", "[aggregate]") {
    Results results{.white = max - 1, .draws = 0, .black = 0};
    CHECK(results.add(GameResult::white));
    CHECK(results.white == max);
    CHECK_FALSE(results.add(GameResult::white));
    CHECK(results.white == max);
    CHECK(results.add(GameResult::draw));
    Results more{.white = 0, .draws = max, .black = 0};
    CHECK_FALSE(results.add(more));
    CHECK(results.draws == max);
}

TEST_CASE("A move's counts and the aggregate report an overflow", "[aggregate]") {
    NodeCounts node;
    CHECK(node.add_play(1, 2, Results{.white = max, .draws = 0, .black = 0}));
    CHECK_FALSE(node.add_play(1, 2, Results{.white = 1, .draws = 0, .black = 0}));

    Aggregate aggregate;
    CHECK_FALSE(aggregate.overflowed());
    const NodeId id{.key = 7, .tier = 3};
    NodeCounts full;
    full.visits = Results{.white = max, .draws = 0, .black = 0};
    aggregate.merge(id, full);
    CHECK_FALSE(aggregate.overflowed());
    aggregate.merge(id, full);
    CHECK(aggregate.overflowed());
}

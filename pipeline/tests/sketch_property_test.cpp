// Property test for VisitSketch: on any stream of (tier, key) visits, at any size and
// saturation, estimate >= min(true count, saturation) for every pair, the guarantee
// docs/reference/pipeline.md states (pipeline; property).
#include "sketch.hpp"

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

using chesscoach::pipeline::VisitSketch;

TEST_CASE("VisitSketch never underestimates below its saturation", "[sketch][property]") {
    REQUIRE(rc::check("estimate >= min(true, saturation)", [] {
        // Small sketches force collisions; a small key range forces repeats.
        const auto log_bytes = *rc::gen::inRange<unsigned>(6, 12);
        const auto saturation = *rc::gen::inRange<std::uint16_t>(1, 50);
        const auto visits =
            *rc::gen::container<std::vector<std::pair<std::uint8_t, std::uint64_t>>>(
                rc::gen::pair(rc::gen::inRange<std::uint8_t>(0, 7),
                              rc::gen::oneOf(rc::gen::inRange<std::uint64_t>(0, 40),
                                             rc::gen::arbitrary<std::uint64_t>())));
        VisitSketch sketch(std::size_t{1} << log_bytes, saturation);
        std::map<std::pair<std::uint8_t, std::uint64_t>, std::uint64_t> truth;
        for (const auto& [tier, key] : visits) {
            sketch.add(tier, key);
            ++truth[{tier, key}];
        }
        for (const auto& [pair, count] : truth) {
            const auto floor = std::min<std::uint64_t>(count, saturation);
            RC_ASSERT(std::uint64_t{sketch.estimate(pair.first, pair.second)} >= floor);
            RC_ASSERT(sketch.estimate(pair.first, pair.second) <= saturation);
        }
    }));
}

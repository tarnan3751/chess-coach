// Property test for VisitSketch: on any stream of (tier, key) visits, at any size and
// saturation, on both sides of 255 so that both counter widths are drawn, estimate >= min(true
// count, saturation) and estimate <= saturation for every pair, the guarantee
// docs/reference/pipeline.md states (pipeline; property).
#include "sketch.hpp"

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <random>
#include <utility>
#include <vector>

using chesscoach::pipeline::VisitSketch;

TEST_CASE("VisitSketch never underestimates below its saturation", "[sketch][property]") {
    REQUIRE(rc::check("estimate >= min(true, saturation), at both counter widths", [] {
        // Small sketches force collisions; a small key range forces repeated pairs.
        const auto log_bytes = *rc::gen::inRange<unsigned>(6, 12);
        const auto saturation = *rc::gen::oneOf(rc::gen::inRange<std::uint16_t>(1, 256),
                                                rc::gen::inRange<std::uint16_t>(256, 1000));
        const auto pairs = *rc::gen::container<std::vector<std::pair<std::uint8_t, std::uint64_t>>>(
            rc::gen::pair(rc::gen::inRange<std::uint8_t>(0, 7),
                          rc::gen::oneOf(rc::gen::inRange<std::uint64_t>(0, 40),
                                         rc::gen::arbitrary<std::uint64_t>())));
        // Each pair visited up to past the saturation, the visits in a generated order.
        const auto repeats = *rc::gen::container<std::vector<std::uint16_t>>(
            pairs.size(),
            rc::gen::inRange<std::uint16_t>(1, static_cast<std::uint16_t>(saturation + 20U)));
        const auto order = *rc::gen::arbitrary<std::uint64_t>();
        std::vector<std::pair<std::uint8_t, std::uint64_t>> visits;
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            visits.insert(visits.end(), repeats.at(i), pairs.at(i));
        }
        std::mt19937_64 shuffler(order);
        std::ranges::shuffle(visits, shuffler);

        auto created = VisitSketch::create(std::size_t{1} << log_bytes, saturation);
        RC_ASSERT(created.has_value());
        VisitSketch& sketch = *created;
        RC_ASSERT(sketch.counter_bytes() == (saturation <= 255 ? 1U : 2U));
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

// Unit tests for VisitSketch: counts below the saturation are exact on a sparse sketch, counters
// stop at the saturation, tiers are separate, concurrent adds lose nothing, and the default size
// (pipeline; unit).
#include "sketch.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

using chesscoach::pipeline::default_sketch_bytes;
using chesscoach::pipeline::VisitSketch;

TEST_CASE("VisitSketch counts a few keys exactly and saturates", "[sketch]") {
    VisitSketch sketch(std::size_t{1} << 20U, 100);
    CHECK(sketch.width() == (std::size_t{1} << 20U) / 8);
    CHECK(sketch.bytes() == std::size_t{1} << 20U);
    for (int i = 0; i < 7; ++i) {
        sketch.add(2, 0x1234567890ABCDEFU);
    }
    for (int i = 0; i < 250; ++i) {
        sketch.add(6, 42);
    }
    CHECK(sketch.estimate(2, 0x1234567890ABCDEFU) == 7);
    CHECK(sketch.estimate(6, 42) == 100);
    CHECK(sketch.estimate(3, 0x1234567890ABCDEFU) == 0);
    CHECK(sketch.estimate(2, 43) == 0);
}

TEST_CASE("VisitSketch loses no add under concurrency", "[sketch]") {
    VisitSketch sketch(std::size_t{1} << 20U, 60000);
    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&sketch] {
            for (int i = 0; i < 10000; ++i) {
                sketch.add(1, 7);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    CHECK(sketch.estimate(1, 7) == 40000);
}

TEST_CASE("VisitSketch refuses a size that is not a power of two", "[sketch]") {
    CHECK_THROWS_AS(VisitSketch(3000, 100), std::invalid_argument);
    CHECK_THROWS_AS(VisitSketch(16, 100), std::invalid_argument);
    CHECK_THROWS_AS(VisitSketch(1024, 0), std::invalid_argument);
}

TEST_CASE("default_sketch_bytes is twice the input as a power of two, within its bounds",
          "[sketch]") {
    CHECK(default_sketch_bytes(0) == std::size_t{1} << 20U);
    CHECK(default_sketch_bytes(100000) == std::size_t{1} << 20U);
    CHECK(default_sketch_bytes(17761302) == std::size_t{1} << 26U);
    CHECK(default_sketch_bytes(std::uint64_t{30} << 30U) == std::size_t{1} << 30U);
}

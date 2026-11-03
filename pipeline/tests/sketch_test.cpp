// Unit tests for VisitSketch: counts below the saturation are exact on a sparse sketch, counters
// stop at the saturation, tiers are separate, the counters are 8-bit up to a saturation of 255
// and 16-bit above it, concurrent adds lose nothing at either width, and the default size
// (pipeline; unit).
#include "sketch.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using chesscoach::pipeline::default_sketch_bytes;
using chesscoach::pipeline::VisitSketch;

TEST_CASE("VisitSketch counts a few keys exactly and saturates", "[sketch]") {
    auto created = VisitSketch::create(std::size_t{1} << 20U, 100);
    REQUIRE(created.has_value());
    VisitSketch& sketch = *created;
    CHECK(sketch.counter_bytes() == 1);
    CHECK(sketch.width() == (std::size_t{1} << 20U) / 4);
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

TEST_CASE("VisitSketch counts in 8 bits up to 255 and in 16 bits above", "[sketch]") {
    auto narrow = VisitSketch::create(std::size_t{1} << 20U, 255);
    auto wide = VisitSketch::create(std::size_t{1} << 20U, 256);
    REQUIRE(narrow.has_value());
    REQUIRE(wide.has_value());
    CHECK(narrow->counter_bytes() == 1);
    CHECK(narrow->width() == (std::size_t{1} << 20U) / 4);
    CHECK(wide->counter_bytes() == 2);
    CHECK(wide->width() == (std::size_t{1} << 20U) / 8);
    CHECK(wide->bytes() == std::size_t{1} << 20U);
    for (int i = 0; i < 300; ++i) {
        narrow->add(4, 99);
        wide->add(4, 99);
    }
    CHECK(narrow->estimate(4, 99) == 255);
    CHECK(wide->estimate(4, 99) == 256);
    auto smallest = VisitSketch::create(64, 100);
    REQUIRE(smallest.has_value());
    CHECK(smallest->width() == 16);
}

TEST_CASE("VisitSketch loses no add under concurrency", "[sketch]") {
    // Four threads adding one (tier, key): every add counts below the saturation, at either
    // width, and the count stops at the saturation past it.
    struct Case {
        std::uint16_t saturation;
        int per_thread;
        std::uint16_t expected;
    };
    for (const Case& test : {
             Case{.saturation = 60000, .per_thread = 10000, .expected = 40000},
             Case{.saturation = 255, .per_thread = 60, .expected = 240},
             Case{.saturation = 255, .per_thread = 100, .expected = 255},
         }) {
        auto created = VisitSketch::create(std::size_t{1} << 20U, test.saturation);
        REQUIRE(created.has_value());
        VisitSketch& sketch = *created;
        std::vector<std::thread> threads;
        threads.reserve(4);
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&sketch, &test] {
                for (int i = 0; i < test.per_thread; ++i) {
                    sketch.add(1, 7);
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        CHECK(sketch.estimate(1, 7) == test.expected);
    }
}

TEST_CASE("VisitSketch::create refuses a size it cannot take, as an error value", "[sketch]") {
    CHECK_FALSE(VisitSketch::create(3000, 100).has_value());
    CHECK_FALSE(VisitSketch::create(16, 100).has_value());
    CHECK_FALSE(VisitSketch::create(1024, 0).has_value());
    CHECK_FALSE(VisitSketch::create(chesscoach::pipeline::max_sketch_bytes * 2, 100).has_value());
    CHECK(VisitSketch::create(3000, 100).error().find("power of two") != std::string::npos);
}

TEST_CASE("default_sketch_bytes is twice the input as a power of two, within its bounds",
          "[sketch]") {
    CHECK(default_sketch_bytes(0) == std::size_t{1} << 20U);
    CHECK(default_sketch_bytes(100000) == std::size_t{1} << 20U);
    CHECK(default_sketch_bytes(17761302) == std::size_t{1} << 26U);
    CHECK(default_sketch_bytes(std::uint64_t{30} << 30U) == std::size_t{1} << 30U);
}

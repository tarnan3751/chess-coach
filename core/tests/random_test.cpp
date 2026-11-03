// Unit tests for chesscoach/random.hpp (core; unit): Splitmix64 from the contract's seed gives the
// Zobrist table (docs/database.md §5.2) value for value, its first output is the pinned one, and
// two generators with one seed give one sequence.
#include <chesscoach/key.hpp>
#include <chesscoach/random.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using chesscoach::Splitmix64;

TEST_CASE("Splitmix64 from the contract's seed gives the Zobrist table", "[random]") {
    Splitmix64 generator(0x434845535320434FULL);
    for (const chesscoach::Key value : chesscoach::zobrist::table) {
        CHECK(generator.next() == value);
    }
    CHECK(Splitmix64(0x434845535320434FULL).next() == 0xF777EAA02D011CB2ULL);
}

TEST_CASE("Splitmix64 repeats its sequence for a seed and differs across seeds", "[random]") {
    Splitmix64 a(20261103);
    Splitmix64 b(20261103);
    Splitmix64 c(20261104);
    bool differs = false;
    for (int i = 0; i < 100; ++i) {
        const std::uint64_t value = a.next();
        CHECK(b.next() == value);
        differs = differs || c.next() != value;
    }
    CHECK(differs);
    // The step of §5.2 from state 0: the first output of seed 0.
    CHECK(Splitmix64(0).next() == 0xE220A8397B1DCDAFULL);
}

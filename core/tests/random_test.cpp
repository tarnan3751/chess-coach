// Unit tests for chesscoach/random.hpp (core; unit): the reference output of splitmix64's step
// from seed 0, and two generators with one seed give one sequence. That the Zobrist tables, which
// key.hpp now draws from Splitmix64, are unchanged is key_test's to show: its pinned table values
// and keys were computed by independent implementations of docs/database.md §5.2.
#include <chesscoach/random.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using chesscoach::Splitmix64;

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

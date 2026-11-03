// Splitmix64: the generator docs/database.md names twice, for the Zobrist tables (§5.2, seeded
// 0x434845535320434F) and for the sampler's draws (§13, seeded with the session's seed). One
// definition, constexpr so the tables are still computed at compile time; uint64_t arithmetic
// throughout, wrapping modulo 2^64. Pure.
#pragma once

#include <cstdint>

namespace chesscoach {

class Splitmix64 {
  public:
    constexpr explicit Splitmix64(std::uint64_t seed) noexcept : state_(seed) {}

    // §5.2's step: state += 0x9E3779B97F4A7C15, then the output mix of the new state.
    constexpr std::uint64_t next() noexcept {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

  private:
    std::uint64_t state_;
};

} // namespace chesscoach

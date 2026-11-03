// Tiers (docs/database.md §4): the seven rating bands every statistic, badge, sample and
// recommendation is computed for, the rule that puts a game in one, and their labels. The
// enum's values are the indices the tier columns store; nothing falls back from one tier to
// another. Pure.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace chesscoach {

enum class Tier : std::uint8_t {
    under_1200 = 0, // r < 1200
    r1200_1399 = 1,
    r1400_1599 = 2,
    r1600_1799 = 3,
    r1800_1999 = 4,
    r2000_2199 = 5,
    r2200_up = 6, // r >= 2200
};

inline constexpr int tier_count = 7;

// The tier the Book badge reads (§7.3).
inline constexpr Tier top_tier = Tier::r2200_up;

// The lower bounds of tiers 1 to 6: tier_of's boundaries, and the value of meta's tiers key.
inline constexpr std::array<int, tier_count - 1> tier_lower_bounds{
    1200, 1400, 1600, 1800, 2000, 2200,
};

// The tier of a game from its two ratings: r = floor((white_elo + black_elo) / 2), summed in
// 64 bits so that no pair of ints overflows.
[[nodiscard]] constexpr Tier tier_of(int white_elo, int black_elo) noexcept {
    const std::int64_t sum = std::int64_t{white_elo} + std::int64_t{black_elo};
    const std::int64_t average = sum >= 0 ? sum / 2 : -((1 - sum) / 2);
    std::uint8_t tier = 0;
    for (const int bound : tier_lower_bounds) {
        if (average >= bound) {
            ++tier;
        }
    }
    return static_cast<Tier>(tier);
}

// The index the tier columns store, 0 to 6.
[[nodiscard]] constexpr int tier_index(Tier tier) noexcept { return static_cast<int>(tier); }

// The tier of a stored index, or nullopt when the index names none.
[[nodiscard]] constexpr std::optional<Tier> tier_from_index(std::int64_t index) noexcept {
    if (index < 0 || index >= tier_count) {
        return std::nullopt;
    }
    return static_cast<Tier>(index);
}

// The tier's band as text: "<1200", "1200-1399", ..., "2000-2199", "2200+".
[[nodiscard]] std::string_view tier_label(Tier tier) noexcept;

} // namespace chesscoach

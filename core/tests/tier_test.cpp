// Unit tests for chesscoach/tier.hpp (core; unit): every boundary of docs/database.md §4's table,
// the floor of an odd sum, the stored index both ways, and the labels.
#include <chesscoach/tier.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using chesscoach::Tier;
using chesscoach::tier_of;

TEST_CASE("tier_of puts each average rating in its band", "[tier]") {
    CHECK(tier_of(0, 0) == Tier::under_1200);
    CHECK(tier_of(1199, 1199) == Tier::under_1200);
    CHECK(tier_of(1200, 1200) == Tier::r1200_1399);
    CHECK(tier_of(1399, 1399) == Tier::r1200_1399);
    CHECK(tier_of(1400, 1400) == Tier::r1400_1599);
    CHECK(tier_of(1599, 1599) == Tier::r1400_1599);
    CHECK(tier_of(1600, 1600) == Tier::r1600_1799);
    CHECK(tier_of(1799, 1799) == Tier::r1600_1799);
    CHECK(tier_of(1800, 1800) == Tier::r1800_1999);
    CHECK(tier_of(1999, 1999) == Tier::r1800_1999);
    CHECK(tier_of(2000, 2000) == Tier::r2000_2199);
    CHECK(tier_of(2199, 2199) == Tier::r2000_2199);
    CHECK(tier_of(2200, 2200) == Tier::r2200_up);
    CHECK(tier_of(3400, 3400) == Tier::r2200_up);
}

TEST_CASE("tier_of averages the two ratings and floors an odd sum", "[tier]") {
    // (1199 + 1200) / 2 = 1199.5, floored to 1199.
    CHECK(tier_of(1199, 1200) == Tier::under_1200);
    CHECK(tier_of(1200, 1199) == Tier::under_1200);
    CHECK(tier_of(1199, 1201) == Tier::r1200_1399);
    CHECK(tier_of(2199, 2200) == Tier::r2000_2199);
    CHECK(tier_of(2200, 2201) == Tier::r2200_up);
    CHECK(tier_of(800, 2400) == Tier::r1600_1799);
    CHECK(tier_of(2400, 1000) == Tier::r1600_1799);
    CHECK(tier_of(std::numeric_limits<int>::max(), std::numeric_limits<int>::max()) ==
          Tier::r2200_up);
    CHECK(tier_of(std::numeric_limits<int>::min(), std::numeric_limits<int>::min()) ==
          Tier::under_1200);
}

TEST_CASE("A tier's index is the stored value, and back", "[tier]") {
    CHECK(chesscoach::tier_count == 7);
    CHECK(chesscoach::top_tier == Tier::r2200_up);
    for (int index = 0; index < chesscoach::tier_count; ++index) {
        const auto tier = static_cast<Tier>(index);
        CHECK(chesscoach::tier_from_index(index) == tier);
        CHECK(chesscoach::tier_index(tier) == index);
    }
    CHECK(chesscoach::tier_index(Tier::under_1200) == 0);
    CHECK(chesscoach::tier_index(Tier::r2200_up) == 6);
    CHECK_FALSE(chesscoach::tier_from_index(-1).has_value());
    CHECK_FALSE(chesscoach::tier_from_index(7).has_value());
    CHECK_FALSE(chesscoach::tier_from_index(std::numeric_limits<std::int64_t>::max()).has_value());
}

TEST_CASE("Each tier has its band as a label", "[tier]") {
    CHECK(chesscoach::tier_label(Tier::under_1200) == "<1200");
    CHECK(chesscoach::tier_label(Tier::r1200_1399) == "1200-1399");
    CHECK(chesscoach::tier_label(Tier::r1400_1599) == "1400-1599");
    CHECK(chesscoach::tier_label(Tier::r1600_1799) == "1600-1799");
    CHECK(chesscoach::tier_label(Tier::r1800_1999) == "1800-1999");
    CHECK(chesscoach::tier_label(Tier::r2000_2199) == "2000-2199");
    CHECK(chesscoach::tier_label(Tier::r2200_up) == "2200+");
}

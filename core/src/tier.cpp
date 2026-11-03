// The labels of the tiers (chesscoach/tier.hpp; docs/database.md §4).
#include <chesscoach/tier.hpp>

#include <string_view>

namespace chesscoach {

std::string_view tier_label(Tier tier) noexcept {
    switch (tier) {
    case Tier::under_1200:
        return "<1200";
    case Tier::r1200_1399:
        return "1200-1399";
    case Tier::r1400_1599:
        return "1400-1599";
    case Tier::r1600_1799:
        return "1600-1799";
    case Tier::r1800_1999:
        return "1800-1999";
    case Tier::r2000_2199:
        return "2000-2199";
    case Tier::r2200_up:
        return "2200+";
    }
    return "";
}

} // namespace chesscoach

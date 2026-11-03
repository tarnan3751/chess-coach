// The badges of a move as the moves.badges column stores them (docs/database.md §7.3): a bit set
// of Book (1), Popular (2) and Dubious (4). Decoding only: the thresholds and the rule are the
// pipeline's, applied once when the database is built, and the app reads the result. Pure.
#pragma once

#include <cstdint>

namespace chesscoach {

enum class Badge : std::uint8_t { book = 1, popular = 2, dubious = 4 };

class Badges {
  public:
    constexpr Badges() noexcept = default;

    // The column's value as stored; any other bit is kept, so raw() gives the value back.
    constexpr explicit Badges(std::uint8_t raw) noexcept : bits_(raw) {}

    [[nodiscard]] constexpr bool has(Badge badge) const noexcept {
        return (bits_ & static_cast<std::uint8_t>(badge)) != 0;
    }

    constexpr void set(Badge badge) noexcept {
        bits_ = static_cast<std::uint8_t>(bits_ | static_cast<std::uint8_t>(badge));
    }

    [[nodiscard]] constexpr std::uint8_t raw() const noexcept { return bits_; }

    friend constexpr bool operator==(Badges, Badges) noexcept = default;

  private:
    std::uint8_t bits_ = 0;
};

} // namespace chesscoach

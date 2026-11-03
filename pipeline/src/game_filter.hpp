// The admission checks of docs/database.md §3 over a game's headers, before any move is parsed,
// and the skip counters they feed. Pure: no I/O. The first failing check names the reason, so
// the counters partition the skipped games; check 7 (moves) belongs to the scanner, which
// replays the moves, and check 1 (malformed) to the PGN parser.
#pragma once

#include <tl/expected.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace chesscoach::pipeline {

// §3's counters, in check order.
enum class SkipReason : std::uint8_t { malformed, unrated, speed, variant, rating, result, moves };

inline constexpr std::size_t skip_reason_count = 7;

inline constexpr std::array<SkipReason, skip_reason_count> all_skip_reasons{
    SkipReason::malformed, SkipReason::unrated, SkipReason::speed, SkipReason::variant,
    SkipReason::rating,    SkipReason::result,  SkipReason::moves,
};

// The counter's name in §3 and in meta's skipped_<name> keys.
[[nodiscard]] std::string_view name(SkipReason reason);

enum class GameResult : std::uint8_t { white, draw, black };

// The headers the checks read, as a game's header callbacks deliver them.
class GameHeaders {
  public:
    void clear();
    // Records one header; tags the checks do not read are ignored.
    void set(std::string_view tag, std::string_view value);

    std::optional<std::string> event;
    std::optional<std::string> variant;
    std::optional<std::string> white_elo;
    std::optional<std::string> black_elo;
    std::optional<std::string> result;
    bool setup_or_fen = false;
};

struct AdmittedGame {
    int white_elo = 0;
    int black_elo = 0;
    GameResult result = GameResult::draw;
};

// Checks 2 to 6 of §3, in order: the game's ratings and result when it is admitted by its
// headers, else the first failing check. A rating is a decimal integer of at most nine digits.
[[nodiscard]] tl::expected<AdmittedGame, SkipReason> check_headers(const GameHeaders& headers);

// Skip counts per reason, indexed by SkipReason.
class SkipCounts {
  public:
    void add(SkipReason reason, std::uint64_t count = 1) {
        counts_.at(static_cast<std::size_t>(reason)) += count;
    }
    [[nodiscard]] std::uint64_t count(SkipReason reason) const {
        return counts_.at(static_cast<std::size_t>(reason));
    }
    [[nodiscard]] std::uint64_t total() const;
    SkipCounts& operator+=(const SkipCounts& other);
    friend bool operator==(const SkipCounts&, const SkipCounts&) = default;

  private:
    std::array<std::uint64_t, skip_reason_count> counts_{};
};

} // namespace chesscoach::pipeline

// chess-coach-cli drill: the cards of a profile's repertoire for one colour, through core's
// ProfileStore and scheduler (docs/reference/cli.md, **drill**): the cards due, a review graded
// again, hard, good or easy, and the repair that brings the cards in step with the repertoire.
#pragma once

#include <chesscoach/profile_store.hpp>

#include "common.hpp"

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>

namespace chesscoach::cli {

// The latest --at accepted: 9999-12-31 23:59:59 UTC, so the scheduler's due dates stay far from
// the limits of its 64-bit arithmetic.
inline constexpr std::int64_t latest_time = 253402300799;

struct DrillOptions {
    std::string data_dir;
    std::string profile = "Default";
    std::string colour;
    std::string action;             // list, review or sync
    std::int64_t card = 0;          // review
    std::string grade;              // review: again, hard, good or easy
    std::optional<std::int64_t> at; // list, review: the time, 0 to latest_time; the clock's without
};

// The clock stamps the cards sync makes, and is the time without --at.
[[nodiscard]] Result<void> run_drill(const DrillOptions& options, std::ostream& out,
                                     const Clock& clock = system_clock_seconds);

} // namespace chesscoach::cli

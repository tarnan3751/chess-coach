// chess-coach-cli spar: a sparring session against the coach at a tier, in the terminal
// (docs/reference/cli.md, **spar**): the user's moves from stdin or from --script, the coach's
// book replies from core's sampler and, once off book, from the engine at limited strength
// (core's EngineReplyProvider, started only when the game leaves the book), the interruptions with
// the better choices, and the summary; the session is saved under a profile in profiles.db. --list
// prints a profile's saved sessions and --replay re-runs one from its record. Plays only through
// core's SparSession, saves only through core's ProfileStore.
#pragma once

#include <chesscoach/profile_store.hpp>

#include "common.hpp"
#include "engine_common.hpp"

#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <string>

namespace chesscoach::cli {

struct SparOptions {
    std::string db;
    std::string data_dir;
    std::optional<int> tier;           // required for a new session
    std::optional<std::string> colour; // "white" or "black", required for a new session
    std::optional<std::uint64_t> seed; // the clock's seconds when absent
    int end_move = 25;
    std::string profile = "Default";
    EngineSettings engine;
    double seconds = 0.5;              // each engine reply's search time
    std::optional<std::string> script; // the user's moves, SAN or UCI, separated by spaces
    bool list = false;
    std::optional<std::int64_t> replay; // a saved session's id
};

// Runs the session (moves read from in unless a script is given), --list or --replay, writing to
// out. The clock stamps the session and gives the default seed.
[[nodiscard]] Result<void> run_spar(const SparOptions& options, std::istream& in, std::ostream& out,
                                    const Clock& clock = system_clock_seconds);

} // namespace chesscoach::cli

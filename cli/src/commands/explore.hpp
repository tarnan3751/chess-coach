// chess-coach-cli explore: a position at one tier, the moves played from it with their badges,
// results, share and whether they stay in book, and with --score the recommendation score's
// components (docs/reference/cli.md, **explore**).
#pragma once

#include "common.hpp"
#include "engine_common.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace chesscoach::cli {

// explore --engine: the engine cache of data_dir, and the search to run through it on a miss.
struct ExploreEngine {
    std::string data_dir;
    EngineQuery query{.lines = 1, .seconds = 1.0, .min_depth = default_min_depth};
    EngineSettings settings;
};

struct ExploreOptions {
    std::string db;
    int tier = 0;
    std::optional<std::string> fen;
    std::optional<std::string> moves;
    bool score = false; // append the recommendation score's components to each move line
    // --engine: an engine line after the eval line, through the engine cache.
    std::optional<ExploreEngine> engine = std::nullopt;
    // --data-dir: its openings.db is read when --db is not given.
    std::optional<std::string> data_dir = std::nullopt;
};

// At most one of fen and moves is set; neither is the start position. With engine, the engine's
// answer is had before anything is written to out.
[[nodiscard]] Result<void> run_explore(const ExploreOptions& options, std::ostream& out);

} // namespace chesscoach::cli

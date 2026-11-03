// chess-coach-cli eval: a position's engine lines, served from the profile store's engine cache
// when it holds them deep enough and otherwise searched by the engine and stored
// (docs/reference/cli.md, **eval**). With --db, or the data directory's openings.db when it has
// one, the stored evaluation of openings.db as well.
#pragma once

#include "common.hpp"
#include "engine_common.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace chesscoach::cli {

struct EvalOptions {
    std::string data_dir;
    std::optional<std::string> db;
    std::optional<std::string> fen;
    std::optional<std::string> moves;
    EngineQuery query{.lines = 3, .seconds = 1.0, .min_depth = default_min_depth};
    EngineSettings engine;
};

// At most one of fen and moves is set; neither is the start position. Writes nothing to out
// unless every step succeeds.
[[nodiscard]] Result<void> run_eval(const EvalOptions& options, std::ostream& out);

} // namespace chesscoach::cli

// chess-coach-cli explore: a position at one tier, the moves played from it with their badges,
// results, share and whether they stay in book (docs/reference/cli.md, **explore**).
#pragma once

#include "common.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace chesscoach::cli {

struct ExploreOptions {
    std::string db;
    int tier = 0;
    std::optional<std::string> fen;
    std::optional<std::string> moves;
};

// At most one of fen and moves is set; neither is the start position.
[[nodiscard]] Result<void> run_explore(const ExploreOptions& options, std::ostream& out);

} // namespace chesscoach::cli

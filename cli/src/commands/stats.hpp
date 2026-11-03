// chess-coach-cli stats: the database's meta, then per tier its nodes, its moves and the games
// at the start position, then the named and the evaluated positions (docs/reference/cli.md,
// **stats**).
#pragma once

#include "common.hpp"

#include <ostream>
#include <string>

namespace chesscoach::cli {

struct StatsOptions {
    std::string db; // empty when --db is not given
    // --data-dir: its openings.db is read when --db is not given.
    std::optional<std::string> data_dir = std::nullopt;
};

[[nodiscard]] Result<void> run_stats(const StatsOptions& options, std::ostream& out);

} // namespace chesscoach::cli

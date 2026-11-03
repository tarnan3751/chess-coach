// chess-coach-cli name: the ECO code and name of the deepest named position along --moves, or of
// the --fen position, or "unnamed" (docs/reference/cli.md, **name**).
#pragma once

#include "common.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace chesscoach::cli {

struct NameOptions {
    std::string db; // empty when --db is not given
    std::optional<std::string> fen;
    std::optional<std::string> moves;
    // --data-dir: its openings.db is read when --db is not given.
    std::optional<std::string> data_dir = std::nullopt;
};

// Exactly one of fen and moves must be set.
[[nodiscard]] Result<void> run_name(const NameOptions& options, std::ostream& out);

} // namespace chesscoach::cli

// chess-coach-cli name: the ECO code and name of the deepest named position along --moves, or of
// the --fen position, or "unnamed" (docs/reference/cli.md, **name**).
#pragma once

#include "common.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace chesscoach::cli {

struct NameOptions {
    std::string db;
    std::optional<std::string> fen;
    std::optional<std::string> moves;
};

// Exactly one of fen and moves must be set.
[[nodiscard]] Result<void> run_name(const NameOptions& options, std::ostream& out);

} // namespace chesscoach::cli

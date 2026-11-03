// chess-coach-cli dump: the canonical dump of docs/database.md §11, written by core's store, and
// nothing else (docs/reference/cli.md, **dump**).
#pragma once

#include "common.hpp"

#include <ostream>
#include <string>

namespace chesscoach::cli {

struct DumpOptions {
    std::string db; // empty when --db is not given
    // --data-dir: its openings.db is read when --db is not given.
    std::optional<std::string> data_dir = std::nullopt;
};

[[nodiscard]] Result<void> run_dump(const DumpOptions& options, std::ostream& out);

} // namespace chesscoach::cli

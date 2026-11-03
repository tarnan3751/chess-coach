// chess-coach-cli database: the opening database a data directory reads (docs/reference/cli.md,
// **database**): info, where the CLI finds it and what its meta records, and import, which puts a
// copy of a file in place through core's import_opening_database. Reads the file only through
// core's OpeningStore.
#pragma once

#include "common.hpp"

#include <ostream>
#include <string>

namespace chesscoach::cli {

struct DatabaseOptions {
    std::string action;   // info or import
    std::string data_dir; // --data-dir, empty when not given; import needs it
    std::string db;       // info: --db, empty when not given
    std::string file;     // import: the file to put in place
};

[[nodiscard]] Result<void> run_database(const DatabaseOptions& options, std::ostream& out);

} // namespace chesscoach::cli

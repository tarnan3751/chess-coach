// chess-coach-cli repertoire: a profile's repertoire for one colour, through core's ProfileStore
// (docs/reference/cli.md, **repertoire**): add a line's choices, remove one, list them, import a
// PGN, and its coverage and gaps at a tier on the opening database, as core's compute_coverage
// gives them.
#pragma once

#include <chesscoach/profile_store.hpp>

#include "common.hpp"

#include <ostream>
#include <string>

namespace chesscoach::cli {

struct RepertoireOptions {
    std::string data_dir;
    std::string profile = "Default";
    std::string colour;
    std::string db;     // coverage, gaps
    std::string action; // add, remove, list, coverage, gaps or import
    std::string moves;  // add, remove
    std::string pgn;    // import
    int tier = 0;       // coverage, gaps
    int depth = 20;     // coverage, gaps: the plies walked
    int limit = 10;     // coverage, gaps: the gaps listed
};

// The clock stamps the choices made and the cards they make.
[[nodiscard]] Result<void> run_repertoire(const RepertoireOptions& options, std::ostream& out,
                                          const Clock& clock = system_clock_seconds);

} // namespace chesscoach::cli

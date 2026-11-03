// run_build: one whole build of openings.db (docs/database.md; docs/reference/pipeline.md,
// **Passes**). Validates the inputs and the output path; pass one streams every input into the
// sketch; pass two streams them again and counts the candidates exactly; the two passes' game
// counts must agree; the candidates below min_games are pruned; the evaluation merge, the names
// and the badges fill the survivors in; the writer writes <out>.partial and renames it to <out>.
// A bad game is counted, never fatal; an unreadable input, a write failure or a refused option
// is an error value, and leaves no output behind. Progress goes to `progress`, the report to
// `report`.
#pragma once

#include "options.hpp"
#include "report.hpp"

#include <tl/expected.hpp>

#include <ostream>
#include <string>

namespace chesscoach::pipeline {

[[nodiscard]] tl::expected<void, std::string> run_build(const BuildOptions& options, Report& report,
                                                        std::ostream& progress);

} // namespace chesscoach::pipeline

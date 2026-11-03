// The pipeline's benchmark (docs/reference/pipeline.md, **Benchmark**): a whole build of the
// sample corpus at the golden knobs, in-process, timed, with the games per second and the peak
// resident size printed; it fails when the build takes longer than its budget, twice the time
// measured on the development machine (pipeline; bench, release preset only).
#include "build.hpp"
#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <chrono>
#include <sstream>

using chesscoach::pipeline::BuildOptions;
using chesscoach::pipeline::Report;
using chesscoach::pipeline::run_build;
namespace testing = chesscoach::pipeline::testing;

namespace {

// Twice the sample build's time on the development machine (docs/reference/pipeline.md).
constexpr double budget_seconds = 0.1;

} // namespace

TEST_CASE("The sample corpus builds within its budget", "[bench]") {
    const auto dir = testing::scratch_dir("bench");
    const std::filesystem::path sample = CHESSCOACH_SAMPLE_DIR;
    BuildOptions options;
    options.inputs = {sample / "lichess_db_standard_rated_2013-01.pgn.zst"};
    options.output = dir / "openings.db";
    options.eval = sample / "eval" / "lichess_db_eval.jsonl.zst";
    options.names = std::filesystem::path(CHESSCOACH_OPENINGS_DIR);
    options.min_games = 2;
    options.max_ply = 20;
    options.threads = 4;
    Report report;
    std::ostringstream progress;
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(run_build(options, report, progress).has_value());
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    fmt::print("pipeline bench: {} games in {:.3f} s, {} games/s, peak RSS {} bytes\n",
               report.value("games_read"), seconds, report.value("games_per_second"),
               report.value("peak_rss_bytes"));
    CHECK(seconds <= budget_seconds);
}

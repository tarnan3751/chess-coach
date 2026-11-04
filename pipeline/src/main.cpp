// chess-coach-pipeline: the entry point. `build` runs run_build (build.hpp) with the options
// docs/reference/pipeline.md, **Running it**, describes; `trim` runs run_trim (trim.hpp, and
// **Trim**); --version and --help as in Phase 0, and the help when run without arguments. Exit
// status 0 on success, 1 on a fatal error (an unreadable input, a refused option, a write
// failure), never because of a bad game.
#include <chesscoach/version.hpp>

#include "build.hpp"
#include "options.hpp"
#include "report.hpp"
#include "sketch.hpp"
#include "trim.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include <bit>
#include <charconv>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

int main(int argc, char** argv) {
    namespace pipeline = chesscoach::pipeline;
    try {
        CLI::App app{"Builds the tiered opening database from the Lichess monthly dumps.",
                     "chess-coach-pipeline"};
        app.set_version_flag("--version", "chess-coach-pipeline " +
                                              chesscoach::to_string(chesscoach::current_version()));
        argv = app.ensure_utf8(argv);

        pipeline::BuildOptions options;
        options.threads =
            std::thread::hardware_concurrency() == 0 ? 1 : std::thread::hardware_concurrency();
        std::string names = CHESSCOACH_OPENINGS_DIR;
        std::optional<std::filesystem::path> report_path;
        std::size_t sketch_mib = 0;

        CLI::App* build = app.add_subcommand("build", "Build openings.db from monthly dumps");
        build
            ->add_option("--in", options.inputs,
                         "Monthly dumps, lichess_db_standard_rated_YYYY-MM.pgn.zst")
            ->required()
            ->check(CLI::ExistingFile);
        build->add_option("--out", options.output, "The database to write; must not exist")
            ->required();
        build->add_option("--eval", options.eval, "The evaluation database, .jsonl.zst")
            ->check(CLI::ExistingFile);
        build->add_option("--names", names, "The chess-openings TSV directory, or none")
            ->capture_default_str();
        build->add_option("--min-games", options.min_games, "Visits a node needs to survive")
            ->check(CLI::Range(1U, 65535U))
            ->capture_default_str();
        build->add_option("--max-ply", options.max_ply, "The depth cap in plies")
            ->check(CLI::Range(0U, 1000U))
            ->capture_default_str();
        build->add_option("--threads", options.threads, "Worker threads")
            ->check(CLI::Range(std::size_t{1}, std::size_t{1024}))
            ->capture_default_str();
        build->add_option("--data-version", options.data_version, "meta's data_version")
            ->capture_default_str();
        build->add_option("--report", report_path, "Write the report here instead of stdout");
        build
            ->add_option("--sketch-mib", sketch_mib,
                         "The sketch's size in MiB, a power of two (default: from the inputs)")
            ->check(CLI::Validator(
                [](const std::string& text) {
                    std::size_t value = 0;
                    const auto [end, parsed] =
                        std::from_chars(text.data(), text.data() + text.size(), value);
                    const bool whole = parsed == std::errc{} && end == text.data() + text.size();
                    constexpr std::size_t max_mib = pipeline::max_sketch_bytes >> 20U;
                    return whole && value != 0 && value <= max_mib && std::has_single_bit(value)
                               ? std::string{}
                               : fmt::format("must be a power of two from 1 to {}", max_mib);
                },
                "POWER OF TWO"));

        pipeline::TrimOptions trim_options;
        CLI::App* trim = app.add_subcommand(
            "trim", "Reduce monthly dumps and the evaluation database to what build reads");
        trim->add_option("--out-dir", trim_options.out_dir,
                         "The directory to write into, under the inputs' names")
            ->required();
        trim->add_option("--max-ply", trim_options.max_ply, "The plies kept per game")
            ->check(CLI::Range(0U, 1000U))
            ->capture_default_str();
        trim->add_option("inputs", trim_options.inputs,
                         "lichess_db_standard_rated_YYYY-MM.pgn.zst or lichess_db_eval.jsonl.zst")
            ->required()
            ->check(CLI::ExistingFile);

        if (argc <= 1) {
            fmt::print("{}", app.help());
            return 0;
        }
        try {
            app.parse(argc, argv);
        } catch (const CLI::ParseError& error) {
            // --help and --version exit 0 as CLI11 prints them; every rejected command line
            // exits 1, the pipeline's one failure status.
            const int status = app.exit(error);
            return error.get_exit_code() == static_cast<int>(CLI::ExitCodes::Success) ? status : 1;
        }
        if (trim->parsed()) {
            const auto trimmed = pipeline::run_trim(trim_options, std::cout);
            if (!trimmed) {
                fmt::print(stderr, "chess-coach-pipeline: {}\n", trimmed.error());
                return 1;
            }
            return 0;
        }
        if (!build->parsed()) {
            fmt::print("{}", app.help());
            return 0;
        }
        if (names != "none") {
            options.names = names;
        }
        options.sketch_bytes = sketch_mib << 20U;

        pipeline::Report report;
        const auto built = pipeline::run_build(options, report, std::cerr);
        if (!built) {
            fmt::print(stderr, "chess-coach-pipeline: {}\n", built.error());
            return 1;
        }
        if (report_path) {
            std::ofstream out(*report_path);
            report.write(out);
            if (!out) {
                fmt::print(stderr, "chess-coach-pipeline: cannot write {}\n",
                           report_path->string());
                return 1;
            }
        } else {
            report.write(std::cout);
        }
        return 0;
    } catch (const std::exception& error) {
        fmt::print(stderr, "chess-coach-pipeline: {}\n", error.what());
        return 1;
    }
}

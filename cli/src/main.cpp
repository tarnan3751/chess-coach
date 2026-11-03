// chess-coach-cli: the entry point, wiring only (docs/reference/cli.md). It parses the command
// line with CLI11 and runs one subcommand: dump, stats, explore or name, each over --db <file>,
// or eval, over the profile store of --data-dir <dir>.
// --version, --help, and the help when run without arguments, exit 0. A command line CLI11
// rejects and an error a command returns each print one line on stderr and exit 2; an exception
// that reaches main is reported on stderr with exit status 1.
#include <chesscoach/version.hpp>

#include "commands/dump.hpp"
#include "commands/eval.hpp"
#include "commands/explore.hpp"
#include "commands/name.hpp"
#include "commands/stats.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include <cstdio>
#include <exception>
#include <iostream>
#include <string>

namespace {

constexpr int usage_error = 2;

int report(const chesscoach::cli::Result<void>& result) {
    if (!result) {
        fmt::print(stderr, "chess-coach-cli: {}\n", result.error().message);
        return usage_error;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        CLI::App app{"Queries the opening database and the engine from the terminal.",
                     "chess-coach-cli"};
        app.set_version_flag("--version", "chess-coach-cli " +
                                              chesscoach::to_string(chesscoach::current_version()));
        argv = app.ensure_utf8(argv);
        app.require_subcommand(1);

        chesscoach::cli::DumpOptions dump;
        CLI::App* dump_command =
            app.add_subcommand("dump", "Write the canonical dump of the database to stdout.");
        dump_command->add_option("--db", dump.db, "The opening database")->required();

        chesscoach::cli::StatsOptions stats;
        CLI::App* stats_command =
            app.add_subcommand("stats", "Print the database's meta and its counts per tier.");
        stats_command->add_option("--db", stats.db, "The opening database")->required();

        // --fen and --moves are read as text and set in the options only when given, so that
        // --moves "" is the start position rather than no position.
        std::string fen;
        std::string moves;

        chesscoach::cli::ExploreOptions explore;
        CLI::App* explore_command =
            app.add_subcommand("explore", "Print a position's moves at one tier.");
        explore_command->add_option("--db", explore.db, "The opening database")->required();
        explore_command->add_option("--tier", explore.tier, "The tier, 0 to 6")->required();
        CLI::Option* explore_fen =
            explore_command->add_option("--fen", fen, "The position as a FEN");
        const CLI::Option* explore_moves =
            explore_command
                ->add_option("--moves", moves,
                             "The moves from the start position, UCI or SAN, separated by spaces")
                ->excludes(explore_fen);
        explore_command->add_flag("--score", explore.score,
                                  "Append the recommendation score's components to each move");
        bool explore_engine = false;
        chesscoach::cli::ExploreEngine explore_engine_options;
        CLI::Option* explore_engine_flag = explore_command->add_flag(
            "--engine", explore_engine, "Add the engine's first line, through the engine cache");
        CLI::Option* explore_data_dir =
            explore_command->add_option("--data-dir", explore_engine_options.data_dir,
                                        "The data directory holding profiles.db (--engine)");
        explore_engine_flag->needs(explore_data_dir);
        explore_command->add_option("--lines", explore_engine_options.query.lines,
                                    "The engine's line count (--engine)");
        explore_command->add_option("--seconds", explore_engine_options.query.seconds,
                                    "The engine's search time (--engine)");
        std::string explore_stockfish;
        const CLI::Option* explore_stockfish_option = explore_command->add_option(
            "--stockfish", explore_stockfish, "The engine binary (--engine)");
        explore_command->add_option("--threads", explore_engine_options.settings.threads,
                                    "The engine's threads (--engine)");
        explore_command->add_option("--hash", explore_engine_options.settings.hash_mb,
                                    "The engine's hash in MB (--engine)");

        chesscoach::cli::EvalOptions eval;
        CLI::App* eval_command = app.add_subcommand(
            "eval", "Print a position's engine lines, from the engine cache or a new search.");
        eval_command
            ->add_option("--data-dir", eval.data_dir, "The data directory holding profiles.db")
            ->required();
        std::string eval_db;
        const CLI::Option* eval_db_option =
            eval_command->add_option("--db", eval_db, "The opening database, for its evaluation");
        CLI::Option* eval_fen = eval_command->add_option("--fen", fen, "The position as a FEN");
        const CLI::Option* eval_moves =
            eval_command
                ->add_option("--moves", moves,
                             "The moves from the start position, UCI or SAN, separated by spaces")
                ->excludes(eval_fen);
        eval_command->add_option("--lines", eval.query.lines, "The engine's line count")
            ->capture_default_str();
        eval_command->add_option("--seconds", eval.query.seconds, "The engine's search time")
            ->capture_default_str();
        eval_command
            ->add_option("--min-depth", eval.query.min_depth,
                         "The least depth a cached search needs to be served")
            ->capture_default_str();
        std::string eval_stockfish;
        const CLI::Option* eval_stockfish_option = eval_command->add_option(
            "--stockfish", eval_stockfish,
            "The engine binary (else CHESS_COACH_STOCKFISH, else stockfish on the PATH)");
        eval_command->add_option("--threads", eval.engine.threads, "The engine's threads")
            ->capture_default_str();
        eval_command->add_option("--hash", eval.engine.hash_mb, "The engine's hash in MB")
            ->capture_default_str();

        chesscoach::cli::NameOptions name;
        CLI::App* name_command = app.add_subcommand(
            "name", "Print the name of the deepest named position along the moves.");
        name_command->add_option("--db", name.db, "The opening database")->required();
        CLI::Option* name_fen = name_command->add_option("--fen", fen, "The position as a FEN");
        const CLI::Option* name_moves =
            name_command
                ->add_option("--moves", moves,
                             "The moves from the start position, UCI or SAN, separated by spaces")
                ->excludes(name_fen);

        if (argc <= 1) {
            fmt::print("{}", app.help());
            return 0;
        }
        try {
            app.parse(argc, argv);
        } catch (const CLI::ParseError& error) {
            if (error.get_exit_code() == static_cast<int>(CLI::ExitCodes::Success)) {
                return app.exit(error);
            }
            fmt::print(stderr, "chess-coach-cli: {}\n", error.what());
            return usage_error;
        }

        if (*dump_command) {
            return report(chesscoach::cli::run_dump(dump, std::cout));
        }
        if (*stats_command) {
            return report(chesscoach::cli::run_stats(stats, std::cout));
        }
        if (*explore_command) {
            if (*explore_fen) {
                explore.fen = fen;
            }
            if (*explore_moves) {
                explore.moves = moves;
            }
            if (*explore_stockfish_option) {
                explore_engine_options.settings.stockfish = explore_stockfish;
            }
            if (explore_engine) {
                explore.engine = explore_engine_options;
            }
            return report(chesscoach::cli::run_explore(explore, std::cout));
        }
        if (*eval_command) {
            if (*eval_fen) {
                eval.fen = fen;
            }
            if (*eval_moves) {
                eval.moves = moves;
            }
            if (*eval_db_option) {
                eval.db = eval_db;
            }
            if (*eval_stockfish_option) {
                eval.engine.stockfish = eval_stockfish;
            }
            return report(chesscoach::cli::run_eval(eval, std::cout));
        }
        if (*name_fen) {
            name.fen = fen;
        }
        if (*name_moves) {
            name.moves = moves;
        }
        return report(chesscoach::cli::run_name(name, std::cout));
    } catch (const std::exception& error) {
        fmt::print(stderr, "chess-coach-cli: {}\n", error.what());
        return 1;
    }
}

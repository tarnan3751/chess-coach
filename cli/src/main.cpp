// chess-coach-cli: the entry point, wiring only (docs/reference/cli.md). It parses the command
// line with CLI11 and runs one subcommand: dump, stats, explore or name, each over --db <file>;
// eval, spar, repertoire, drill or profile, over the profile store of --data-dir <dir>.
// --version, --help, and the help when run without arguments, exit 0. A command line CLI11
// rejects and an error a command returns each print one line on stderr and exit 2; an exception
// that reaches main is reported on stderr with exit status 1.
#include <chesscoach/engine.hpp>
#include <chesscoach/version.hpp>

#include "commands/drill.hpp"
#include "commands/dump.hpp"
#include "commands/eval.hpp"
#include "commands/explore.hpp"
#include "commands/name.hpp"
#include "commands/profiles.hpp"
#include "commands/repertoire.hpp"
#include "commands/spar.hpp"
#include "commands/stats.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

namespace {

constexpr int usage_error = 2;

// --seconds: above 0 and at most the longest search core's engine accepts.
std::string check_seconds(const std::string& text) {
    double value = 0.0;
    try {
        std::size_t used = 0;
        value = std::stod(text, &used);
        if (used != text.size()) {
            return "not a number of seconds";
        }
    } catch (const std::exception&) {
        return "not a number of seconds";
    }
    if (!(value > 0.0) || !(value <= chesscoach::maximum_search_seconds)) {
        return fmt::format("must be above 0 and at most {}", chesscoach::maximum_search_seconds);
    }
    return {};
}

// The spar subcommand with its database and data directory options.
CLI::App* spar_command_of(CLI::App& app, chesscoach::cli::SparOptions& spar) {
    CLI::App* command = app.add_subcommand(
        "spar", "Play a sparring session against the coach at a tier, or list or replay one.");
    command->add_option("--db", spar.db, "The opening database (not needed for --list)");
    command->add_option("--data-dir", spar.data_dir, "The data directory holding profiles.db")
        ->required();
    return command;
}

// A subcommand of repertoire, drill or profile, which also takes its parent's options after it.
CLI::App* action_of(CLI::App& command, const std::string& name, const std::string& description) {
    CLI::App* action = command.add_subcommand(name, description);
    action->fallthrough();
    return action;
}

// The name of the subcommand parsed under the command.
std::string parsed_action(const CLI::App& command) {
    const auto parsed = command.get_subcommands();
    return parsed.empty() ? std::string() : parsed.front()->get_name();
}

// The options of coverage and gaps.
void tier_options_of(CLI::App& action, chesscoach::cli::RepertoireOptions& options) {
    action.add_option("--tier", options.tier, "The tier, 0 to 6")->required();
    action.add_option("--depth", options.depth, "The plies walked from the start")
        ->capture_default_str();
    action.add_option("--limit", options.limit, "The gaps listed")->capture_default_str();
}

// repertoire and its subcommands, over a profile's repertoire for one colour.
CLI::App* repertoire_command_of(CLI::App& app, chesscoach::cli::RepertoireOptions& options) {
    CLI::App* command =
        app.add_subcommand("repertoire", "Add to, list, import or measure a profile's repertoire.");
    command->require_subcommand(1);
    command->add_option("--data-dir", options.data_dir, "The data directory holding profiles.db")
        ->required();
    command->add_option("--colour", options.colour, "The repertoire's colour: white or black")
        ->required();
    command->add_option("--profile", options.profile, "The profile")->capture_default_str();
    command->add_option("--db", options.db, "The opening database (coverage and gaps)");
    action_of(*command, "add", "Choose, at each of the colour's positions, the line's move")
        ->add_option("--moves", options.moves,
                     "The moves from the start position, UCI or SAN, separated by spaces")
        ->required();
    action_of(*command, "remove", "Remove the choice at the position the line reaches")
        ->add_option("--moves", options.moves,
                     "The moves from the start position, UCI or SAN, separated by spaces")
        ->required();
    action_of(*command, "list", "List the choices");
    tier_options_of(*action_of(*command, "coverage",
                               "Print the share of games the repertoire answers and its largest "
                               "gaps at a tier"),
                    options);
    tier_options_of(*action_of(*command, "gaps", "List the gaps at a tier, largest first"),
                    options);
    action_of(*command, "import", "Add the choices of a PGN file's games and variations")
        ->add_option("file", options.pgn, "The PGN file")
        ->required();
    return command;
}

// drill and its subcommands, over the cards of a profile's repertoire for one colour.
CLI::App* drill_command_of(CLI::App& app, chesscoach::cli::DrillOptions& options,
                           std::int64_t& at) {
    CLI::App* command = app.add_subcommand("drill", "List, review or sync a repertoire's cards.");
    command->require_subcommand(1);
    command->add_option("--data-dir", options.data_dir, "The data directory holding profiles.db")
        ->required();
    command->add_option("--colour", options.colour, "The repertoire's colour: white or black")
        ->required();
    command->add_option("--profile", options.profile, "The profile")->capture_default_str();
    command->add_option("--at", at, "The time in Unix seconds (default: the clock's)");
    action_of(*command, "list", "List the cards due");
    CLI::App* review = action_of(*command, "review", "Grade a card");
    review->add_option("card", options.card, "The card's id")->required();
    review->add_option("grade", options.grade, "again, hard, good or easy")->required();
    action_of(*command, "sync", "Bring the cards in step with the repertoire");
    return command;
}

// profile and its subcommands, over the profiles of a data directory.
CLI::App* profile_command_of(CLI::App& app, chesscoach::cli::ProfileOptions& options) {
    CLI::App* command =
        app.add_subcommand("profile", "List, create or remove profiles, or export or import them.");
    command->require_subcommand(1);
    command->add_option("--data-dir", options.data_dir, "The data directory holding profiles.db")
        ->required();
    action_of(*command, "list", "List the profiles");
    CLI::App* create = action_of(*command, "create", "Create a profile");
    create->add_option("name", options.name, "The profile's name")->required();
    create->add_option("--tier", options.tier, "Its tier, 0 to 6")->required();
    action_of(*command, "remove", "Remove a profile with everything it holds")
        ->add_option("name", options.name, "The profile's name")
        ->required();
    action_of(*command, "export", "Write a copy of profiles.db to a new file")
        ->add_option("path", options.path, "The file to write, which must not exist")
        ->required();
    action_of(*command, "import", "Replace profiles.db with an exported file")
        ->add_option("path", options.path, "The exported file")
        ->required();
    return command;
}

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
        explore_command
            ->add_option("--seconds", explore_engine_options.query.seconds,
                         "The engine's search time (--engine)")
            ->check(CLI::Validator(check_seconds, "SECONDS"));
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
            ->capture_default_str()
            ->check(CLI::Validator(check_seconds, "SECONDS"));
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

        chesscoach::cli::SparOptions spar;
        CLI::App* spar_command = spar_command_of(app, spar);
        int spar_tier = 0;
        std::string spar_colour;
        std::uint64_t spar_seed = 0;
        std::string spar_script;
        std::string spar_stockfish;
        std::int64_t spar_replay = 0;
        const CLI::Option* spar_tier_option =
            spar_command->add_option("--tier", spar_tier, "The tier, 0 to 6");
        const CLI::Option* spar_colour_option =
            spar_command->add_option("--colour", spar_colour, "The user's colour: white or black");
        const CLI::Option* spar_seed_option = spar_command->add_option(
            "--seed", spar_seed, "The session's seed (default: the clock's seconds)");
        spar_command->add_option("--end-move", spar.end_move, "The move number the session ends at")
            ->capture_default_str();
        spar_command
            ->add_option("--profile", spar.profile, "The profile the session is saved under")
            ->capture_default_str();
        const CLI::Option* spar_stockfish_option = spar_command->add_option(
            "--stockfish", spar_stockfish,
            "The engine binary (else CHESS_COACH_STOCKFISH, else stockfish on the PATH)");
        spar_command->add_option("--threads", spar.engine.threads, "The engine's threads")
            ->capture_default_str();
        spar_command->add_option("--hash", spar.engine.hash_mb, "The engine's hash in MB")
            ->capture_default_str();
        spar_command->add_option("--seconds", spar.seconds, "The engine's time per reply")
            ->check(CLI::Validator(check_seconds, "SECONDS"))
            ->capture_default_str();
        CLI::Option* spar_script_option = spar_command->add_option(
            "--script", spar_script, "The user's moves, UCI or SAN, separated by spaces");
        CLI::Option* spar_list_option =
            spar_command->add_flag("--list", spar.list, "List the profile's saved sessions");
        const CLI::Option* spar_replay_option =
            spar_command->add_option("--replay", spar_replay, "Replay a saved session by its id");
        spar_list_option->excludes(spar_script_option);

        chesscoach::cli::RepertoireOptions repertoire;
        const CLI::App* repertoire_command = repertoire_command_of(app, repertoire);
        chesscoach::cli::DrillOptions drill;
        std::int64_t drill_at = 0;
        const CLI::App* drill_command = drill_command_of(app, drill, drill_at);
        chesscoach::cli::ProfileOptions profile;
        const CLI::App* profile_command = profile_command_of(app, profile);

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
        if (*spar_command) {
            if (*spar_tier_option) {
                spar.tier = spar_tier;
            }
            if (*spar_colour_option) {
                spar.colour = spar_colour;
            }
            if (*spar_seed_option) {
                spar.seed = spar_seed;
            }
            if (*spar_stockfish_option) {
                spar.engine.stockfish = spar_stockfish;
            }
            if (*spar_script_option) {
                spar.script = spar_script;
            }
            if (*spar_replay_option) {
                spar.replay = spar_replay;
            }
            return report(chesscoach::cli::run_spar(spar, std::cin, std::cout));
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
        if (*repertoire_command) {
            repertoire.action = parsed_action(*repertoire_command);
            return report(chesscoach::cli::run_repertoire(repertoire, std::cout));
        }
        if (*drill_command) {
            drill.action = parsed_action(*drill_command);
            if (drill_command->count("--at") > 0) {
                drill.at = drill_at;
            }
            return report(chesscoach::cli::run_drill(drill, std::cout));
        }
        if (*profile_command) {
            profile.action = parsed_action(*profile_command);
            return report(chesscoach::cli::run_profile(profile, std::cout));
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

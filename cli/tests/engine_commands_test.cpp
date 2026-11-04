// Unit tests of the CLI's engine commands (cli; unit): eval and explore --engine against core's
// fake engine through core/tests/support/fake-stockfish.sh, with their whole expected output. A
// first eval searches and stores; a second of the same position is served from the engine cache
// without the engine (its binary is then a path that does not exist, and the fake's log is not
// written); a minimum depth above the cached one searches again; a missing binary is an error; the
// data directory gets profiles.db and nothing else.
#include "commands/eval.hpp"
#include "commands/explore.hpp"
#include "fixture.hpp"
#include "scratch_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using chesscoach::cli::EvalOptions;
using chesscoach::cli::ExploreOptions;
using chesscoach::cli::test::FixtureDb;
using chesscoach::cli::test::key_after;
using chesscoach::testing::ScratchDir;

namespace {

constexpr std::string_view start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";

// The fake engine's environment, set before any engine starts: core's fake-uci and its normal
// script, which the wrapper script reads, logging every line it receives to fake.log in the
// scratch directory.
void use_fake_engine(const std::filesystem::path& scratch) {
    // NOLINTBEGIN(concurrency-mt-unsafe): one test at a time
    setenv("CHESSCOACH_FAKE_UCI", CHESSCOACH_FAKE_UCI, 1);
    setenv("CHESSCOACH_FAKE_UCI_SCRIPT", "normal", 1);
    setenv("CHESSCOACH_FAKE_UCI_LOG", (scratch / "fake.log").c_str(), 1);
    // NOLINTEND(concurrency-mt-unsafe)
}

// Whether the fake engine has started since the scratch directory was made or last forgotten.
bool engine_ran(const ScratchDir& scratch) {
    return std::filesystem::exists(scratch.path("fake.log"));
}

void forget_engine(const ScratchDir& scratch) { std::filesystem::remove(scratch.path("fake.log")); }

EvalOptions eval_options(const ScratchDir& scratch, int min_depth = 3) {
    EvalOptions options;
    options.data_dir = scratch.path("data").string();
    options.moves = "";
    options.query = {.lines = 3, .seconds = 0.05, .min_depth = min_depth};
    options.engine.stockfish = CHESSCOACH_FAKE_STOCKFISH;
    return options;
}

std::string run_eval(const EvalOptions& options) {
    std::ostringstream out;
    const auto result = chesscoach::cli::run_eval(options, out);
    if (!result) {
        return "error: " + result.error().message;
    }
    return out.str();
}

std::string run_explore(const ExploreOptions& options) {
    std::ostringstream out;
    const auto result = chesscoach::cli::run_explore(options, out);
    if (!result) {
        return "error: " + result.error().message;
    }
    return out.str();
}

std::string start_eval(std::string_view cached) {
    return fmt::format("fen: {}\n"
                       "key: {}\n"
                       "engine: FakeUCI 1.0\n"
                       "1: depth 3 cp 29 e4 e5\n"
                       "2: depth 3 cp 28 d4 e5\n"
                       "3: depth 3 cp 27 Nf3 e5\n"
                       "cached: {}\n",
                       start_fen, key_after(""), cached);
}

std::vector<std::string> entries(const std::string& dir) {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        names.push_back(entry.path().filename().string());
    }
    return names;
}

} // namespace

TEST_CASE("eval searches on a miss and serves a repeat from the engine cache", "[cli][engine]") {
    const ScratchDir scratch("engine-commands", "repeat", use_fake_engine);
    CHECK(run_eval(eval_options(scratch)) == start_eval("no"));
    CHECK(engine_ran(scratch));
    CHECK(entries(scratch.path("data").string()) == std::vector<std::string>{"profiles.db"});

    // The second run never looks for the engine: its binary does not exist.
    forget_engine(scratch);
    auto again = eval_options(scratch);
    again.engine.stockfish = scratch.path("no-such-engine").string();
    CHECK(run_eval(again) == start_eval("yes"));
    CHECK_FALSE(engine_ran(scratch));
    CHECK(entries(scratch.path("data").string()) == std::vector<std::string>{"profiles.db"});
}

TEST_CASE("eval searches again when the cache is not deep enough", "[cli][engine]") {
    const ScratchDir scratch("engine-commands", "min-depth", use_fake_engine);
    CHECK(run_eval(eval_options(scratch)) == start_eval("no"));
    forget_engine(scratch);
    CHECK(run_eval(eval_options(scratch, 4)) == start_eval("no"));
    CHECK(engine_ran(scratch));
    // A line count is its own cache entry.
    forget_engine(scratch);
    auto one_line = eval_options(scratch);
    one_line.query.lines = 1;
    CHECK(run_eval(one_line) == fmt::format("fen: {}\n"
                                            "key: {}\n"
                                            "engine: FakeUCI 1.0\n"
                                            "1: depth 3 cp 29 e4 e5\n"
                                            "cached: no\n",
                                            start_fen, key_after("")));
    CHECK(engine_ran(scratch));
}

TEST_CASE("eval with --db adds the stored evaluation from white's side", "[cli][engine]") {
    const ScratchDir scratch("engine-commands", "stored", use_fake_engine);
    const FixtureDb db;
    auto options = eval_options(scratch);
    options.db = db.path();
    CHECK(run_eval(options) == fmt::format("fen: {}\n"
                                           "key: {}\n"
                                           "stored: cp 19 depth 40 best e4\n"
                                           "engine: FakeUCI 1.0\n"
                                           "1: depth 3 cp 29 e4 e5\n"
                                           "2: depth 3 cp 28 d4 e5\n"
                                           "3: depth 3 cp 27 Nf3 e5\n"
                                           "cached: no\n",
                                           start_fen, key_after("")));
}

TEST_CASE("eval reports a missing engine, a bad request and a bad position", "[cli][engine]") {
    const ScratchDir scratch("engine-commands", "errors", use_fake_engine);
    auto missing = eval_options(scratch);
    missing.engine.stockfish = scratch.path("no-such-engine").string();
    CHECK(run_eval(missing) == "error: no engine at " + scratch.path("no-such-engine").string());
    auto no_lines = eval_options(scratch);
    no_lines.query.lines = 0;
    CHECK(run_eval(no_lines) == "error: --lines 0 is not at least 1");
    auto bad_move = eval_options(scratch);
    bad_move.moves = "e4 e9";
    CHECK(run_eval(bad_move).starts_with("error: "));
    CHECK_FALSE(engine_ran(scratch));
}

TEST_CASE("explore --engine adds the engine's first line after the eval line", "[cli][engine]") {
    const ScratchDir scratch("engine-commands", "explore", use_fake_engine);
    const FixtureDb db;
    const ExploreOptions plain{.db = db.path(), .tier = 3, .fen = {}, .moves = "e4 e5"};
    const auto without = run_explore(plain);
    REQUIRE_FALSE(without.starts_with("error"));

    auto with = plain;
    with.engine = chesscoach::cli::ExploreEngine{
        .data_dir = scratch.path("data").string(),
        .query = {.lines = 1, .seconds = 0.05, .min_depth = 3},
        .settings = {.stockfish = CHESSCOACH_FAKE_STOCKFISH, .threads = 1, .hash_mb = 16},
    };
    // The fake's pv is fixed: after 1. e4 e5 its e2e4 is not legal, so the pv ends in UCI.
    const auto eval_line_end = without.find('\n', without.find("eval: ")) + 1;
    const auto expected = without.substr(0, eval_line_end) + "engine: depth 3 cp 29 e2e4\n" +
                          without.substr(eval_line_end);
    CHECK(run_explore(with) == expected);
    CHECK(engine_ran(scratch));

    // Through the same cache: served without the engine the second time.
    forget_engine(scratch);
    with.engine->settings.stockfish = scratch.path("no-such-engine").string();
    CHECK(run_explore(with) == expected);
    CHECK_FALSE(engine_ran(scratch));
}

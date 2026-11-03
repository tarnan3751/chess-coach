// Unit tests of the CLI's engine commands (cli; unit): eval and explore --engine against core's
// fake engine through cli/tests/fake-stockfish.sh, with their whole expected output. A first
// eval searches and stores; a second of the same position is served from the engine cache without
// the engine (its binary is then a path that does not exist, and the fake's log is not written);
// a minimum depth above the cached one searches again; a missing binary is an error; the data
// directory gets profiles.db and nothing else.
#include "commands/eval.hpp"
#include "commands/explore.hpp"
#include "fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using chesscoach::cli::EvalOptions;
using chesscoach::cli::ExploreOptions;
using chesscoach::cli::test::FixtureDb;
using chesscoach::cli::test::key_after;

namespace {

constexpr std::string_view start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";

// A data directory and the fake engine's log for one test, under the build directory; the fake
// is found through CHESSCOACH_FAKE_UCI, which the wrapper script reads.
class Scratch {
  public:
    explicit Scratch(std::string_view name)
        : root_(std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / "engine-commands" / name),
          data_(root_ / "data"), log_(root_ / "fake.log") {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        // NOLINTBEGIN(concurrency-mt-unsafe): set before any engine starts, one test at a time
        setenv("CHESSCOACH_FAKE_UCI", CHESSCOACH_FAKE_UCI, 1);
        setenv("CHESSCOACH_FAKE_UCI_LOG", log_.c_str(), 1);
        // NOLINTEND(concurrency-mt-unsafe)
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    [[nodiscard]] std::string data() const { return data_.string(); }
    [[nodiscard]] bool engine_ran() const { return std::filesystem::exists(log_); }
    void forget_engine() const { std::filesystem::remove(log_); }
    [[nodiscard]] std::string missing() const { return (root_ / "no-such-engine").string(); }

  private:
    std::filesystem::path root_;
    std::filesystem::path data_;
    std::filesystem::path log_;
};

EvalOptions eval_options(const Scratch& scratch, int min_depth = 3) {
    EvalOptions options;
    options.data_dir = scratch.data();
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
    const Scratch scratch("repeat");
    CHECK(run_eval(eval_options(scratch)) == start_eval("no"));
    CHECK(scratch.engine_ran());
    CHECK(entries(scratch.data()) == std::vector<std::string>{"profiles.db"});

    // The second run never looks for the engine: its binary does not exist.
    scratch.forget_engine();
    auto again = eval_options(scratch);
    again.engine.stockfish = scratch.missing();
    CHECK(run_eval(again) == start_eval("yes"));
    CHECK_FALSE(scratch.engine_ran());
    CHECK(entries(scratch.data()) == std::vector<std::string>{"profiles.db"});
}

TEST_CASE("eval searches again when the cache is not deep enough", "[cli][engine]") {
    const Scratch scratch("min-depth");
    CHECK(run_eval(eval_options(scratch)) == start_eval("no"));
    scratch.forget_engine();
    CHECK(run_eval(eval_options(scratch, 4)) == start_eval("no"));
    CHECK(scratch.engine_ran());
    // A line count is its own cache entry.
    scratch.forget_engine();
    auto one_line = eval_options(scratch);
    one_line.query.lines = 1;
    CHECK(run_eval(one_line) == fmt::format("fen: {}\n"
                                            "key: {}\n"
                                            "engine: FakeUCI 1.0\n"
                                            "1: depth 3 cp 29 e4 e5\n"
                                            "cached: no\n",
                                            start_fen, key_after("")));
    CHECK(scratch.engine_ran());
}

TEST_CASE("eval with --db adds the stored evaluation from white's side", "[cli][engine]") {
    const Scratch scratch("stored");
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
    const Scratch scratch("errors");
    auto missing = eval_options(scratch);
    missing.engine.stockfish = scratch.missing();
    CHECK(run_eval(missing) == "error: no engine at " + scratch.missing());
    auto no_lines = eval_options(scratch);
    no_lines.query.lines = 0;
    CHECK(run_eval(no_lines) == "error: --lines 0 is not at least 1");
    auto bad_move = eval_options(scratch);
    bad_move.moves = "e4 e9";
    CHECK(run_eval(bad_move).starts_with("error: "));
    CHECK_FALSE(scratch.engine_ran());
}

TEST_CASE("explore --engine adds the engine's first line after the eval line", "[cli][engine]") {
    const Scratch scratch("explore");
    const FixtureDb db;
    const ExploreOptions plain{.db = db.path(), .tier = 3, .fen = {}, .moves = "e4 e5"};
    const auto without = run_explore(plain);
    REQUIRE_FALSE(without.starts_with("error"));

    auto with = plain;
    with.engine = chesscoach::cli::ExploreEngine{
        .data_dir = scratch.data(),
        .query = {.lines = 1, .seconds = 0.05, .min_depth = 3},
        .settings = {.stockfish = CHESSCOACH_FAKE_STOCKFISH, .threads = 1, .hash_mb = 16},
    };
    // The fake's pv is fixed: after 1. e4 e5 its e2e4 is not legal, so the pv ends in UCI.
    const auto eval_line_end = without.find('\n', without.find("eval: ")) + 1;
    const auto expected = without.substr(0, eval_line_end) + "engine: depth 3 cp 29 e2e4\n" +
                          without.substr(eval_line_end);
    CHECK(run_explore(with) == expected);
    CHECK(scratch.engine_ran());

    // Through the same cache: served without the engine the second time.
    scratch.forget_engine();
    with.engine->settings.stockfish = scratch.missing();
    CHECK(run_explore(with) == expected);
    CHECK_FALSE(scratch.engine_ran());
}

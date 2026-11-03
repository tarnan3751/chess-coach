// Unit tests of chess-coach-cli spar (cli; unit) on the fixture database, with core's fake engine
// answering the first legal move once off book (cli/tests/fake-stockfish-legal.sh): a scripted
// session meeting a Dubious interruption with its better choices, leaving the book, ending at its
// move, saved, listed and replayed to the same summary; the moves read from stdin until it ends;
// and the errors the command returns before it opens a file.
#include <chesscoach/opening_store.hpp>
#include <chesscoach/sampler.hpp>

#include "commands/spar.hpp"
#include "fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using chesscoach::cli::run_spar;
using chesscoach::cli::SparOptions;
using chesscoach::cli::test::FixtureDb;

namespace {

// A data directory and the fake engine's log for one test, under the build directory; the fake
// is found through CHESSCOACH_FAKE_UCI, which the wrapper script reads.
class Scratch {
  public:
    explicit Scratch(std::string_view name)
        : root_(std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / "spar" / name),
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

  private:
    std::filesystem::path root_;
    std::filesystem::path data_;
    std::filesystem::path log_;
};

// A seed whose first draw at the start position, tier 3, is 1. e4.
std::uint64_t seed_drawing_e4(const FixtureDb& db) {
    auto store = chesscoach::OpeningStore::open(db.path());
    REQUIRE(store.has_value());
    const auto candidates = chesscoach::sampler_candidates(
        store->moves(chesscoach::Position::start().key(), chesscoach::Tier::r1600_1799), {});
    for (std::uint64_t seed = 1;; ++seed) {
        chesscoach::Splitmix64 generator(seed);
        if (chesscoach::sample_reply(candidates, generator) == "e2e4") {
            return seed;
        }
    }
}

SparOptions session(const FixtureDb& db, const Scratch& scratch, std::uint64_t seed) {
    SparOptions options;
    options.db = db.path();
    options.data_dir = scratch.data();
    options.tier = 3;
    options.colour = "black";
    options.seed = seed;
    options.end_move = 3;
    options.engine.stockfish = CHESSCOACH_FAKE_STOCKFISH_LEGAL;
    options.seconds = 0.01;
    return options;
}

// The command's output, or its error message prefixed with "error: ".
std::string run(const SparOptions& options, const std::string& input = "") {
    std::istringstream in(input);
    std::ostringstream out;
    const auto result = run_spar(options, in, out, [] { return std::int64_t{86400}; });
    return result ? out.str() : "error: " + result.error().message;
}

// The lines of the text from the first that starts with the prefix.
std::string from(const std::string& text, std::string_view prefix) {
    const auto at = text.find(prefix);
    return at == std::string::npos ? std::string{} : text.substr(at);
}

} // namespace

TEST_CASE("spar --script meets a Dubious move, leaves the book, ends and is saved", "[cli]") {
    const FixtureDb db;
    const Scratch scratch("scripted");
    const std::uint64_t seed = seed_drawing_e4(db);
    SparOptions options = session(db, scratch, seed);
    options.script = "e6 a6";
    const std::string output = run(options);
    // 1. e4 from the book; 1... e6, Dubious and out of book, with e4's better replies by total.
    CHECK(output.starts_with(fmt::format("seed: {}\n"
                                         "coach: e4 (e2e4)\n"
                                         "you: e6 (e7e6)\n"
                                         "coach: Dubious move: e6 (e7e6), badges PD, loss -\n"
                                         "  better: e5 (e7e5) P 300 games 47% 10% 43%\n"
                                         "  better: c5 (c7c5) BP 250 games 44% 8% 48%\n"
                                         "out of book at tier 3 (1600-1799): the engine plays "
                                         "from here\n"
                                         "coach: ",
                                         seed)));
    CHECK(scratch.engine_ran());
    CHECK(output.find("you: a6 (a7a6)\n") != std::string::npos);
    CHECK(output.find("session ended at move 3\n") != std::string::npos);
    CHECK(output.find("summary: 5 moves, 1 interruptions, reached off book, result none\n") !=
          std::string::npos);
    CHECK(output.ends_with("saved: session 1 under Default\n"));

    SparOptions list;
    list.data_dir = scratch.data();
    list.list = true;
    CHECK(run(list) == "id date tier colour moves interruptions result\n"
                       "1 1970-01-02 3 black 5 1 -\n");

    SparOptions replay;
    replay.db = db.path();
    replay.data_dir = scratch.data();
    replay.replay = 1;
    replay.end_move = 25; // ignored: the saved session's own end move, 3, is used
    const std::string replayed = run(replay);
    CHECK(replayed.starts_with(fmt::format("replay of session 1: tier 3, black, seed {}\n"
                                           "coach: Dubious move: e6 (e7e6), badges PD, loss -\n",
                                           seed)));
    // The replay's summary and moves are the session's.
    const auto summary = from(output, "summary: ");
    CHECK(from(replayed, "summary: ") == summary.substr(0, summary.find("saved: ")));
}

TEST_CASE("spar reads the user's moves from stdin and stops at the end of the input", "[cli]") {
    const FixtureDb db;
    const Scratch scratch("interactive");
    const std::uint64_t seed = seed_drawing_e4(db);
    const std::string output = run(session(db, scratch, seed), "e9\ne5\n");
    CHECK(output.starts_with(fmt::format("seed: {}\n"
                                         "coach: e4 (e2e4)\n"
                                         "1. e4 > not a legal move here: e9\n"
                                         "1. e4 > you: e5 (e7e5)\n",
                                         seed)));
    CHECK(output.find("session stopped before its end\n") != std::string::npos);
    CHECK(output.ends_with("saved: session 1 under Default\n"));
}

TEST_CASE("spar refuses a bad command before it opens a file", "[cli]") {
    const FixtureDb db;
    const Scratch scratch("refusals");
    SparOptions options = session(db, scratch, 1);
    options.colour.reset();
    CHECK(run(options) == "error: a new session needs --tier and --colour");
    options.colour = "green";
    CHECK(run(options) == "error: --colour must be white or black, not green");
    options.colour = "white";
    options.tier = 9;
    CHECK(run(options) == "error: tier 9 is not one of 0 to 6");
    options.tier = 3;
    options.list = true;
    options.replay = 1;
    CHECK(run(options) == "error: --list and --replay exclude each other");
    CHECK_FALSE(std::filesystem::exists(scratch.data()));
    options.list = false;
    options.profile = "Nobody";
    CHECK(run(options) == "error: no profile named Nobody");
    options.profile = "Default";
    CHECK(run(options) == "error: no saved session 1");
}

// Integration tests of run_build on the sample corpus (pipeline/tests/sample/, docs/reference/
// pipeline.md, **The sample corpus**): the golden dump reproduced byte for byte in-process
// through core's OpeningStore::dump, the report's input lines, two runs and one and four
// threads alike, a sketch too small to matter, the §6.4 invariants at min_games 1 (inflow
// exact, the start position's admitted games included) and at 2, and the refusals (pipeline;
// unit).
#include "build.hpp"

#include <chesscoach/key.hpp>
#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>

#include "test_files.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

using chesscoach::Key;
using chesscoach::pipeline::BuildOptions;
using chesscoach::pipeline::Report;
using chesscoach::pipeline::run_build;
namespace testing = chesscoach::pipeline::testing;

namespace {

std::filesystem::path sample_dir() { return CHESSCOACH_SAMPLE_DIR; }

BuildOptions sample_options(const std::filesystem::path& out) {
    BuildOptions options;
    options.inputs = {sample_dir() / "lichess_db_standard_rated_2013-01.pgn.zst"};
    options.output = out;
    options.eval = sample_dir() / "eval" / "lichess_db_eval.jsonl.zst";
    options.names = std::filesystem::path(CHESSCOACH_OPENINGS_DIR);
    options.min_games = 2;
    options.max_ply = 20;
    options.threads = 2;
    return options;
}

std::string build_and_dump(const BuildOptions& options) {
    Report report;
    std::ostringstream progress;
    const auto built = run_build(options, report, progress);
    REQUIRE(built.has_value());
    auto store = chesscoach::OpeningStore::open(options.output);
    REQUIRE(store.has_value());
    std::ostringstream dump;
    store->dump(dump);
    return dump.str();
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Counts {
    std::int64_t white = 0;
    std::int64_t draws = 0;
    std::int64_t black = 0;
    friend bool operator==(const Counts&, const Counts&) = default;
};

using NodeKey = std::pair<std::int64_t, std::int64_t>; // (key as stored, tier)

struct Tables {
    std::map<NodeKey, Counts> nodes;
    std::map<NodeKey, Counts> outflow;
    std::map<NodeKey, Counts> inflow;
    std::map<std::string, std::string> meta;
};

Tables read_tables(const std::filesystem::path& path) {
    const SQLite::Database db(path, SQLite::OPEN_READONLY);
    Tables tables;
    SQLite::Statement nodes(db, "SELECT key, tier, games, white, draws, black FROM nodes");
    while (nodes.executeStep()) {
        const Counts counts{
            .white = nodes.getColumn(3).getInt64(),
            .draws = nodes.getColumn(4).getInt64(),
            .black = nodes.getColumn(5).getInt64(),
        };
        CHECK(nodes.getColumn(2).getInt64() == counts.white + counts.draws + counts.black);
        tables.nodes[{nodes.getColumn(0).getInt64(), nodes.getColumn(1).getInt64()}] = counts;
    }
    SQLite::Statement moves(db, "SELECT key, tier, child, white, draws, black FROM moves");
    while (moves.executeStep()) {
        const NodeKey parent{moves.getColumn(0).getInt64(), moves.getColumn(1).getInt64()};
        const NodeKey child{moves.getColumn(2).getInt64(), moves.getColumn(1).getInt64()};
        const Counts counts{
            .white = moves.getColumn(3).getInt64(),
            .draws = moves.getColumn(4).getInt64(),
            .black = moves.getColumn(5).getInt64(),
        };
        CHECK(counts.white + counts.draws + counts.black >= 1);
        for (auto* table : {&tables.outflow[parent], &tables.inflow[child]}) {
            table->white += counts.white;
            table->draws += counts.draws;
            table->black += counts.black;
        }
    }
    SQLite::Statement meta(db, "SELECT key, value FROM meta");
    while (meta.executeStep()) {
        tables.meta[meta.getColumn(0).getString()] = meta.getColumn(1).getString();
    }
    return tables;
}

bool at_most(const Counts& a, const Counts& b) {
    return a.white <= b.white && a.draws <= b.draws && a.black <= b.black;
}

} // namespace

TEST_CASE("run_build reproduces the sample corpus's golden dump", "[build]") {
    const auto dir = testing::scratch_dir("build_golden");
    BuildOptions options = sample_options(dir / "openings.db");
    CHECK(build_and_dump(options) == read_file(sample_dir() / "golden.dump"));
}

TEST_CASE("run_build's report names its inputs, evaluation file and names directory", "[build]") {
    const auto dir = testing::scratch_dir("build_report_inputs");
    const BuildOptions options = sample_options(dir / "openings.db");
    Report report;
    std::ostringstream progress;
    REQUIRE(run_build(options, report, progress).has_value());
    std::ostringstream lines;
    report.write(lines);
    CHECK(lines.str().find("input\t" + options.inputs.front().string() + "\n") !=
          std::string::npos);
    CHECK(report.value("eval") == (sample_dir() / "eval" / "lichess_db_eval.jsonl.zst").string());
    CHECK(report.value("names") == std::filesystem::path(CHESSCOACH_OPENINGS_DIR).string());
}

TEST_CASE("run_build gives the same dump on two runs, any thread count and a tiny sketch",
          "[build]") {
    const auto dir = testing::scratch_dir("build_determinism");
    BuildOptions one = sample_options(dir / "one.db");
    one.threads = 1;
    BuildOptions four = sample_options(dir / "four.db");
    four.threads = 4;
    BuildOptions again = sample_options(dir / "again.db");
    again.threads = 4;
    BuildOptions tiny = sample_options(dir / "tiny.db");
    tiny.sketch_bytes = 1024; // collisions everywhere: more candidates, the same survivors
    const std::string first = build_and_dump(one);
    CHECK(build_and_dump(four) == first);
    CHECK(build_and_dump(again) == first);
    CHECK(build_and_dump(tiny) == first);
}

TEST_CASE("run_build keeps the §6.4 invariants on the sample", "[build]") {
    const auto dir = testing::scratch_dir("build_invariants");
    const std::int64_t start = chesscoach::key_to_sql(chesscoach::Position::start().key());
    for (const std::uint32_t min_games : {1U, 2U}) {
        BuildOptions options = sample_options(dir / ("min" + std::to_string(min_games) + ".db"));
        options.min_games = min_games;
        options.eval.reset();
        options.names.reset();
        Report report;
        std::ostringstream progress;
        REQUIRE(run_build(options, report, progress).has_value());
        const Tables tables = read_tables(options.output);
        REQUIRE_FALSE(tables.nodes.empty());
        for (const auto& [id, counts] : tables.nodes) {
            // 1: every node reaches min_games; 2: outflow at most the visits.
            CHECK(counts.white + counts.draws + counts.black >= std::int64_t{min_games});
            CHECK(at_most(tables.outflow.contains(id) ? tables.outflow.at(id) : Counts{}, counts));
            // 3: inflow, exact when every visited position survives.
            Counts inflow = tables.inflow.contains(id) ? tables.inflow.at(id) : Counts{};
            if (id.first == start) {
                const auto admitted =
                    std::stoll(tables.meta.at("admitted_tier_" + std::to_string(id.second)));
                CHECK(admitted <= counts.white + counts.draws + counts.black);
                if (min_games == 1) {
                    const auto visits = counts.white + counts.draws + counts.black;
                    CHECK(visits == admitted + inflow.white + inflow.draws + inflow.black);
                }
                continue;
            }
            if (min_games == 1) {
                CHECK(inflow == counts);
            } else {
                CHECK(at_most(inflow, counts));
            }
        }
        for (const auto& [id, counts] : tables.outflow) {
            CHECK(tables.nodes.contains(id));
        }
    }
}

TEST_CASE("run_build refuses an existing output and a misnamed input", "[build]") {
    const auto dir = testing::scratch_dir("build_refusals");
    BuildOptions options = sample_options(dir / "openings.db");
    testing::write_file(dir / "openings.db", "");
    Report report;
    std::ostringstream progress;
    CHECK_FALSE(run_build(options, report, progress).has_value());
    std::filesystem::remove(dir / "openings.db");
    testing::write_file(dir / "games.pgn.zst", testing::zstd_compress("x"));
    options.inputs = {dir / "games.pgn.zst"};
    CHECK_FALSE(run_build(options, report, progress).has_value());
    CHECK_FALSE(std::filesystem::exists(dir / "openings.db"));
    CHECK_FALSE(std::filesystem::exists(dir / "openings.db.partial"));
}

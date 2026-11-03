// Unit tests for the evaluation merge (docs/database.md §9) on hand-written lines: the deepest
// evaluation and its tie-breaks, both castling forms, a line skipped whole when its chosen
// evaluation is unusable, unreadable and non-surviving lines, and the earlier of two equal
// lines of one key across a whole file (pipeline; unit).
#include "eval_merge.hpp"

#include <chesscoach/position.hpp>

#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using chesscoach::Key;
using chesscoach::Position;
using chesscoach::pipeline::better;
using chesscoach::pipeline::EvalCandidate;
using chesscoach::pipeline::EvalCounts;
using chesscoach::pipeline::merge_evaluations;
using chesscoach::pipeline::PositionRecord;
using chesscoach::pipeline::read_eval_line;
using chesscoach::pipeline::Tree;
namespace testing = chesscoach::pipeline::testing;

namespace {

constexpr std::string_view start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
constexpr std::string_view castling_fen = "r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq -";

Key key_of(std::string_view fen) { return Position::from_fen(fen)->key(); }

Tree tree_of(std::initializer_list<std::string_view> fens) {
    Tree tree;
    for (const std::string_view fen : fens) {
        tree.by_key.emplace(key_of(fen), tree.positions.size());
        tree.positions.push_back(PositionRecord{
            .key = key_of(fen),
            .fen = std::string(fen),
            .evaluation = std::nullopt,
            .eco = std::nullopt,
            .name = std::nullopt,
        });
    }
    return tree;
}

std::string line(std::string_view fen, std::string_view evals) {
    return R"({"fen":")" + std::string(fen) + R"(","evals":[)" + std::string(evals) + "]}";
}

} // namespace

TEST_CASE("read_eval_line chooses the deepest evaluation, then knodes, then the earlier",
          "[eval]") {
    const Tree tree = tree_of({start_fen});
    EvalCounts counts;
    const auto deepest = read_eval_line(
        line(start_fen, R"({"pvs":[{"cp":19,"line":"e2e4 e7e5"}],"knodes":100,"depth":30},)"
                        R"({"pvs":[{"cp":25,"line":"d2d4 d7d5"}],"knodes":50,"depth":40})"),
        0, tree, counts);
    REQUIRE(deepest.has_value());
    CHECK(testing::engaged(deepest).evaluation.depth == 40);
    CHECK(testing::engaged(deepest).evaluation.cp == 25);
    CHECK(testing::engaged(deepest).evaluation.best_move == "d2d4");

    const auto knodes = read_eval_line(
        line(start_fen, R"({"pvs":[{"cp":19,"line":"e2e4"}],"knodes":100,"depth":40},)"
                        R"({"pvs":[{"mate":-3,"line":"g1f3"}],"knodes":200,"depth":40},)"
                        R"({"pvs":[{"cp":7,"line":"c2c4"}],"knodes":200,"depth":40})"),
        0, tree, counts);
    REQUIRE(knodes.has_value());
    CHECK(testing::engaged(knodes).evaluation.mate == -3);
    CHECK_FALSE(testing::engaged(knodes).evaluation.cp.has_value());
    CHECK(testing::engaged(knodes).evaluation.best_move == "g1f3");
    CHECK(counts.matched == 2);
}

TEST_CASE("read_eval_line accepts castling in both forms and writes the two-square move",
          "[eval]") {
    const Tree tree = tree_of({castling_fen});
    EvalCounts counts;
    for (const auto& [given, expected] : {
             std::pair{"e1h1", "e1g1"},
             std::pair{"e1g1", "e1g1"},
             std::pair{"e1a1", "e1c1"},
             std::pair{"e1c1", "e1c1"},
         }) {
        const auto read =
            read_eval_line(line(castling_fen, std::string(R"({"pvs":[{"cp":5,"line":")") + given +
                                                  R"( e8g8"}],"knodes":1,"depth":20})"),
                           0, tree, counts);
        REQUIRE(read.has_value());
        CHECK(testing::engaged(read).evaluation.best_move == expected);
    }
}

TEST_CASE("read_eval_line skips a line whose chosen evaluation is unusable, with no fallback",
          "[eval]") {
    const Tree tree = tree_of({start_fen});
    EvalCounts counts;
    // The deepest evaluation's first move is illegal; the shallower usable one is not used.
    CHECK_FALSE(read_eval_line(line(start_fen,
                                    R"({"pvs":[{"cp":19,"line":"e2e5"}],"knodes":100,"depth":40},)"
                                    R"({"pvs":[{"cp":19,"line":"e2e4"}],"knodes":100,"depth":30})"),
                               0, tree, counts));
    // No score, two scores, no line.
    CHECK_FALSE(read_eval_line(line(start_fen, R"({"pvs":[{"line":"e2e4"}],"knodes":1,"depth":9})"),
                               0, tree, counts));
    CHECK_FALSE(read_eval_line(
        line(start_fen, R"({"pvs":[{"cp":1,"mate":2,"line":"e2e4"}],"knodes":1,"depth":9})"), 0,
        tree, counts));
    CHECK_FALSE(read_eval_line(line(start_fen, R"({"pvs":[{"cp":1}],"knodes":1,"depth":9})"), 0,
                               tree, counts));
    CHECK(counts.unusable == 4);
}

TEST_CASE("read_eval_line counts unreadable lines and ignores positions that did not survive",
          "[eval]") {
    const Tree tree = tree_of({start_fen});
    EvalCounts counts;
    CHECK_FALSE(read_eval_line(R"({"evals":[]})", 0, tree, counts));
    CHECK_FALSE(read_eval_line(R"({"fen":"not a fen","evals":[]})", 0, tree, counts));
    CHECK_FALSE(
        read_eval_line(R"({"fen":")" + std::string(start_fen) + R"(","evals":[)", 0, tree, counts));
    CHECK(counts.unreadable == 3);
    CHECK_FALSE(read_eval_line(
        line(castling_fen, R"({"pvs":[{"cp":5,"line":"e1g1"}],"knodes":1,"depth":20})"), 0, tree,
        counts));
    CHECK(counts.unreadable == 3);
    CHECK(counts.matched == 0);
    CHECK(counts.lines == 4);
}

TEST_CASE("read_eval_line normalizes an en passant target without a legal capture", "[eval]") {
    const Tree tree = tree_of({"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -"});
    EvalCounts counts;
    const auto read =
        read_eval_line(line("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3",
                            R"({"pvs":[{"cp":-20,"line":"e7e5"}],"knodes":1,"depth":20})"),
                       0, tree, counts);
    REQUIRE(read.has_value());
    CHECK(testing::engaged(read).evaluation.best_move == "e7e5");
}

TEST_CASE("merge_evaluations keeps the earlier of two equal lines of one key", "[eval]") {
    const auto dir = testing::scratch_dir("eval_merge");
    const std::string text =
        line(start_fen, R"({"pvs":[{"cp":11,"line":"e2e4"}],"knodes":10,"depth":30})") + "\n" +
        line(castling_fen, R"({"pvs":[{"cp":3,"line":"e1h1"}],"knodes":10,"depth":30})") + "\n" +
        line(start_fen, R"({"pvs":[{"cp":22,"line":"d2d4"}],"knodes":10,"depth":30})") + "\n" +
        "garbage\n";
    testing::write_file(dir / "eval.jsonl.zst", testing::zstd_compress(text));
    for (const std::size_t threads : {std::size_t{1}, std::size_t{3}}) {
        Tree tree = tree_of({start_fen, castling_fen});
        const auto counts = merge_evaluations(dir / "eval.jsonl.zst", threads, tree);
        REQUIRE(counts.has_value());
        CHECK(counts->lines == 4);
        CHECK(counts->unreadable == 1);
        CHECK(counts->matched == 3);
        REQUIRE(tree.positions.at(0).evaluation.has_value());
        CHECK(testing::engaged(tree.positions.at(0).evaluation).cp == 11);
        CHECK(testing::engaged(tree.positions.at(1).evaluation).best_move == "e1g1");
    }
    const auto candidate = [](std::int64_t depth, std::int64_t knodes, std::uint64_t order) {
        return EvalCandidate{
            .key = 1,
            .knodes = knodes,
            .order = order,
            .evaluation = {.cp = 0, .mate = std::nullopt, .depth = depth, .best_move = "e2e4"},
        };
    };
    CHECK(better(candidate(2, 1, 1), candidate(1, 9, 0)));
    CHECK(better(candidate(2, 9, 1), candidate(2, 1, 0)));
    CHECK(better(candidate(2, 9, 0), candidate(2, 9, 1)));
    CHECK_FALSE(better(candidate(2, 9, 1), candidate(2, 9, 0)));
}

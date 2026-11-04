// Unit tests for `chess-coach-pipeline trim` (docs/reference/pipeline.md, **Trim**): the input
// names it knows, the movetext filter (comments, NAGs, move numbers glued or not, glyphs alone,
// the cap, a result token), the games kept and the tags written, the shapes the build's parser
// reads its own way (a game with no moves, a tag line it rejects), each scanned as the build
// scans it, the evaluation the merge would choose and the lines passed through, the refusals,
// the output's failure; and on the sample corpus, the trimmed month building the untrimmed
// month's dump apart from the skip counts and the trimmed evaluation file building the golden
// dump exactly (pipeline; unit).
#include "trim.hpp"

#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>

#include "build.hpp"
#include "chunker.hpp"
#include "eval_merge.hpp"
#include "game_scanner.hpp"
#include "report.hpp"
#include "test_files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using chesscoach::Position;
using chesscoach::pipeline::BuildOptions;
using chesscoach::pipeline::EvalCounts;
using chesscoach::pipeline::GameScanner;
using chesscoach::pipeline::PositionRecord;
using chesscoach::pipeline::Report;
using chesscoach::pipeline::run_build;
using chesscoach::pipeline::run_trim;
using chesscoach::pipeline::ScanCounts;
using chesscoach::pipeline::ScannedGame;
using chesscoach::pipeline::SkipReason;
using chesscoach::pipeline::split_games;
using chesscoach::pipeline::Tree;
using chesscoach::pipeline::trim_evals;
using chesscoach::pipeline::trim_games;
using chesscoach::pipeline::trim_kind;
using chesscoach::pipeline::TrimKind;
using chesscoach::pipeline::trimmed_eval_line;
using chesscoach::pipeline::trimmed_movetext;
using chesscoach::pipeline::TrimOptions;
namespace testing = chesscoach::pipeline::testing;

namespace {

std::filesystem::path sample_dir() { return CHESSCOACH_SAMPLE_DIR; }

constexpr std::string_view month_name = "lichess_db_standard_rated_2013-01.pgn.zst";
constexpr std::string_view eval_name = "lichess_db_eval.jsonl.zst";
constexpr std::string_view start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Trimmed {
    std::string text;
    chesscoach::pipeline::GameTrimCounts counts;
};

Trimmed trim_text(const std::string& text, std::uint32_t max_ply = 30) {
    std::istringstream in(text);
    std::ostringstream out;
    Trimmed trimmed;
    trimmed.counts = trim_games(in, out, max_ply);
    trimmed.text = out.str();
    return trimmed;
}

struct Scan {
    std::vector<ScannedGame> games;
    ScanCounts counts;
};

// The text scanned as the build scans a batch: cut into games (split_games), each game scanned.
Scan scan(const std::string& text, std::uint32_t max_ply = 30) {
    GameScanner scanner(max_ply, nullptr);
    Scan result;
    for (const std::string_view game : split_games(text)) {
        scanner.scan(game, [&](const ScannedGame& scanned) { result.games.push_back(scanned); });
    }
    result.counts = scanner.counts();
    return result;
}

// The build admits the same games from both texts, with the same positions.
void check_same_admitted(const Scan& untrimmed, const Scan& trimmed) {
    CHECK(trimmed.counts.admitted == untrimmed.counts.admitted);
    REQUIRE(trimmed.games.size() == untrimmed.games.size());
    for (std::size_t i = 0; i < trimmed.games.size(); ++i) {
        CHECK(trimmed.games.at(i).keys == untrimmed.games.at(i).keys);
        CHECK(trimmed.games.at(i).moves == untrimmed.games.at(i).moves);
    }
}

constexpr std::string_view blitz_event = "[Event \"Rated Blitz game\"]\n";
constexpr std::string_view blitz_rest =
    "[Result \"1-0\"]\n[WhiteElo \"1500\"]\n[BlackElo \"1500\"]\n";

// The tags of a game the build admits: Rated Blitz, 1-0, both ratings 1500.
std::string blitz_tags() { return std::string(blitz_event) + std::string(blitz_rest); }

std::string eval_line(std::string_view evals) {
    return R"({"fen":")" + std::string(start_fen) + R"(","evals":[)" + std::string(evals) + "]}";
}

std::string trimmed_line(std::string_view score, std::string_view move, int knodes, int depth) {
    return R"({"fen":")" + std::string(start_fen) + R"(","evals":[{"pvs":[{)" + std::string(score) +
           R"(,"line":")" + std::string(move) + R"("}],"knodes":)" + std::to_string(knodes) +
           R"(,"depth":)" + std::to_string(depth) + "}]}";
}

// A tree in which the start position survives.
Tree start_tree() {
    Tree tree;
    const auto key = Position::from_fen(start_fen)->key();
    tree.by_key.emplace(key, 0);
    tree.positions.push_back(PositionRecord{
        .key = key,
        .fen = std::string(start_fen),
        .evaluation = std::nullopt,
        .eco = std::nullopt,
        .name = std::nullopt,
    });
    return tree;
}

// The merge reads the same evaluation from a line and from its trimmed form.
void check_same_choice(const std::string& line, const std::string& trimmed) {
    const Tree tree = start_tree();
    EvalCounts counts;
    const auto original = read_eval_line(line, 0, tree, counts);
    const auto reread = read_eval_line(trimmed, 0, tree, counts);
    REQUIRE(original.has_value());
    REQUIRE(reread.has_value());
    const auto& a = testing::engaged(original);
    const auto& b = testing::engaged(reread);
    CHECK(a.knodes == b.knodes);
    CHECK(a.evaluation.depth == b.evaluation.depth);
    CHECK(a.evaluation.cp == b.evaluation.cp);
    CHECK(a.evaluation.mate == b.evaluation.mate);
    CHECK(a.evaluation.best_move == b.evaluation.best_move);
}

BuildOptions sample_options(const std::filesystem::path& out) {
    BuildOptions options;
    options.inputs = {sample_dir() / month_name};
    options.output = out;
    options.eval = sample_dir() / "eval" / eval_name;
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

// A dump without meta's skip counts, which a trimmed month changes by the games it dropped.
std::string without_skip_counts(const std::string& dump) {
    std::istringstream in(dump);
    std::string kept;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.starts_with("meta\tgames_skipped\t") && !line.starts_with("meta\tskipped_")) {
            kept += line + '\n';
        }
    }
    return kept;
}

std::string trim_one(const std::filesystem::path& input, const std::filesystem::path& out_dir) {
    std::ostringstream report;
    const auto trimmed =
        run_trim(TrimOptions{.out_dir = out_dir, .inputs = {input}, .max_ply = 30}, report);
    REQUIRE(trimmed.has_value());
    return report.str();
}

} // namespace

TEST_CASE("trim_kind knows a month file and the evaluation database by name", "[trim]") {
    CHECK(trim_kind(std::filesystem::path("in") / month_name) == TrimKind::games);
    CHECK(trim_kind(std::filesystem::path("in") / eval_name) == TrimKind::evals);
    CHECK_FALSE(trim_kind("games.pgn.zst").has_value());
    CHECK_FALSE(trim_kind("lichess_db_standard_rated_2013-1.pgn.zst").has_value());
    CHECK_FALSE(trim_kind("lichess_db_eval.jsonl").has_value());
}

TEST_CASE("trimmed_movetext drops comments, NAGs and move numbers and numbers again", "[trim]") {
    CHECK(trimmed_movetext("1. e4 { [%clk 0:03:00] [%eval 0.17] } 1... e5 $1 2. Nf3?! "
                           "{ [%clk 0:02:58] } 2... Nc6 { a comment } 3. Bb5 1-0",
                           30, "1-0") == "1. e4 e5 2. Nf3?! Nc6 3. Bb5 1-0");
    // Numbers with three dots go like the others; the build reads the moves, not the numbers.
    CHECK(trimmed_movetext("12... Nf6 13. O-O-O 0-1", 30, "0-1") == "1. Nf6 O-O-O 0-1");
}

TEST_CASE("trimmed_movetext reads a number glued to its move and drops a glyph alone", "[trim]") {
    // The parser reads the move after a glued number, and the scanner ignores a glyph alone, so
    // neither counts as a ply: the cap of 3 keeps three moves.
    CHECK(trimmed_movetext("1.e4 ! 1...e5 ?! 2.Nf3 !! Nc6 1-0", 3, "1-0") == "1. e4 e5 2. Nf3 1-0");
    CHECK(trimmed_movetext("1.e4 ! 1...e5 ?! 2.Nf3 !! Nc6 1-0", 30, "1-0") ==
          "1. e4 e5 2. Nf3 Nc6 1-0");
    // A move with its own glyph keeps it, as before; castling with zeros is a move, not a number.
    CHECK(trimmed_movetext("1. e4?! e5 2. Nf3 0-0 1-0", 30, "1-0") == "1. e4?! e5 2. Nf3 0-0 1-0");

    // The build reads the same moves from the trimmed text as from the original, at the cap too.
    const std::string game =
        blitz_tags() + "\n1.e4 ! 1...e5 ?! 2.Nf3 !! 2...Nc6 3.Bb5 ? a6 1-0\n\n";
    for (const std::uint32_t cap : {3U, 30U}) {
        const auto trimmed = trim_text(game, cap);
        CHECK(trimmed.counts.kept == 1);
        const auto untrimmed_scan = scan(game, cap);
        const auto trimmed_scan = scan(trimmed.text, cap);
        check_same_admitted(untrimmed_scan, trimmed_scan);
        REQUIRE(trimmed_scan.games.size() == 1);
        CHECK(trimmed_scan.games.front().moves.size() == std::min<std::size_t>(cap, 6));
    }
}

TEST_CASE("trimmed_movetext ends at the cap, at a result token and at the end", "[trim]") {
    const std::string game = "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 0-1";
    CHECK(trimmed_movetext(game, 3, "0-1") == "1. e4 e5 2. Nf3 0-1");
    CHECK(trimmed_movetext(game, 6, "0-1") == "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 0-1");
    CHECK(trimmed_movetext(game, 0, "0-1") == "0-1");
    // Shorter than the cap.
    CHECK(trimmed_movetext("1. d4 d5 1/2-1/2", 30, "1/2-1/2") == "1. d4 d5 1/2-1/2");
    // A result token mid-way ends the moves; the result written is the Result tag's.
    CHECK(trimmed_movetext("1. e4 e5 * 2. Nf3 Nc6 1-0", 30, "1-0") == "1. e4 e5 1-0");
    CHECK(trimmed_movetext("1-0", 30, "1-0") == "1-0");
}

TEST_CASE("trim_games keeps what the build's header checks admit or count, with their tags",
          "[trim]") {
    const std::string blitz = "[Event \"Rated Blitz game\"]\n"
                              "[Site \"https://lichess.org/abcd1234\"]\n"
                              "[White \"a\"]\n[Black \"b\"]\n[Result \"1-0\"]\n"
                              "[UTCDate \"2013.01.01\"]\n"
                              "[WhiteElo \"1500\"]\n[BlackElo \"1700\"]\n"
                              "[WhiteRatingDiff \"+10\"]\n[ECO \"C60\"]\n"
                              "[Opening \"Ruy Lopez\"]\n[TimeControl \"300+0\"]\n"
                              "[Termination \"Normal\"]\n\n"
                              "1. e4 { [%clk 0:05:00] } 1... e5 { [%clk 0:05:00] } 2. Nf3 $1 "
                              "{ [%eval 0.2] [%clk 0:04:58] } 2... Nc6 3. Bb5 1-0\n\n";
    const std::string bullet = "[Event \"Rated Bullet game\"]\n[Result \"0-1\"]\n"
                               "[WhiteElo \"1500\"]\n[BlackElo \"1500\"]\n\n1. d4 d5 0-1\n\n";
    const std::string unrated = "[Event \"Rated Blitz game\"]\n[Result \"1/2-1/2\"]\n"
                                "[WhiteElo \"?\"]\n[BlackElo \"1500\"]\n\n1. c4 c5 1/2-1/2\n\n";
    const std::string casual = "[Event \"Casual Rapid game\"]\n[Result \"1-0\"]\n"
                               "[WhiteElo \"1500\"]\n[BlackElo \"1500\"]\n\n1. e4 1-0\n\n";
    const std::string chess960 =
        "[Event \"Rated Classical game\"]\n[Site \"https://lichess.org/efgh5678\"]\n"
        "[Result \"0-1\"]\n[WhiteElo \"1800\"]\n[BlackElo \"1900\"]\n[Variant \"Chess960\"]\n"
        "[TimeControl \"900+15\"]\n"
        "[FEN \"bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w HFhf - 0 1\"]\n[SetUp \"1\"]\n\n"
        "1. g3 g6 0-1\n\n";
    const std::string dump = blitz + bullet + unrated + casual + chess960;
    const auto trimmed = trim_text(dump);
    CHECK(trimmed.counts.seen == 5);
    CHECK(trimmed.counts.kept == 2);
    CHECK(trimmed.text == "[Event \"Rated Blitz game\"]\n[Result \"1-0\"]\n"
                          "[WhiteElo \"1500\"]\n[BlackElo \"1700\"]\n\n"
                          "1. e4 e5 2. Nf3 Nc6 3. Bb5 1-0\n\n"
                          "[Event \"Rated Classical game\"]\n[Result \"0-1\"]\n"
                          "[WhiteElo \"1800\"]\n[BlackElo \"1900\"]\n[Variant \"Chess960\"]\n"
                          "[SetUp \"1\"]\n"
                          "[FEN \"bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w HFhf - 0 1\"]\n\n"
                          "1. g3 g6 0-1\n\n");

    // The build admits the same games from both and refuses the Chess960 game for its variant;
    // only the dropped games' counters (speed, unrated, rating) differ.
    const auto untrimmed_scan = scan(dump);
    const auto trimmed_scan = scan(trimmed.text);
    CHECK(trimmed_scan.counts.admitted == untrimmed_scan.counts.admitted);
    CHECK(trimmed_scan.counts.admitted_total() == 1);
    REQUIRE(trimmed_scan.games.size() == 1);
    REQUIRE(untrimmed_scan.games.size() == 1);
    CHECK(trimmed_scan.games.front().keys == untrimmed_scan.games.front().keys);
    CHECK(trimmed_scan.counts.skipped.count(SkipReason::variant) == 1);
    CHECK(trimmed_scan.counts.skipped.total() == 1);
    CHECK(untrimmed_scan.counts.skipped.count(SkipReason::rating) == 1);
}

TEST_CASE("trim_games ends a game at an Event line and keeps one whose moves start empty",
          "[trim]") {
    // A blank line after the tags starts the moves, so the build admits the first game with no
    // move, visiting the start position; the third game's moves never start, which the build
    // counts malformed.
    const std::string dump =
        blitz_tags() + "\n" +
        "[Event \"Rated Blitz game\"]\r\n[Result \"0-1\"]\r\n[WhiteElo \"1600\"]\r\n"
        "[BlackElo \"1600\"]\r\n\r\n1. e4 e5\r\n2. Nf3 0-1\r\n\r\n" +
        blitz_tags() +
        "[Event \"Rated Rapid game\"]\n[Result \"1/2-1/2\"]\n[WhiteElo \"2000\"]\n"
        "[BlackElo \"2000\"]\n\n1/2-1/2\n\n";
    const auto trimmed = trim_text(dump);
    CHECK(trimmed.counts.seen == 4);
    CHECK(trimmed.counts.kept == 3);
    CHECK(trimmed.text == blitz_tags() + "\n1-0\n\n" +
                              "[Event \"Rated Blitz game\"]\n[Result \"0-1\"]\n"
                              "[WhiteElo \"1600\"]\n[BlackElo \"1600\"]\n\n1. e4 e5 2. Nf3 0-1\n\n"
                              "[Event \"Rated Rapid game\"]\n[Result \"1/2-1/2\"]\n"
                              "[WhiteElo \"2000\"]\n[BlackElo \"2000\"]\n\n1/2-1/2\n\n");

    const auto untrimmed_scan = scan(dump);
    const auto trimmed_scan = scan(trimmed.text);
    check_same_admitted(untrimmed_scan, trimmed_scan);
    CHECK(trimmed_scan.counts.admitted_total() == 3);
    REQUIRE(trimmed_scan.games.size() == 3);
    CHECK(trimmed_scan.games.front().keys.size() == 1);
    CHECK(untrimmed_scan.counts.skipped.count(SkipReason::malformed) == 1);
    CHECK(trimmed_scan.counts.skipped.total() == 0);
}

TEST_CASE("trim_games drops a game with a tag line the parser rejects", "[trim]") {
    const std::string moves = "\n1. e4 e5 1-0\n\n";
    // Its closing quote missing; a quote not followed by the closing bracket; a quote escaped by
    // a backslash, which closes nothing, so the closing quote is missing again.
    const std::string no_quote = "[Event \"Rated Blitz game\"]\n[Site \"https://lichess.org/x]\n" +
                                 std::string(blitz_rest) + moves;
    const std::string no_bracket =
        "[Event \"Rated Blitz game\"]\n[White \"a\"b\"]\n" + std::string(blitz_rest) + moves;
    const std::string escaped_close =
        "[Event \"Rated Blitz game\"]\n[White \"a\\\"]\n" + std::string(blitz_rest) + moves;
    // An escaped quote inside a value the parser accepts: the game is kept, the value as written.
    const std::string escaped =
        "[Event \"Rated Blitz game\"]\n[White \"a\\\"b\"]\n" + std::string(blitz_rest) + moves;
    const std::string dump = no_quote + no_bracket + escaped_close + escaped;
    const auto trimmed = trim_text(dump);
    CHECK(trimmed.counts.seen == 4);
    CHECK(trimmed.counts.kept == 1);
    CHECK(trimmed.text == blitz_tags() + "\n1. e4 e5 1-0\n\n");

    const auto untrimmed_scan = scan(dump);
    const auto trimmed_scan = scan(trimmed.text);
    check_same_admitted(untrimmed_scan, trimmed_scan);
    CHECK(trimmed_scan.counts.admitted_total() == 1);
    CHECK(untrimmed_scan.counts.skipped.count(SkipReason::malformed) == 3);
    CHECK(trimmed_scan.counts.skipped.total() == 0);
}

TEST_CASE("trim_games reads a tag value as the parser does, its backslashes dropped", "[trim]") {
    // The parser drops a backslash, so the header checks read Rated Blitz game and admit it; the
    // value is written as it was, for the build to read the same way again.
    const std::string game =
        "[Event \"Rated\\ Blitz game\"]\n" + std::string(blitz_rest) + "\n1. d4 d5 1-0\n\n";
    const auto trimmed = trim_text(game);
    CHECK(trimmed.counts.kept == 1);
    CHECK(trimmed.text == game);
    check_same_admitted(scan(game), scan(trimmed.text));
    CHECK(scan(trimmed.text).counts.admitted_total() == 1);
}

TEST_CASE("trim stops reading once its output has failed", "[trim]") {
    const std::string game = blitz_tags() + "\n1. e4 e5 1-0\n\n";
    std::istringstream games(game + game);
    std::ostringstream failed;
    failed.setstate(std::ios::badbit);
    CHECK(trim_games(games, failed, 30).seen == 0);
    CHECK(games.tellg() == 0);

    std::istringstream lines(
        eval_line(R"({"pvs":[{"cp":5,"line":"e2e4"}],"knodes":3,"depth":20})"));
    CHECK(trim_evals(lines, failed).lines == 0);
    CHECK(lines.tellg() == 0);
}

TEST_CASE("trimmed_eval_line keeps the evaluation the merge would choose", "[trim]") {
    // The deepest.
    const std::string deepest =
        eval_line(R"({"pvs":[{"cp":19,"line":"e2e4 e7e5"},{"cp":7,"line":"c2c4"}],)"
                  R"("knodes":100,"depth":30},)"
                  R"({"pvs":[{"cp":25,"line":"d2d4 d7d5 c2c4"}],"knodes":50,"depth":40})");
    CHECK(trimmed_eval_line(deepest) == trimmed_line(R"("cp":25)", "d2d4", 50, 40));
    check_same_choice(deepest, trimmed_line(R"("cp":25)", "d2d4", 50, 40));
    // At one depth the greater knodes, at equal knodes the earlier; a mate score kept.
    const std::string knodes =
        eval_line(R"({"pvs":[{"cp":19,"line":"e2e4"}],"knodes":100,"depth":40},)"
                  R"({"pvs":[{"mate":-3,"line":"g1f3 g8f6"}],"knodes":200,"depth":40},)"
                  R"({"pvs":[{"cp":7,"line":"c2c4"}],"knodes":200,"depth":40})");
    CHECK(trimmed_eval_line(knodes) == trimmed_line(R"("mate":-3)", "g1f3", 200, 40));
    check_same_choice(knodes, trimmed_line(R"("mate":-3)", "g1f3", 200, 40));
}

TEST_CASE("trimmed_eval_line has nothing for a line the merge cannot use", "[trim]") {
    CHECK_FALSE(trimmed_eval_line("not json").has_value());
    CHECK_FALSE(trimmed_eval_line("[1,2]").has_value());
    CHECK_FALSE(trimmed_eval_line(R"({"evals":[{"pvs":[{"cp":1,"line":"e2e4"}],)"
                                  R"("knodes":1,"depth":9}]})")
                    .has_value());
    // An evaluation without knodes makes the whole line unusable to the merge.
    CHECK_FALSE(trimmed_eval_line(eval_line(R"({"pvs":[{"cp":1,"line":"e2e4"}],"depth":9},)"
                                            R"({"pvs":[{"cp":1,"line":"d2d4"}],"knodes":1,)"
                                            R"("depth":5})"))
                    .has_value());
    CHECK_FALSE(trimmed_eval_line(eval_line(R"({"pvs":[{"line":"e2e4"}],"knodes":1,"depth":9})"))
                    .has_value());
}

TEST_CASE("trimmed_eval_line has nothing for a line whose fen the merge reads otherwise",
          "[trim]") {
    // The merge reads the fen from the raw text, so a space after the colon hides it and an
    // escape in the FEN spoils it; both lines are unreadable to it, and stay so, unchanged.
    const std::string evals =
        R"(","evals":[{"pvs":[{"cp":5,"line":"e2e4"}],"knodes":3,"depth":20}]})";
    const std::string spaced = R"({"fen": ")" + std::string(start_fen) + evals;
    const std::string escaped =
        R"({"fen":"rnbqkbnr\/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -)" + evals;
    CHECK_FALSE(trimmed_eval_line(spaced).has_value());
    CHECK_FALSE(trimmed_eval_line(escaped).has_value());
    const Tree tree = start_tree();
    EvalCounts counts;
    CHECK_FALSE(read_eval_line(spaced, 0, tree, counts).has_value());
    CHECK_FALSE(read_eval_line(escaped, 1, tree, counts).has_value());
    CHECK(counts.unreadable == 2);
    // The same line without either is trimmed.
    CHECK(trimmed_eval_line(R"({"fen":")" + std::string(start_fen) + evals) ==
          trimmed_line(R"("cp":5)", "e2e4", 3, 20));
}

TEST_CASE("trim_evals keeps the line order and passes the unusable lines through", "[trim]") {
    const std::string first = eval_line(R"({"pvs":[{"cp":5,"line":"e2e4 e7e5"}],"knodes":3,)"
                                        R"("depth":20})");
    const std::string second = R"({"fen": "not trimmed", "evals": )";
    const std::string third = eval_line(R"({"pvs":[{"cp":-5,"line":"d2d4"}],"knodes":4,)"
                                        R"("depth":21})");
    std::istringstream in(first + '\n' + second + '\n' + third + '\n');
    std::ostringstream out;
    const auto counts = trim_evals(in, out);
    CHECK(counts.lines == 3);
    CHECK(counts.passed_through == 1);
    CHECK(out.str() == trimmed_line(R"("cp":5)", "e2e4", 3, 20) + '\n' + second + '\n' +
                           trimmed_line(R"("cp":-5)", "d2d4", 4, 21) + '\n');
}

TEST_CASE("run_trim refuses an unknown name, a repeated name and an existing output", "[trim]") {
    const auto dir = testing::scratch_dir("trim_refusals");
    const auto out_dir = dir / "trimmed";
    std::ostringstream report;
    testing::write_file(dir / "games.pgn.zst", testing::zstd_compress("x"));
    CHECK_FALSE(
        run_trim(TrimOptions{.out_dir = out_dir, .inputs = {dir / "games.pgn.zst"}, .max_ply = 30},
                 report)
            .has_value());
    CHECK_FALSE(std::filesystem::exists(out_dir));

    std::filesystem::create_directories(dir / "a");
    std::filesystem::create_directories(dir / "b");
    testing::write_file(dir / "a" / month_name, testing::zstd_compress(""));
    testing::write_file(dir / "b" / month_name, testing::zstd_compress(""));
    CHECK_FALSE(run_trim(TrimOptions{.out_dir = out_dir,
                                     .inputs = {dir / "a" / month_name, dir / "b" / month_name},
                                     .max_ply = 30},
                         report)
                    .has_value());
    CHECK_FALSE(std::filesystem::exists(out_dir));

    std::filesystem::create_directories(out_dir);
    testing::write_file(out_dir / month_name, "kept");
    CHECK_FALSE(
        run_trim(TrimOptions{.out_dir = out_dir, .inputs = {dir / "a" / month_name}, .max_ply = 30},
                 report)
            .has_value());
    CHECK(read_file(out_dir / month_name) == "kept");

    // An input that is not zstd: an error, and nothing left behind.
    std::filesystem::create_directories(dir / "c");
    testing::write_file(dir / "c" / month_name, "not zstd");
    const auto fresh = dir / "fresh";
    CHECK_FALSE(
        run_trim(TrimOptions{.out_dir = fresh, .inputs = {dir / "c" / month_name}, .max_ply = 30},
                 report)
            .has_value());
    CHECK(std::filesystem::is_empty(fresh));
    CHECK(report.str().empty());
}

TEST_CASE("the trimmed sample month builds the sample's dump but for the skip counts",
          "[trim][corpus]") {
    const auto dir = testing::scratch_dir("trim_sample_month");
    const auto report = trim_one(sample_dir() / month_name, dir / "trimmed");
    // The sample's 2000 games: 1364 admitted, 634 of another speed and 2 without a rating.
    CHECK(report.starts_with(std::string(month_name) + ": 2000 games seen, 1364 kept, "));
    CHECK(std::filesystem::file_size(dir / "trimmed" / month_name) <
          std::filesystem::file_size(sample_dir() / month_name));

    const std::string untrimmed = build_and_dump(sample_options(dir / "untrimmed.db"));
    BuildOptions options = sample_options(dir / "trimmed.db");
    options.inputs = {dir / "trimmed" / month_name};
    const std::string trimmed = build_and_dump(options);
    CHECK(trimmed != untrimmed);
    CHECK(without_skip_counts(trimmed) == without_skip_counts(untrimmed));
}

TEST_CASE("the trimmed sample evaluation file builds the golden dump", "[trim][corpus]") {
    const auto dir = testing::scratch_dir("trim_sample_eval");
    const auto report = trim_one(sample_dir() / "eval" / eval_name, dir / "trimmed");
    CHECK(report.starts_with(std::string(eval_name) + ": "));
    BuildOptions options = sample_options(dir / "openings.db");
    options.eval = dir / "trimmed" / eval_name;
    CHECK(build_and_dump(options) == read_file(sample_dir() / "golden.dump"));
}

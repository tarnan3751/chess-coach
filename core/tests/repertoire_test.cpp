// Unit tests for chesscoach/repertoire.hpp (core; unit): coverage on small hand-built stores whose
// keys come from Position, each number worked out by hand in a comment: a white repertoire complete
// to a depth and one with a gap, a black repertoire, a transposition merging mass, truncation below
// the minimum mass, the walk ending off book, with no replies and at the depth, a chosen move with
// no row and an illegal one, the gaps' order, and the store overload reading the same rows.
#include <chesscoach/opening_schema.hpp>
#include <chesscoach/repertoire.hpp>

#include <SQLiteCpp/SQLiteCpp.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using chesscoach::ChildRows;
using chesscoach::Color;
using chesscoach::Coverage;
using chesscoach::CoverageConfig;
using chesscoach::Gap;
using chesscoach::Key;
using chesscoach::Position;
using chesscoach::Repertoire;
using chesscoach::Tier;

namespace {

constexpr Tier tier = Tier::r1400_1599;

// The position after SAN moves separated by spaces, from the start position.
Position after(std::string_view moves) {
    auto position = Position::start();
    while (!moves.empty()) {
        const auto space = moves.find(' ');
        const auto san = moves.substr(0, space);
        moves.remove_prefix(space == std::string_view::npos ? moves.size() : space + 1);
        if (!san.empty()) {
            REQUIRE(position.play_san(san).has_value());
        }
    }
    return position;
}

std::string uci(std::string_view moves, std::string_view san) {
    const auto found = after(moves).uci_of(san);
    REQUIRE(found.has_value());
    return *found;
}

// The rows of a few positions at one tier, as a lookup.
class Rows {
  public:
    // The position after moves has a node, and a moves row for each (SAN, total) reply.
    void add(std::string_view moves,
             std::initializer_list<std::pair<std::string_view, std::int64_t>> replies) {
        const auto position = after(moves);
        ChildRows rows;
        std::int64_t sum = 0;
        for (const auto& [san, total] : replies) {
            auto child = position;
            const auto played = child.play_san(san);
            REQUIRE(played.has_value());
            rows.moves.push_back({
                .key = position.key(),
                .tier = tier,
                .move = played->uci,
                .child = played->child,
                .white = total,
                .draws = 0,
                .black = 0,
                .badges = chesscoach::Badges{},
            });
            sum += total;
        }
        rows.node = chesscoach::NodeRow{
            .key = position.key(),
            .tier = tier,
            .games = sum,
            .white = sum,
            .draws = 0,
            .black = 0,
        };
        rows_.insert_or_assign(position.key(), std::move(rows));
    }

    [[nodiscard]] chesscoach::ChildLookup lookup() const {
        return [this](Key key) {
            const auto found = rows_.find(key);
            return found == rows_.end() ? ChildRows{} : found->second;
        };
    }

    [[nodiscard]] const std::map<Key, ChildRows>& all() const { return rows_; }

  private:
    std::map<Key, ChildRows> rows_;
};

// A repertoire from (moves to the position, SAN of the chosen move) pairs.
Repertoire
repertoire(std::initializer_list<std::pair<std::string_view, std::string_view>> choices) {
    Repertoire chosen;
    for (const auto& [moves, san] : choices) {
        chosen.emplace(after(moves).key(), uci(moves, san));
    }
    return chosen;
}

Coverage coverage(const Repertoire& chosen, Color colour, const Rows& rows, int max_plies,
                  double min_mass = 0.001) {
    return chesscoach::compute_coverage(
        chosen, colour, rows.lookup(),
        CoverageConfig{.max_plies = max_plies, .min_mass = min_mass});
}

std::vector<std::string> line_of(std::initializer_list<std::string_view> ucis) {
    return {ucis.begin(), ucis.end()};
}

// 1. e4 (100) e5 (60) or c5 (40); 2. Nf3; then Nc6 70 / Nf6 30 after e5, d6 50 / Nc6 50 after
// c5; 3. Bb5 after Nc6 (not in the repertoire), Nxe5 after Nf6 and then d6; d4 after the
// Sicilian's Nc6, into a position the store does not have; the Sicilian's d6 is off book.
Rows white_rows() {
    Rows rows;
    rows.add("", {{"e4", 100}});
    rows.add("e4", {{"e5", 60}, {"c5", 40}});
    rows.add("e4 e5", {{"Nf3", 10}});
    rows.add("e4 c5", {{"Nf3", 10}});
    rows.add("e4 e5 Nf3", {{"Nc6", 70}, {"Nf6", 30}});
    rows.add("e4 c5 Nf3", {{"d6", 50}, {"Nc6", 50}});
    rows.add("e4 e5 Nf3 Nc6", {{"Bb5", 5}});
    rows.add("e4 e5 Nf3 Nf6", {{"Nxe5", 5}});
    rows.add("e4 e5 Nf3 Nf6 Nxe5", {{"d6", 100}});
    rows.add("e4 c5 Nf3 Nc6", {{"d4", 5}});
    return rows;
}

Repertoire white_repertoire() {
    return repertoire({
        {"", "e4"},
        {"e4 e5", "Nf3"},
        {"e4 c5", "Nf3"},
        {"e4 e5 Nf3 Nf6", "Nxe5"},
        {"e4 c5 Nf3 Nc6", "d4"},
    });
}

} // namespace

TEST_CASE("A repertoire answering everything to its depth covers it all", "[repertoire]") {
    // To ply 4 every line is answered: 1. e4 (1.0), e5 0.6 and c5 0.4, 2. Nf3 in both, and the
    // four replies at ply 3 (0.42, 0.18, 0.2, 0.2) reach the depth.
    const auto result = coverage(white_repertoire(), Color::white, white_rows(), 4);
    CHECK_THAT(result.covered, WithinAbs(1.0, 1e-9));
    CHECK(result.total == 1.0);
    CHECK(result.gaps.empty());
    CHECK(result.positions_visited == 10); // 1 + 1 + 2 + 2 + 4
}

TEST_CASE("A repertoire with a gap after 1. e4 e5 2. Nf3 Nc6 covers the rest", "[repertoire]") {
    // To ply 6: Nc6 after 1. e4 e5 2. Nf3 is met with mass 0.6 * 0.7 = 0.42 and has no answer;
    // Nf6 (0.18) is answered by Nxe5 and d6 reaches the depth; the Sicilian's d6 (0.2) is off
    // book and its Nc6 (0.2) is answered by d4 into a position off book. Covered 0.58.
    const auto result = coverage(white_repertoire(), Color::white, white_rows(), 6);
    CHECK_THAT(result.covered, WithinAbs(0.58, 1e-9));
    REQUIRE(result.gaps.size() == 1);
    const Gap& gap = result.gaps.front();
    CHECK(gap.key == after("e4 e5 Nf3 Nc6").key());
    CHECK(gap.fen == after("e4 e5 Nf3 Nc6").fen());
    CHECK(gap.line == line_of({"e2e4", "e7e5", "g1f3", "b8c6"}));
    CHECK_THAT(gap.mass, WithinAbs(0.42, 1e-9));
    CHECK(result.positions_visited == 13); // 1 + 1 + 2 + 2 + 4 + 2 + 1
}

TEST_CASE("A black repertoire starts at the opponent's first move", "[repertoire]") {
    // 1. e4 0.7 answered by c5, then Nf3 0.56 answered by d6 and Nc3 0.14 a gap; 1. d4 0.3
    // answered by d5, then c4 0.15 answered by e6 and Nf3 0.15 a gap. Covered 0.56 + 0.15.
    Rows rows;
    rows.add("", {{"e4", 70}, {"d4", 30}});
    rows.add("e4", {{"c5", 10}});
    rows.add("d4", {{"d5", 10}});
    rows.add("e4 c5", {{"Nf3", 80}, {"Nc3", 20}});
    rows.add("d4 d5", {{"c4", 50}, {"Nf3", 50}});
    rows.add("e4 c5 Nf3", {{"d6", 10}});
    rows.add("e4 c5 Nc3", {{"Nc6", 10}});
    rows.add("d4 d5 c4", {{"e6", 10}});
    rows.add("d4 d5 Nf3", {{"Nf6", 10}});
    const auto chosen =
        repertoire({{"e4", "c5"}, {"d4", "d5"}, {"e4 c5 Nf3", "d6"}, {"d4 d5 c4", "e6"}});
    const auto result = coverage(chosen, Color::black, rows, 4);
    CHECK_THAT(result.covered, WithinAbs(0.71, 1e-9));
    REQUIRE(result.gaps.size() == 2);
    CHECK(result.gaps.at(0).line == line_of({"d2d4", "d7d5", "g1f3"}));
    CHECK_THAT(result.gaps.at(0).mass, WithinAbs(0.15, 1e-9));
    CHECK(result.gaps.at(1).line == line_of({"e2e4", "c7c5", "b1c3"}));
    CHECK_THAT(result.gaps.at(1).mass, WithinAbs(0.14, 1e-9));
}

TEST_CASE("Move orders that transpose merge their mass into one position", "[repertoire]") {
    // 1. Nf3 d5 2. d4 Nf6 and 1. Nf3 Nf6 2. d4 d5 reach one position at ply 4, each with mass
    // 0.5; unanswered there, it is one gap of mass 1.0 with the bytewise-smaller line.
    Rows rows;
    rows.add("", {{"Nf3", 100}});
    rows.add("Nf3", {{"d5", 50}, {"Nf6", 50}});
    rows.add("Nf3 d5", {{"d4", 10}});
    rows.add("Nf3 Nf6", {{"d4", 10}});
    rows.add("Nf3 d5 d4", {{"Nf6", 100}});
    rows.add("Nf3 Nf6 d4", {{"d5", 100}});
    rows.add("Nf3 d5 d4 Nf6", {{"c4", 10}});
    REQUIRE(after("Nf3 d5 d4 Nf6").key() == after("Nf3 Nf6 d4 d5").key());
    const auto chosen = repertoire({{"", "Nf3"}, {"Nf3 d5", "d4"}, {"Nf3 Nf6", "d4"}});
    const auto result = coverage(chosen, Color::white, rows, 8);
    CHECK_THAT(result.covered, WithinAbs(0.0, 1e-9));
    REQUIRE(result.gaps.size() == 1);
    CHECK(result.gaps.front().line == line_of({"g1f3", "d7d5", "d2d4", "g8f6"}));
    CHECK_THAT(result.gaps.front().mass, WithinAbs(1.0, 1e-9));
    CHECK(result.positions_visited == 7); // 1 + 1 + 2 + 2 + 1
}

TEST_CASE("A reply below the minimum mass is not followed and counts covered", "[repertoire]") {
    // 1. a3 is met with mass 10 / 100000 = 0.0001, below 0.001: covered without an answer. With a
    // minimum of 0 it is followed, and is a gap.
    Rows rows;
    rows.add("", {{"e4", 99990}, {"a3", 10}});
    rows.add("e4", {{"c5", 10}});
    rows.add("a3", {{"e5", 10}});
    const auto chosen = repertoire({{"e4", "c5"}});
    const auto truncated = coverage(chosen, Color::black, rows, 2);
    CHECK_THAT(truncated.covered, WithinAbs(1.0, 1e-9));
    CHECK(truncated.gaps.empty());
    const auto followed = coverage(chosen, Color::black, rows, 2, 0.0);
    CHECK_THAT(followed.covered, WithinAbs(0.9999, 1e-9));
    REQUIRE(followed.gaps.size() == 1);
    CHECK(followed.gaps.front().line == line_of({"a2a3"}));
    CHECK_THAT(followed.gaps.front().mass, WithinAbs(0.0001, 1e-12));
}

TEST_CASE("The walk ends covered off book, without replies and at its depth", "[repertoire]") {
    Rows rows;
    rows.add("", {{"e4", 10}});
    rows.add("e4", {});
    // 1. e4 reaches a position in book with no replies.
    CHECK(coverage(repertoire({{"", "e4"}}), Color::white, rows, 10).covered == 1.0);
    // 1. d4 has no row at the start but is followed, into a position off book.
    const auto off_book = coverage(repertoire({{"", "d4"}}), Color::white, rows, 10);
    CHECK(off_book.covered == 1.0);
    CHECK(off_book.positions_visited == 2);
    // At depth 0 nothing is asked, even of an empty repertoire.
    const auto no_depth = coverage(Repertoire{}, Color::white, rows, 0);
    CHECK(no_depth.covered == 1.0);
    CHECK(no_depth.gaps.empty());
    CHECK(no_depth.positions_visited == 1);
    // The start position off book: covered at once.
    CHECK(coverage(Repertoire{}, Color::white, Rows{}, 10).covered == 1.0);
}

TEST_CASE("An unanswered or illegally answered position is a gap", "[repertoire]") {
    Rows rows;
    rows.add("", {{"e4", 10}});
    const auto empty = coverage(Repertoire{}, Color::white, rows, 10);
    REQUIRE(empty.gaps.size() == 1);
    CHECK(empty.gaps.front() == Gap{.key = Position::start().key(),
                                    .fen = Position::start().fen(),
                                    .line = {},
                                    .mass = 1.0});
    CHECK(empty.covered == 0.0);
    Repertoire illegal;
    illegal.emplace(Position::start().key(), "e2e5");
    CHECK(coverage(illegal, Color::white, rows, 10).gaps == empty.gaps);
}

TEST_CASE("Gaps come by mass, then by line", "[repertoire]") {
    Rows rows;
    rows.add("", {{"e4", 50}, {"d4", 50}, {"c4", 20}});
    rows.add("e4", {{"e5", 1}});
    rows.add("d4", {{"d5", 1}});
    rows.add("c4", {{"e5", 1}});
    const auto result = coverage(Repertoire{}, Color::black, rows, 4);
    REQUIRE(result.gaps.size() == 3);
    CHECK(result.gaps.at(0).line == line_of({"d2d4"})); // 50 / 120, before e4 by line
    CHECK(result.gaps.at(1).line == line_of({"e2e4"})); // 50 / 120
    CHECK(result.gaps.at(2).line == line_of({"c2c4"})); // 20 / 120
    CHECK(result.gaps.at(0).mass == result.gaps.at(1).mass);
}

TEST_CASE("The store overload reads the same rows", "[repertoire]") {
    const auto rows = white_rows();
    const auto dir = std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / "repertoire";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto file = dir / "openings.db";
    {
        // SQLite's open flags are ints, combined with | as its API documents.
        const int flags =
            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE; // NOLINT(bugprone-signed-bitwise)
        SQLite::Database db(file, flags);
        db.exec(chesscoach::openings_schema_sql());
        db.exec("INSERT INTO meta VALUES ('schema_version', '1')");
        SQLite::Statement node(db, "INSERT INTO nodes VALUES (?, ?, ?, ?, ?, ?)");
        SQLite::Statement move(db, "INSERT INTO moves VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        for (const auto& [key, child] : rows.all()) {
            if (!child.node) {
                continue;
            }
            node.reset();
            node.bind(1, chesscoach::key_to_sql(key));
            node.bind(2, chesscoach::tier_index(tier));
            node.bind(3, child.node->games);
            node.bind(4, child.node->white);
            node.bind(5, std::int64_t{0});
            node.bind(6, std::int64_t{0});
            node.exec();
            for (const auto& row : child.moves) {
                move.reset();
                move.bind(1, chesscoach::key_to_sql(key));
                move.bind(2, chesscoach::tier_index(tier));
                move.bind(3, row.move);
                move.bind(4, chesscoach::key_to_sql(row.child));
                move.bind(5, row.white);
                move.bind(6, std::int64_t{0});
                move.bind(7, std::int64_t{0});
                move.bind(8, 0);
                move.exec();
            }
        }
    }
    const auto store = chesscoach::OpeningStore::open(file);
    REQUIRE(store.has_value());
    const CoverageConfig config{.max_plies = 6, .min_mass = 0.001};
    CHECK(chesscoach::compute_coverage(*store, white_repertoire(), Color::white, tier, config) ==
          chesscoach::compute_coverage(white_repertoire(), Color::white, rows.lookup(), config));
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

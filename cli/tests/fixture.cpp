// The CLI tests' opening database (fixture.hpp): its rows, written by the writer it shares with the
// app's fixture (core/tests/support/fixture_writer.hpp). The counts are consistent with the
// counting rule of docs/database.md §6.4 at tier 3; tiers 0 and 6 hold only a few rows. Badges are
// set by hand and are not recomputed from §7, unlike the app's: the expected outputs and the spar
// session's Dubious 1... e6 rest on them.
#include "fixture.hpp"

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace chesscoach::cli::test {

namespace {

using chesscoach::testing::FixtureMove;
using chesscoach::testing::FixtureNode;
using chesscoach::testing::FixturePosition;
using chesscoach::testing::FixtureRows;
using chesscoach::testing::move;
using chesscoach::testing::node;
using chesscoach::testing::position;
using chesscoach::testing::unevaluated;

constexpr auto none = std::nullopt;

// Every key with a nodes row has a positions row (§6.2).
std::vector<FixturePosition> positions() {
    return {
        position("", none, none, 19, none, 40, "e2e4"),
        position("e4", "B00", "King's Pawn Game", 30, none, 36, "c7c5"),
        position("e4 c5", "B20", "Sicilian Defense", 35, none, 38, "g1f3"),
        position("e4 e5", "C20", "King's Pawn Game", 40, none, 34, "g1f3"),
        position("d4", "A40", "Queen's Pawn Game", 20, none, 35, "d7d5"),
        position("d4 d5", "D00", "Queen's Pawn Game", 18, none, 33, "c2c4"),
        unevaluated("Nf3", "A04", "Zukertort Opening"),
        unevaluated("Nf3 d5", none, none),
        position("d4 d5 Nf3", "D02", "Queen's Pawn Game: Zukertort Variation", 15, none, 32,
                 "g8f6"),
        position("f3 e5 g4", none, none, none, -1, 99, "d8h4"),
    };
}

std::vector<FixtureNode> nodes() {
    return {
        node("", 0, 9, 1, 10),          node("", 3, 455, 100, 445),
        node("", 6, 45, 30, 25),        node("e4", 3, 260, 60, 280),
        node("e4", 6, 20, 15, 10),      node("e4 c5", 3, 110, 20, 120),
        node("e4 e5", 3, 140, 30, 130), node("d4", 3, 150, 30, 120),
        node("d4 d5", 3, 100, 20, 80),  node("Nf3", 3, 40, 10, 30),
        node("Nf3 d5", 3, 30, 5, 25),   node("d4 d5 Nf3", 3, 80, 15, 65),
        node("f3 e5 g4", 0, 0, 0, 10),
    };
}

std::vector<FixtureMove> moves() {
    return {
        move("", 0, "e2e4", "e4", 5, 1, 6, 2),
        move("", 0, "f2f3", "f3", 4, 0, 4, 2),
        move("", 3, "e2e4", "e4", 260, 60, 280, 3),
        move("", 3, "d2d4", "d4", 150, 30, 120, 3),
        move("", 3, "g1f3", "Nf3", 40, 10, 30, 1),
        move("", 3, "f2f3", "f3", 5, 0, 15, 0),
        move("", 6, "e2e4", "e4", 20, 15, 10, 3),
        move("", 6, "d2d4", "d4", 25, 15, 15, 3),
        move("e4", 3, "e7e5", "e4 e5", 140, 30, 130, 2),
        move("e4", 3, "c7c5", "e4 c5", 110, 20, 120, 3),
        move("e4", 3, "e7e6", "e4 e6", 10, 10, 30, 6),
        move("e4", 6, "c7c5", "e4 c5", 12, 8, 6, 3),
        move("e4 c5", 3, "g1f3", "e4 c5 Nf3", 70, 10, 70, 3),
        move("e4 c5", 3, "b1c3", "e4 c5 Nc3", 40, 10, 50, 2),
        move("d4", 3, "d7d5", "d4 d5", 100, 20, 80, 3),
        move("d4", 3, "g8f6", "d4 Nf6", 50, 10, 40, 2),
        move("d4 d5", 3, "g1f3", "d4 d5 Nf3", 50, 10, 40, 3),
        move("d4 d5", 3, "c2c4", "d4 d5 c4", 50, 10, 40, 3),
        move("Nf3", 3, "d7d5", "Nf3 d5", 30, 5, 25, 3),
        move("Nf3", 3, "g8f6", "Nf3 Nf6", 10, 5, 5, 2),
        move("Nf3 d5", 3, "d2d4", "Nf3 d5 d4", 30, 5, 25, 3),
        move("f3 e5 g4", 0, "d8h4", "f3 e5 g4 Qh4#", 0, 0, 8, 2),
    };
}

// meta as the pipeline writes it (§6.1), at the fixture's knobs.
std::vector<std::pair<std::string_view, std::string_view>> meta() {
    return {
        {"schema_version", "1"},
        {"data_version", "0"},
        {"months", "2013-01"},
        {"min_games", "10"},
        {"max_ply", "8"},
        {"tiers", "1200,1400,1600,1800,2000,2200"},
        {"built_at", "2026-11-03T12:00:00Z"},
        {"pipeline_version", "0.1.0"},
        {"eval_db", "none"},
        {"openings_tsv", "a.tsv,b.tsv,c.tsv,d.tsv,e.tsv"},
        {"games_admitted", "1120"},
        {"admitted_tier_0", "20"},
        {"admitted_tier_1", "0"},
        {"admitted_tier_2", "0"},
        {"admitted_tier_3", "1000"},
        {"admitted_tier_4", "0"},
        {"admitted_tier_5", "0"},
        {"admitted_tier_6", "100"},
        {"games_skipped", "0"},
        {"skipped_malformed", "0"},
        {"skipped_unrated", "0"},
        {"skipped_speed", "0"},
        {"skipped_variant", "0"},
        {"skipped_rating", "0"},
        {"skipped_result", "0"},
        {"skipped_moves", "0"},
    };
}

const FixtureRows& rows() {
    static const FixtureRows fixture{
        .positions = positions(),
        .nodes = nodes(),
        .moves = moves(),
        .meta = meta(),
    };
    return fixture;
}

} // namespace

void write_fixture_db(const std::filesystem::path& path) {
    chesscoach::testing::write_fixture_db(path, rows());
}

FixtureDb::FixtureDb() : chesscoach::testing::FixtureDb("chess-coach-cli-test-", rows()) {}

} // namespace chesscoach::cli::test

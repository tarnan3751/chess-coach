// Repertoire coverage (PLAN.md §5.4, §7 Phase 5; docs/reference/core.md, **Repertoire
// coverage**): how much of what opponents at a tier play against one colour's repertoire it
// answers, to a stated depth, and the gaps ranked by how often they will be met. Pure over the
// store's rows through a lookup, as score.hpp's score_moves is, so it is tested without SQLite;
// the overload on an OpeningStore only reads the rows. Moves are played through Position (the
// children's keys, the gaps' FENs). Floating-point values are summed in a fixed order (plies in
// order, positions by key, children by move text), one IEEE operation per statement, so the
// numbers are the same on every platform.
#pragma once

#include <chesscoach/key.hpp>
#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/score.hpp>
#include <chesscoach/tier.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace chesscoach {

// One colour's repertoire: the chosen move, in UCI, for each position where that colour is to
// move.
using Repertoire = std::unordered_map<Key, std::string>;

struct CoverageConfig {
    int max_plies = 20; // the depth, in plies from the start position
    double min_mass =
        0.001; // a reply whose mass falls below this is not followed: it counts covered
};

// A position the repertoire does not answer where its colour is to move: how often it is met
// (mass) and where (its key, its FEN, the first line reaching it in UCI from the start).
struct Gap {
    Key key = 0;
    std::string fen;
    std::vector<std::string> line;
    double mass = 0.0;

    friend bool operator==(const Gap&, const Gap&) = default;
};

struct Coverage {
    double covered = 0.0;      // the mass whose walk ended covered, truncated replies included
    double total = 1.0;        // the start position's mass
    std::vector<Gap> gaps;     // by mass descending, then by line, move by move bytewise
    int positions_visited = 0; // the (ply, key) pairs walked, transpositions merged

    friend bool operator==(const Coverage&, const Coverage&) = default;
};

// The walk from the start position (docs/reference/core.md, **Repertoire coverage**), with lookup
// giving each position's rows at one tier; only their node and moves are read.
[[nodiscard]] Coverage compute_coverage(const Repertoire& repertoire, Color colour,
                                        const ChildLookup& lookup, const CoverageConfig& config);

// The same, reading the rows from the store at the tier.
[[nodiscard]] Coverage compute_coverage(const OpeningStore& store, const Repertoire& repertoire,
                                        Color colour, Tier tier, const CoverageConfig& config = {});

} // namespace chesscoach

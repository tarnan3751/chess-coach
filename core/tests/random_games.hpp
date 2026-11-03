// Random legal games for core's property tests: a game starts from one of a few positions and is
// one index per ply into the position's legal moves (sorted bytewise, so the same indices give the
// same game everywhere), wrapped around the list's length; it ends early when no move is legal.
#pragma once

#include <chesscoach/position.hpp>

#include <rapidcheck.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace chesscoach::testing {

// The start position, and three from which random play soon castles, promotes (capturing rooks,
// so castling rights fall) or meets en passant: moves that random games from the start position
// rarely reach.
inline rc::Gen<std::string_view> start_fens() {
    return rc::gen::element<std::string_view>(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
        "r3k2r/pppq1ppp/2npbn2/2b1p3/2B1P3/2NPBN2/PPPQ1PPP/R3K2R w KQkq -",
        "r3k2r/1P4P1/8/8/8/8/1p4p1/R3K2R w KQkq -",
        "4k3/pppppppp/8/P1P1P1P1/1p1p1p1p/8/PPPPPPPP/4K3 b - -");
}

// Up to max_plies indices, each below max_index: a small max_index keeps the games to a few
// choices per ply, so different games often reach the same positions.
inline rc::Gen<std::vector<std::size_t>> move_choices(int max_plies, std::size_t max_index) {
    return rc::gen::mapcat(rc::gen::inRange(0, max_plies + 1), [max_index](int plies) {
        return rc::gen::container<std::vector<std::size_t>>(
            static_cast<std::size_t>(plies), rc::gen::inRange<std::size_t>(0, max_index));
    });
}

// The UCI of the move a choice picks in a position, or an empty string when none is legal.
inline std::string chosen_move(const Position& position, std::size_t choice) {
    const auto moves = position.legal_moves_uci();
    return moves.empty() ? std::string() : moves.at(choice % moves.size());
}

} // namespace chesscoach::testing

// Property tests for chesscoach/position.hpp (core; property): over random legal games, printing
// a position and parsing it back gives the same normalized FEN and key (docs/database.md §5.1),
// and every legal move's SAN and UCI round-trip through chess-library, playing a move by its SAN,
// by its UCI or by play_san_fast gives the same move and child.
#include <chesscoach/position.hpp>

#include "random_games.hpp"

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

using chesscoach::Position;
using chesscoach::testing::chosen_move;
using chesscoach::testing::move_choices;
using chesscoach::testing::start_fens;

TEST_CASE("parse(print(position)) is the normalized FEN with the same key",
          "[position][property]") {
    REQUIRE(rc::check("from_fen(fen()) round trip", [] {
        const auto choices = *move_choices(60, 256);
        auto position = Position::from_fen(*start_fens()).value();
        for (const auto choice : choices) {
            const auto fen = position.fen();
            const auto parsed = Position::from_fen(fen);
            RC_ASSERT(parsed.has_value());
            RC_ASSERT(parsed->fen() == fen);
            RC_ASSERT(parsed->key() == position.key());
            RC_ASSERT(parsed->side_to_move() == position.side_to_move());
            RC_ASSERT(parsed->legal_moves_uci() == position.legal_moves_uci());
            const auto six = Position::from_fen(fen + " 17 42");
            RC_ASSERT(six.has_value());
            RC_ASSERT(six->fen() == fen);
            const auto uci = chosen_move(position, choice);
            if (uci.empty()) {
                break;
            }
            RC_ASSERT(position.play_uci(uci).has_value());
        }
    }));
}

TEST_CASE("SAN and UCI round-trip for every legal move", "[position][property]") {
    REQUIRE(rc::check("SAN and UCI round trips", [] {
        const auto choices = *move_choices(60, 256);
        auto position = Position::from_fen(*start_fens()).value();
        for (const auto choice : choices) {
            for (const auto& uci : position.legal_moves_uci()) {
                const auto san = position.san_of(uci);
                RC_ASSERT(san.has_value());
                const auto back = position.uci_of(*san);
                RC_ASSERT(back.has_value());
                RC_ASSERT(*back == uci);
            }
            const auto uci = chosen_move(position, choice);
            if (uci.empty()) {
                break;
            }
            const auto san = position.san_of(uci).value();
            auto by_san = position;
            auto fast = position;
            const auto played_san = by_san.play_san(san);
            const auto played_fast = fast.play_san_fast(san);
            const auto played_uci = position.play_uci(uci);
            RC_ASSERT(played_uci.has_value());
            RC_ASSERT(played_san.has_value());
            RC_ASSERT(*played_san == *played_uci);
            RC_ASSERT(played_uci->uci == uci);
            RC_ASSERT(played_uci->san == san);
            RC_ASSERT(played_fast.has_value());
            RC_ASSERT(played_fast->uci == uci);
            RC_ASSERT(played_fast->child == played_uci->child);
            RC_ASSERT(by_san.key() == position.key());
            RC_ASSERT(fast.fen() == position.fen());
        }
    }));
}

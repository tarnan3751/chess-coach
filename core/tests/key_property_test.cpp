// Property tests for the key (core; property; docs/database.md §5.2): over random legal games,
// the key Position updates incrementally on every move equals the key computed from scratch from
// its normalized FEN, and two positions have equal keys exactly when chess-library's own hash,
// which also ignores the counters and keeps en passant only when a capture is legal, is equal.
#include <chesscoach/key.hpp>
#include <chesscoach/position.hpp>

#include "random_games.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chess-library/chess.hpp>
#include <rapidcheck.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using chesscoach::Key;
using chesscoach::Position;
using chesscoach::testing::chosen_move;
using chesscoach::testing::move_choices;
using chesscoach::testing::start_fens;

namespace {

// Plays a UCI move on a chess-library board the way Position does: the library's own legal move
// of that spelling, made with makeMove<true>.
void play_on_board(chess::Board& board, const std::string& uci) {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    for (const auto& move : moves) {
        if (chess::uci::moveToUci(move) == uci) {
            board.makeMove<true>(move);
            return;
        }
    }
    RC_FAIL("the board has no legal move " + uci);
}

} // namespace

TEST_CASE("The incremental key equals the key from scratch at every ply", "[key][property]") {
    REQUIRE(rc::check("incremental key == from-scratch key", [] {
        const auto fen = *start_fens();
        const auto choices = *move_choices(60, 256);
        auto position = Position::from_fen(fen).value();
        bool en_passant = false;
        bool castled = false;
        bool promoted = false;
        for (const auto choice : choices) {
            const auto uci = chosen_move(position, choice);
            if (uci.empty()) {
                break;
            }
            const auto played = position.play_uci(uci);
            RC_ASSERT(played.has_value());
            RC_ASSERT(played->child == position.key());
            const auto printed = position.fen();
            const auto parsed = Position::from_fen(printed);
            RC_ASSERT(parsed.has_value());
            RC_ASSERT(parsed->key() == position.key());
            en_passant = en_passant || printed.back() != '-';
            castled = castled || played->san.starts_with("O-O");
            promoted = promoted || played->uci.size() == 5;
        }
        RC_TAG(en_passant ? "en passant kept" : "-", castled ? "castled" : "-",
               promoted ? "promoted" : "-");
    }));
}

TEST_CASE("Keys are equal exactly when chess-library's hashes are", "[key][property]") {
    REQUIRE(rc::check("library hash equality iff key equality", [] {
        // A batch of games, mostly with few choices per ply, so that positions recur across the
        // batch; with many choices, distinct positions must keep distinct keys.
        const auto fen = *start_fens();
        const auto width = *rc::gen::element<std::size_t>(3, 4, 6, 256);
        const auto games =
            *rc::gen::container<std::vector<std::vector<std::size_t>>>(8, move_choices(40, width));
        std::map<std::uint64_t, Key> key_of_hash;
        std::map<Key, std::uint64_t> hash_of_key;
        std::size_t repeats = 0;
        const auto record = [&](const Position& position, const chess::Board& board) {
            const auto [by_hash, new_hash] = key_of_hash.emplace(board.hash(), position.key());
            const auto [by_key, new_key] = hash_of_key.emplace(position.key(), board.hash());
            RC_ASSERT(by_hash->second == position.key());
            RC_ASSERT(by_key->second == board.hash());
            RC_ASSERT(new_hash == new_key);
            repeats += new_hash ? 0 : 1;
        };
        for (const auto& choices : games) {
            auto position = Position::from_fen(fen).value();
            chess::Board board{fen};
            record(position, board);
            for (const auto choice : choices) {
                const auto uci = chosen_move(position, choice);
                if (uci.empty()) {
                    break;
                }
                RC_ASSERT(position.play_uci(uci).has_value());
                play_on_board(board, uci);
                record(position, board);
            }
        }
        // The start position recurs once per game after the first.
        RC_TAG(repeats + 1 > games.size() ? "positions recur" : "only the start recurs");
    }));
}

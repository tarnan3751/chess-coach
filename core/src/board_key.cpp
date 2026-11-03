// The key of a chess-library board from scratch (board_key.hpp).
#include "board_key.hpp"

namespace chesscoach {

Key key_from_scratch(const chess::Board& board) {
    using Side = chess::Board::CastlingRights::Side;
    Key key = 0;
    auto occupied = board.occ();
    while (!occupied.empty()) {
        const int square = occupied.pop();
        key ^= zobrist::piece_square(static_cast<int>(board.at(chess::Square(square))), square);
    }
    if (board.sideToMove() == chess::Color::BLACK) {
        key ^= zobrist::black_to_move();
    }
    const auto rights = board.castlingRights();
    if (rights.has(chess::Color::WHITE, Side::KING_SIDE)) {
        key ^= zobrist::castling(zobrist::CastlingRight::white_king_side);
    }
    if (rights.has(chess::Color::WHITE, Side::QUEEN_SIDE)) {
        key ^= zobrist::castling(zobrist::CastlingRight::white_queen_side);
    }
    if (rights.has(chess::Color::BLACK, Side::KING_SIDE)) {
        key ^= zobrist::castling(zobrist::CastlingRight::black_king_side);
    }
    if (rights.has(chess::Color::BLACK, Side::QUEEN_SIDE)) {
        key ^= zobrist::castling(zobrist::CastlingRight::black_queen_side);
    }
    if (board.enpassantSq() != chess::Square::NO_SQ) {
        key ^= zobrist::en_passant_file(static_cast<int>(board.enpassantSq().file()));
    }
    return key;
}

} // namespace chesscoach

// Squares and the piece placement (squares.hpp).
#include "squares.hpp"

#include <cstddef>

namespace chesscoach::app {

std::string square_name(int square) {
    return {static_cast<char>('a' + file_of(square)), static_cast<char>('1' + rank_of(square))};
}

std::optional<int> square_from_name(std::string_view name) {
    if (name.size() != 2) {
        return std::nullopt;
    }
    const char file = name.front();
    const char rank = name.back();
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') {
        return std::nullopt;
    }
    return square_of(file - 'a', rank - '1');
}

std::optional<UciMove> parse_uci(std::string_view uci) {
    if (uci.size() != 4 && uci.size() != 5) {
        return std::nullopt;
    }
    const auto from = square_from_name(uci.substr(0, 2));
    const auto to = square_from_name(uci.substr(2, 2));
    if (!from || !to) {
        return std::nullopt;
    }
    return UciMove{.from = *from, .to = *to, .promotion = uci.size() == 5 ? uci.back() : char{0}};
}

Placement placement_of(const Position& position) {
    Placement placement{};
    const std::string fen = position.fen();
    // The FEN lists rank 8 first, each rank from the a-file.
    int rank = 7;
    int file = 0;
    for (const char c : fen) {
        if (c == ' ') {
            break;
        }
        if (c == '/') {
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else if (rank >= 0 && file < 8) {
            placement.at(static_cast<std::size_t>(square_of(file, rank))) = c;
            ++file;
        }
    }
    return placement;
}

std::optional<int> king_square(const Placement& placement, Color side) {
    const char king = side == Color::white ? 'K' : 'k';
    for (int square = 0; square < square_count; ++square) {
        if (placement.at(static_cast<std::size_t>(square)) == king) {
            return square;
        }
    }
    return std::nullopt;
}

} // namespace chesscoach::app

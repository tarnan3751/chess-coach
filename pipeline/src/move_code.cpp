// encode_move and decode_move (move_code.hpp).
#include "move_code.hpp"

namespace chesscoach::pipeline {

namespace {

constexpr std::string_view promotions = " nbrq";

std::optional<unsigned> square(std::string_view text) {
    if (text.size() != 2 || text.at(0) < 'a' || text.at(0) > 'h' || text.at(1) < '1' ||
        text.at(1) > '8') {
        return std::nullopt;
    }
    return static_cast<unsigned>(text.at(0) - 'a') + (8U * static_cast<unsigned>(text.at(1) - '1'));
}

void append_square(std::string& out, unsigned sq) {
    out += static_cast<char>('a' + static_cast<char>(sq % 8U));
    out += static_cast<char>('1' + static_cast<char>(sq / 8U));
}

} // namespace

std::optional<MoveCode> encode_move(std::string_view uci) {
    if (uci.size() != 4 && uci.size() != 5) {
        return std::nullopt;
    }
    const auto from = square(uci.substr(0, 2));
    const auto to = square(uci.substr(2, 2));
    if (!from || !to) {
        return std::nullopt;
    }
    unsigned promotion = 0;
    if (uci.size() == 5) {
        const std::size_t found = promotions.find(uci.at(4));
        if (found == std::string_view::npos || found == 0) {
            return std::nullopt;
        }
        promotion = static_cast<unsigned>(found);
    }
    return static_cast<MoveCode>(*from | (*to << 6U) | (promotion << 12U));
}

std::string decode_move(MoveCode code) {
    std::string uci;
    const auto bits = static_cast<unsigned>(code);
    append_square(uci, bits & 63U);
    append_square(uci, (bits >> 6U) & 63U);
    const unsigned promotion = (bits >> 12U) & 7U;
    if (promotion != 0) {
        uci += promotions.at(promotion);
    }
    return uci;
}

} // namespace chesscoach::pipeline

// Position over chess-library (chesscoach/position.hpp). chess-library is lax about its input and
// asserts or reads out of bounds on some of what it accepts, so this file checks every FEN and
// SAN before the library sees it (docs/reference/core.md, **chess-library and the wrapper**):
// the FEN's syntax, its kings, its castling rights and en passant target; the SAN's syntax. Moves
// are made with makeMove<true>, which keeps an en passant target only when a capture is legal.
#include <chesscoach/position.hpp>

#include "board_key.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chesscoach {

struct Position::Impl {
    chess::Board board;
    Key key = 0;
};

namespace {

using Side = chess::Board::CastlingRights::Side;

constexpr std::string_view piece_letters = "PNBRQKpnbrqk";

bool is_file(char c) { return c >= 'a' && c <= 'h'; }
bool is_rank(char c) { return c >= '1' && c <= '8'; }
bool is_digits(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

// The fields of a FEN separated by single spaces; an empty field (a doubled, leading or trailing
// space) makes the whole text malformed.
std::optional<std::vector<std::string_view>> split_fields(std::string_view text) {
    std::vector<std::string_view> fields;
    while (true) {
        const auto space = text.find(' ');
        const auto field = text.substr(0, space);
        if (field.empty()) {
            return std::nullopt;
        }
        fields.push_back(field);
        if (space == std::string_view::npos) {
            return fields;
        }
        text.remove_prefix(space + 1);
    }
}

// The placement field as 64 squares (a1 = 0), each a piece letter or '.'; eight ranks of eight.
std::optional<std::array<char, 64>> parse_placement(std::string_view field) {
    std::array<char, 64> squares{};
    squares.fill('.');
    int rank = 7;
    int file = 0;
    for (const char c : field) {
        if (c == '/') {
            if (file != 8 || rank == 0) {
                return std::nullopt;
            }
            --rank;
            file = 0;
        } else if (is_rank(c)) {
            file += c - '0';
            if (file > 8) {
                return std::nullopt;
            }
        } else if (piece_letters.find(c) != std::string_view::npos && file < 8) {
            const int square = (rank * 8) + file;
            squares.at(static_cast<std::size_t>(square)) = c;
            ++file;
        } else {
            return std::nullopt;
        }
    }
    if (rank != 0 || file != 8) {
        return std::nullopt;
    }
    return squares;
}

char at(const std::array<char, 64>& squares, int square) {
    return squares.at(static_cast<std::size_t>(square));
}

// A castling field: '-' or distinct letters of KQkq. Each right needs its king and its rook on
// their home squares, or chess-library would grant it with whatever rook stands on that side.
std::optional<FenError> check_castling(std::string_view field, const std::array<char, 64>& sq) {
    if (field == "-") {
        return std::nullopt;
    }
    std::string seen;
    for (const char c : field) {
        if (std::string_view("KQkq").find(c) == std::string_view::npos ||
            seen.find(c) != std::string::npos) {
            return FenError::malformed;
        }
        seen += c;
    }
    for (const char c : field) {
        const bool home = (c == 'K' && at(sq, 4) == 'K' && at(sq, 7) == 'R') ||
                          (c == 'Q' && at(sq, 4) == 'K' && at(sq, 0) == 'R') ||
                          (c == 'k' && at(sq, 60) == 'k' && at(sq, 63) == 'r') ||
                          (c == 'q' && at(sq, 60) == 'k' && at(sq, 56) == 'r');
        if (!home) {
            return FenError::illegal;
        }
    }
    return std::nullopt;
}

// A placement field from 64 squares (a1 = 0), each a piece letter or '.'.
std::string placement_of(const std::array<char, 64>& squares) {
    std::string placement;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const char c = at(squares, (rank * 8) + file);
            if (c == '.') {
                ++empty;
                continue;
            }
            if (empty > 0) {
                placement += static_cast<char>('0' + empty);
                empty = 0;
            }
            placement += c;
        }
        if (empty > 0) {
            placement += static_cast<char>('0' + empty);
        }
        if (rank > 0) {
            placement += '/';
        }
    }
    return placement;
}

// The en passant field as the library should see it: '-' unless the double push it records was
// possible. A pawn of the side not to move must stand where the push put it, with the target and
// the square it passed empty; chess-library checks the capture's legality but not that the pawn
// is there, so a target without one would give a capture of an empty square. And the position
// before the push must have been legal: with the pawn back on its origin square and the pushing
// side to move, the side now to move's king must not be attacked, which chess-library's own check
// (pins and the lines the captured pawn's removal opens) does not look at. Without a possible
// push there is no legal capture, and the contract drops the target (§5.1).
std::string_view normalize_en_passant(std::string_view field, bool white_to_move,
                                      const std::array<char, 64>& squares) {
    if (field == "-") {
        return field;
    }
    const int file = field.at(0) - 'a';
    const int pushed = white_to_move ? 32 + file : 24 + file;
    const int origin = white_to_move ? 48 + file : 8 + file;
    const bool shaped = white_to_move
                            ? (field.at(1) == '6' && at(squares, pushed) == 'p' &&
                               at(squares, 40 + file) == '.' && at(squares, origin) == '.')
                            : (field.at(1) == '3' && at(squares, pushed) == 'P' &&
                               at(squares, 16 + file) == '.' && at(squares, origin) == '.');
    if (!shaped) {
        return "-";
    }
    auto before = squares;
    std::swap(before.at(static_cast<std::size_t>(pushed)),
              before.at(static_cast<std::size_t>(origin)));
    chess::Board board;
    if (!board.setFen(placement_of(before) + (white_to_move ? " b - -" : " w - -"))) {
        return "-";
    }
    const auto capturer = white_to_move ? chess::Color::WHITE : chess::Color::BLACK;
    return board.isAttacked(board.kingSq(capturer), ~capturer) ? std::string_view("-") : field;
}

// SAN as Lichess and the chess-openings files write it, and 0-0 for castling: the move without
// its check suffix and annotations, or nullopt when the text is not SAN. chess-library's parser
// reads past the end of some malformed strings, so only text that passes reaches it.
std::optional<std::string_view> san_body(std::string_view san) {
    int marks = 0;
    while (!san.empty() && marks < 2 && (san.back() == '!' || san.back() == '?')) {
        san.remove_suffix(1);
        ++marks;
    }
    if (!san.empty() && (san.back() == '+' || san.back() == '#')) {
        san.remove_suffix(1);
    }
    if (san == "O-O" || san == "O-O-O" || san == "0-0" || san == "0-0-0") {
        return san;
    }
    if (san.size() < 2 || !is_file(san.at(san.size() - 2)) || !is_rank(san.back())) {
        // Every other move ends on its target square, but for a promotion's suffix.
        if (san.size() < 4 || san.at(san.size() - 2) != '=' ||
            std::string_view("QRBN").find(san.back()) == std::string_view::npos) {
            return std::nullopt;
        }
        const auto pawn = san.substr(0, san.size() - 2);
        const bool push = pawn.size() == 2 && is_file(pawn.at(0)) && is_rank(pawn.at(1));
        const bool capture = pawn.size() == 4 && is_file(pawn.at(0)) && pawn.at(1) == 'x' &&
                             is_file(pawn.at(2)) && is_rank(pawn.at(3));
        return (push || capture) ? std::optional(san) : std::nullopt;
    }
    const auto prefix = san.substr(0, san.size() - 2);
    if (prefix.empty()) {
        return san; // a pawn push
    }
    if (is_file(prefix.at(0))) {
        return (prefix.size() == 2 && prefix.at(1) == 'x') ? std::optional(san) : std::nullopt;
    }
    if (std::string_view("KQRBN").find(prefix.at(0)) == std::string_view::npos) {
        return std::nullopt;
    }
    auto from = prefix.substr(1);
    if (!from.empty() && from.back() == 'x') {
        from.remove_suffix(1);
    }
    const bool disambiguation =
        from.empty() || (from.size() == 1 && (is_file(from.at(0)) || is_rank(from.at(0)))) ||
        (from.size() == 2 && is_file(from.at(0)) && is_rank(from.at(1)));
    return disambiguation ? std::optional(san) : std::nullopt;
}

bool is_uci_syntax(std::string_view uci) {
    const bool squares = uci.size() >= 4 && is_file(uci.at(0)) && is_rank(uci.at(1)) &&
                         is_file(uci.at(2)) && is_rank(uci.at(3));
    return (squares && uci.size() == 4) ||
           (squares && uci.size() == 5 &&
            std::string_view("qrbn").find(uci.at(4)) != std::string_view::npos);
}

// The legal move whose UCI is the text, matched against the library's own move list and
// spelling, so castling is only the king's two-square move and a promotion needs its letter.
tl::expected<chess::Move, MoveError> find_uci(const chess::Board& board, std::string_view uci) {
    if (!is_uci_syntax(uci)) {
        return tl::unexpected(MoveError::malformed);
    }
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    for (const auto& move : moves) {
        if (chess::uci::moveToUci(move) == uci) {
            return move;
        }
    }
    return tl::unexpected(MoveError::illegal);
}

tl::expected<chess::Move, MoveError> find_san(const chess::Board& board, std::string_view san) {
    const auto body = san_body(san);
    if (!body) {
        return tl::unexpected(MoveError::malformed);
    }
    chess::Move move(chess::Move::NO_MOVE);
    try {
        move = chess::uci::parseSan(board, *body);
    } catch (const chess::uci::AmbiguousMoveError&) {
        return tl::unexpected(MoveError::ambiguous);
    } catch (const chess::uci::SanParseError&) {
        return tl::unexpected(MoveError::illegal);
    }
    if (move == chess::Move(chess::Move::NO_MOVE)) { // parseSan's answer to an empty string
        return tl::unexpected(MoveError::illegal);
    }
    return move;
}

// The castling values of the rights held before a move and not after it, which are all that a
// move can change.
Key lost_rights(const chess::Board::CastlingRights& before,
                const chess::Board::CastlingRights& after) {
    Key key = 0;
    const auto lost = [&](chess::Color color, Side side) {
        return before.has(color, side) && !after.has(color, side);
    };
    if (lost(chess::Color::WHITE, Side::KING_SIDE)) {
        key ^= zobrist::castling(zobrist::CastlingRight::white_king_side);
    }
    if (lost(chess::Color::WHITE, Side::QUEEN_SIDE)) {
        key ^= zobrist::castling(zobrist::CastlingRight::white_queen_side);
    }
    if (lost(chess::Color::BLACK, Side::KING_SIDE)) {
        key ^= zobrist::castling(zobrist::CastlingRight::black_king_side);
    }
    if (lost(chess::Color::BLACK, Side::QUEEN_SIDE)) {
        key ^= zobrist::castling(zobrist::CastlingRight::black_queen_side);
    }
    return key;
}

Key value(chess::Piece piece, chess::Square square) {
    return zobrist::piece_square(static_cast<int>(piece), square.index());
}

Key en_passant(chess::Square target) {
    return target == chess::Square::NO_SQ
               ? 0
               : zobrist::en_passant_file(static_cast<int>(target.file()));
}

// Makes a legal move and updates the key by the values the move changes: the pieces it moves,
// captures and promotes, the side to move, the rights it loses, and the old and new en passant
// targets.
void make_move(chess::Board& board, Key& position_key, chess::Move move) {
    const auto from = move.from();
    const auto to = move.to();
    const auto moved = board.at(from);
    Key key = position_key ^ zobrist::black_to_move() ^ en_passant(board.enpassantSq());
    const auto rights_before = board.castlingRights();

    if (move.typeOf() == chess::Move::CASTLING) {
        // King takes rook: the king and the rook land on the standard castling squares.
        const bool king_side = to > from;
        const auto color = board.sideToMove();
        const auto rook = board.at(to);
        key ^= value(moved, from) ^
               value(moved, chess::Square::castling_king_square(king_side, color));
        key ^= value(rook, to) ^ value(rook, chess::Square::castling_rook_square(king_side, color));
    } else if (move.typeOf() == chess::Move::ENPASSANT) {
        const chess::Square captured(to.file(), from.rank());
        key ^= value(moved, from) ^ value(moved, to) ^ value(board.at(captured), captured);
    } else {
        const auto placed = move.typeOf() == chess::Move::PROMOTION
                                ? chess::Piece(move.promotionType(), board.sideToMove())
                                : moved;
        key ^= value(moved, from) ^ value(placed, to);
        if (board.at(to) != chess::Piece::NONE) {
            key ^= value(board.at(to), to);
        }
    }

    board.makeMove<true>(move);
    key ^= lost_rights(rights_before, board.castlingRights()) ^ en_passant(board.enpassantSq());
    position_key = key;
    assert(position_key == key_from_scratch(board));
}

PlayedMove play(chess::Board& board, Key& key, chess::Move move, bool with_san) {
    PlayedMove played{
        .uci = chess::uci::moveToUci(move),
        .san = with_san ? chess::uci::moveToSan(board, move) : std::string(),
        .child = 0,
    };
    make_move(board, key, move);
    played.child = key;
    return played;
}

} // namespace

Position::Position(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Position::Position(const Position& other) : impl_(std::make_unique<Impl>(*other.impl_)) {}

Position& Position::operator=(const Position& other) {
    if (this != &other) {
        impl_ = std::make_unique<Impl>(*other.impl_);
    }
    return *this;
}

Position::Position(Position&& other) noexcept = default;
Position& Position::operator=(Position&& other) noexcept = default;
Position::~Position() = default;

Position Position::start() {
    auto impl = std::make_unique<Impl>();
    impl->key = key_from_scratch(impl->board);
    return Position(std::move(impl));
}

tl::expected<Position, FenError> Position::from_fen(std::string_view fen) {
    const auto fields = split_fields(fen);
    if (!fields || (fields->size() != 4 && fields->size() != 6)) {
        return tl::unexpected(FenError::malformed);
    }
    const auto& f = *fields;
    const auto squares = parse_placement(f.at(0));
    const bool side_ok = f.at(1) == "w" || f.at(1) == "b";
    const bool en_passant_ok = f.at(3) == "-" || (f.at(3).size() == 2 && is_file(f.at(3).at(0)) &&
                                                  (f.at(3).at(1) == '3' || f.at(3).at(1) == '6'));
    const bool counters_ok = f.size() == 4 || (is_digits(f.at(4)) && is_digits(f.at(5)));
    if (!squares || !side_ok || !en_passant_ok || !counters_ok) {
        return tl::unexpected(FenError::malformed);
    }
    if (std::ranges::count(*squares, 'K') != 1 || std::ranges::count(*squares, 'k') != 1) {
        return tl::unexpected(FenError::kings);
    }
    if (const auto castling = check_castling(f.at(2), *squares)) {
        return tl::unexpected(*castling);
    }
    const auto back_ranks = [&](int square) {
        const char c = at(*squares, square);
        return c == 'P' || c == 'p';
    };
    for (int file = 0; file < 8; ++file) {
        if (back_ranks(file) || back_ranks(56 + file)) {
            return tl::unexpected(FenError::illegal);
        }
    }

    const bool white_to_move = f.at(1) == "w";
    std::string normalized(f.at(0));
    normalized += ' ';
    normalized += f.at(1);
    normalized += ' ';
    normalized += f.at(2);
    normalized += ' ';
    normalized += normalize_en_passant(f.at(3), white_to_move, *squares);

    auto impl = std::make_unique<Impl>();
    if (!impl->board.setFen(normalized)) {
        return tl::unexpected(FenError::malformed);
    }
    const auto mover = impl->board.sideToMove();
    if (impl->board.isAttacked(impl->board.kingSq(~mover), mover)) {
        return tl::unexpected(FenError::illegal);
    }
    impl->key = key_from_scratch(impl->board);
    return Position(std::move(impl));
}

Key Position::key() const noexcept { return impl_->key; }

std::string Position::fen() const { return impl_->board.getFen(false); }

Color Position::side_to_move() const noexcept {
    return impl_->board.sideToMove() == chess::Color::WHITE ? Color::white : Color::black;
}

bool Position::in_check() const { return impl_->board.inCheck(); }

std::vector<std::string> Position::legal_moves_uci() const {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, impl_->board);
    std::vector<std::string> ucis;
    ucis.reserve(static_cast<std::size_t>(moves.size()));
    for (const auto& move : moves) {
        ucis.push_back(chess::uci::moveToUci(move));
    }
    std::ranges::sort(ucis);
    return ucis;
}

tl::expected<PlayedMove, MoveError> Position::play_san(std::string_view san) {
    return find_san(impl_->board, san).map([&](chess::Move move) {
        return play(impl_->board, impl_->key, move, true);
    });
}

tl::expected<PlayedMove, MoveError> Position::play_san_fast(std::string_view san) {
    return find_san(impl_->board, san).map([&](chess::Move move) {
        return play(impl_->board, impl_->key, move, false);
    });
}

tl::expected<PlayedMove, MoveError> Position::play_uci(std::string_view uci) {
    return find_uci(impl_->board, uci).map([&](chess::Move move) {
        return play(impl_->board, impl_->key, move, true);
    });
}

tl::expected<std::string, MoveError> Position::san_of(std::string_view uci) const {
    return find_uci(impl_->board, uci).map([&](chess::Move move) {
        return chess::uci::moveToSan(impl_->board, move);
    });
}

tl::expected<std::string, MoveError> Position::uci_of(std::string_view san) const {
    return find_san(impl_->board, san).map([](chess::Move move) {
        return chess::uci::moveToUci(move);
    });
}

} // namespace chesscoach

// Position: a chess position over chess-library's board, held behind a pointer so that no public
// header includes chess.hpp (docs/reference/core.md, **Position**). The rules (move generation,
// legality, SAN, UCI) come from chess-library and nowhere else; the key is ours (key.hpp),
// computed from scratch when a FEN is parsed and incrementally on every move, the two asserted
// equal in debug builds. fen() is the normalized FEN of docs/database.md §5.1, and moves are UCI
// as §5.3 writes them. Errors are values; no I/O.
#pragma once

#include <chesscoach/key.hpp>

#include <tl/expected.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace chesscoach {

enum class Color : std::uint8_t { white, black };

enum class FenError : std::uint8_t {
    malformed, // not four or six fields separated by single spaces, or a field that does not parse
    kings,     // a side without exactly one king
    illegal,   // a castling right without its king and rook on their home squares, the side not to
               // move in check, or a pawn on the first or last rank
};

enum class MoveError : std::uint8_t {
    malformed, // not SAN (play_san, uci_of) or not UCI (play_uci, san_of) at all
    illegal,   // well formed, but no legal move in this position matches it
    ambiguous, // SAN that matches more than one legal move, such as e8 with no promotion piece
};

// A move just played: its UCI, its SAN as chess-library writes it in the position before the move
// (check and mate suffixes included; empty from play_san_fast), and the key after it.
struct PlayedMove {
    std::string uci;
    std::string san;
    Key child = 0;

    friend bool operator==(const PlayedMove&, const PlayedMove&) = default;
};

class Position {
  public:
    // The standard start position.
    [[nodiscard]] static Position start();

    // Four fields, or six with the move counters, which are checked to be decimal and ignored. An
    // en passant target from which the side to move has no legal capture is dropped (§5.1).
    [[nodiscard]] static tl::expected<Position, FenError> from_fen(std::string_view fen);

    Position(const Position& other);
    Position& operator=(const Position& other);
    // A moved-from Position may only be assigned to or destroyed.
    Position(Position&& other) noexcept;
    Position& operator=(Position&& other) noexcept;
    ~Position();

    [[nodiscard]] Key key() const noexcept;
    [[nodiscard]] std::string fen() const;
    [[nodiscard]] Color side_to_move() const noexcept;
    [[nodiscard]] bool in_check() const;

    // Every legal move in UCI, sorted bytewise.
    [[nodiscard]] std::vector<std::string> legal_moves_uci() const;

    // Plays a move given in SAN (O-O and 0-0 both castle; a trailing + or # and up to two of ! and
    // ? are ignored) or in UCI. On an error the position is unchanged.
    [[nodiscard]] tl::expected<PlayedMove, MoveError> play_san(std::string_view san);
    [[nodiscard]] tl::expected<PlayedMove, MoveError> play_uci(std::string_view uci);

    // play_san without writing the move's SAN back, the pipeline's path through every game: the
    // result's san is empty.
    [[nodiscard]] tl::expected<PlayedMove, MoveError> play_san_fast(std::string_view san);

    // The SAN of a legal move given in UCI, and the UCI of one given in SAN, without playing it.
    [[nodiscard]] tl::expected<std::string, MoveError> san_of(std::string_view uci) const;
    [[nodiscard]] tl::expected<std::string, MoveError> uci_of(std::string_view san) const;

  private:
    struct Impl;
    explicit Position(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace chesscoach

// A repertoire from a PGN file (PLAN.md §7 Phase 5; docs/reference/core.md, **PGN import**): every
// game and every variation is replayed through Position, and wherever the repertoire's colour is
// to move, the move played there becomes that position's choice, the first in document order
// winning. Reads only the stream it is given; the profile store applies the choices.
#pragma once

#include <chesscoach/position.hpp>
#include <chesscoach/repertoire.hpp>

#include <tl/expected.hpp>

#include <cstdint>
#include <istream>
#include <vector>

namespace chesscoach {

struct PgnImport {
    std::vector<RepertoireChoice> choices; // in document order, one per position
    int games = 0;                         // the games read
    int variations = 0;                    // the parenthesized variations read
    int conflicts = 0;  // later moves for an already chosen position that differ from its choice
    int unparsable = 0; // lines ended by a move that does not play (or a [FEN] that does not parse)

    friend bool operator==(const PgnImport&, const PgnImport&) = default;
};

enum class PgnImportError : std::uint8_t {
    malformed_header, // a header without its closing bracket or quote
    too_long,         // a header or a move longer than the parser reads
};

// Each game begins with its header, so movetext before any header is no game, and starts at the
// start position, or at its [FEN] header's position. A variation starts
// from the position before the move it replaces and returns to its line at its end. A NAG ($n), a
// comment and the result are not moves. A move that does not play ends its line, down to that
// line's closing parenthesis or the game's end, and counts once in unparsable; so does a game
// whose [FEN] does not parse.
[[nodiscard]] tl::expected<PgnImport, PgnImportError> import_repertoire_pgn(std::istream& in,
                                                                            Color colour);

} // namespace chesscoach

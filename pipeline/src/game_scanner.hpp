// GameScanner: one game's PGN text to the positions and moves it contributes, both passes alike
// (docs/database.md §3, §6.4). chess-library's StreamParser reads the text, one parser per game
// so a parser error marks that game alone `malformed`; the §3 header checks run when the moves
// start; each SAN token, its annotation glyphs stripped and NAGs ignored, is replayed through
// core's Position up to max_ply plies, after which the rest of the game is skipped unparsed. A
// game whose moves before the cap do not all replay counts as `moves` and contributes nothing.
// Pass two asks for the FEN of the positions it has none for through fen_wanted.
#pragma once

#include <chesscoach/position.hpp>
#include <chesscoach/tier.hpp>

#include "game_filter.hpp"
#include "move_code.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chesscoach::pipeline {

// What an admitted game contributes: its tier and result, the keys of its positions at plies
// 0..n (n at most max_ply), and the move from each of the first n.
struct ScannedGame {
    std::uint8_t tier = 0;
    GameResult result = GameResult::draw;
    std::vector<Key> keys;                         // plies 0..n
    std::vector<MoveCode> moves;                   // moves[i] leads from keys[i] to keys[i + 1]
    std::vector<std::pair<Key, std::string>> fens; // the normalized FENs fen_wanted asked for
};

// The games a scanner read, admitted per tier and skipped per §3 reason.
struct ScanCounts {
    std::array<std::uint64_t, static_cast<std::size_t>(tier_count)> admitted{};
    SkipCounts skipped;

    [[nodiscard]] std::uint64_t admitted_total() const;
    ScanCounts& operator+=(const ScanCounts& other);
    friend bool operator==(const ScanCounts&, const ScanCounts&) = default;
};

// Asked once per visit of an admitted game: whether the scanner should record the position's
// FEN. Absent: never.
using FenWanted = std::function<bool(std::uint8_t tier, Key key)>;

class GameScanner {
  public:
    GameScanner(std::uint32_t max_ply, FenWanted fen_wanted);

    // Reads the games in text (normally one) and calls sink for each admitted one.
    void scan(std::string_view text, const std::function<void(const ScannedGame&)>& sink);

    [[nodiscard]] const ScanCounts& counts() const { return counts_; }

  private:
    std::uint32_t max_ply_;
    FenWanted fen_wanted_;
    ScanCounts counts_;
};

// A SAN token without the annotation glyph after it (!, ?, !?, ?!, !! or ??), which may follow
// a + or #.
[[nodiscard]] std::string_view strip_glyphs(std::string_view token);

} // namespace chesscoach::pipeline

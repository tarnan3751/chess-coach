// What eval and explore --engine share (docs/reference/cli.md, **eval**): finding the engine
// binary, a search through the profile store's engine cache (a lookup, and on a miss the engine
// started, searched and the result stored), and the text of an engine line: its score from the
// side to move and its pv in SAN; with opening the profile store and the text of its errors, which
// every command over --data-dir uses. The engine runs only through core's Engine, the cache only
// through core's ProfileStore, the moves only through core's Position.
#pragma once

#include <chesscoach/engine.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/profile_store.hpp>

#include "common.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chesscoach::cli {

// The minimum depth a cached search needs to be served without searching again.
inline constexpr int default_min_depth = 12;

// The engine options of the command line.
struct EngineSettings {
    std::optional<std::string> stockfish; // --stockfish
    int threads = 1;
    int hash_mb = 16;
};

// What a search through the cache is asked for.
struct EngineQuery {
    int lines = 1;
    double seconds = 1.0;
    int min_depth = default_min_depth;
};

// The lines of a search and whether the cache answered without the engine.
struct EngineAnswer {
    CachedSearch search;
    bool cached = false;
};

// What a profile store error says after the file or the store it names: "profiles.db <text>".
[[nodiscard]] std::string_view describe(ProfileError error);

// The profile store of the data directory on the clock, which ProfileStore::open creates when
// missing.
[[nodiscard]] Result<ProfileStore> open_profiles(const std::string& data_dir,
                                                 const Clock& clock = system_clock_seconds);

// --stockfish when given; else the environment variable CHESS_COACH_STOCKFISH when set; else
// stockfish on the PATH. An error when none of them names a file.
[[nodiscard]] Result<std::filesystem::path> find_engine(const std::optional<std::string>& given);

// The cache's lines for the position when they reach the minimum depth (§12.2); otherwise the
// engine found by find_engine is started, searches, and the result goes through the cache, whose
// answer is returned. The engine is not looked for on a hit.
[[nodiscard]] Result<EngineAnswer> search_through_cache(ProfileStore& profiles,
                                                        const Position& position,
                                                        const EngineQuery& query,
                                                        const EngineSettings& settings);

// "cp <n>" or "mate <n>", from the side to move as the engine and the cache give it.
[[nodiscard]] std::string score_text(const SearchLine& line);

// "depth <d> <score>", then the pv in SAN when the line has one: the text of one engine line.
[[nodiscard]] std::string line_text(const Position& position, const SearchLine& line);

// The first max_moves moves of a pv in SAN, separated by spaces, played from the position; a move
// that is not legal where it falls ends the text with its UCI.
[[nodiscard]] std::string pv_san(const Position& position, const std::vector<std::string>& pv,
                                 std::size_t max_moves = 8);

} // namespace chesscoach::cli

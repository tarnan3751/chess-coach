// Engine: one UCI engine (Stockfish) as a child process over reproc++, held behind a pointer so no
// public header includes reproc (docs/reference/core.md, **The engine**). Synchronous: start,
// new_game and search block the calling thread; stop() and stop_before() may be called from any
// other thread: stop() ends the search in progress, stop_before() also every later search of an
// older generation. The client owns the protocol: uci and uciok, the options, isready
// and readyok before every position, never a second go while one is in flight, ucinewgame through
// new_game. Every reply has a bound; a missed bound or a dead process leaves the engine unusable
// (is_alive() false) and the caller starts a new one. Scores are from the side to move, as UCI
// gives them. On POSIX, start() ignores SIGPIPE process-wide, once, so that writing to an engine
// that has died returns an error instead of killing this process. No Qt.
#pragma once

#include <chesscoach/position.hpp>

#include <tl/expected.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chesscoach {

enum class EngineError : std::uint8_t {
    start_failed,    // the binary could not be started
    timeout,         // no uciok, readyok or bestmove within its bound; the process is killed
    died,            // the process exited or closed its output, or the engine was already unusable
    busy,            // a search or new_game is in progress on another thread; nothing was sent
    unsupported,     // limit_elo asked of an engine that advertises no UCI_Elo option
    invalid_request, // lines below 1, or seconds not in (0, maximum_search_seconds]; nothing sent
};

struct EngineOptions {
    int threads = 1;
    int hash_mb = 16;
    // Passed to the binary after its path: the fake engine's script; Stockfish takes none.
    std::vector<std::string> arguments;
    // The bound for uciok and readyok, for bestmove beyond the movetime, and for bestmove after a
    // stop.
    std::chrono::milliseconds reply_timeout{5000};
};

// The longest search a request may ask for.
inline constexpr double maximum_search_seconds = 3600.0;

struct SearchRequest {
    // MultiPV; above the maximum the engine advertises, the maximum.
    int lines = 1;
    // go movetime, rounded to milliseconds, at least 1; above 0 and at most maximum_search_seconds.
    double seconds = 1.0;
    // UCI_LimitStrength with UCI_Elo, clamped to the engine's range.
    std::optional<int> limit_elo = std::nullopt;
    // The caller's request number, which stop_before compares: below the highest generation
    // stopped, the search returns at once as stopped, sending nothing.
    std::int64_t generation = 0;
};

// The last exact info of one multipv: an info with a lowerbound or upperbound score does not
// replace it.
struct SearchLine {
    int multipv = 1;
    int depth = 0;
    std::optional<int> score_cp; // exactly one of the two scores is set
    std::optional<int> score_mate;
    std::vector<std::string>
        pv; // UCI moves; empty only where the engine sent no pv (no legal move)

    friend bool operator==(const SearchLine&, const SearchLine&) = default;
};

struct SearchResult {
    std::vector<SearchLine> lines; // by multipv; only the multipvs that arrived, none above lines
    std::string best_move;         // empty when the engine answers "bestmove (none)"
    std::string engine_name;       // the engine's id name
    bool limited = false;          // searched at limited strength
    std::optional<int> elo; // the UCI_Elo applied, after clamping (Stockfish's floor is 1320)
    bool stopped = false;   // ended by stop() before its movetime

    friend bool operator==(const SearchResult&, const SearchResult&) = default;
};

class Engine {
  public:
    // Starts the binary and performs the handshake: uci; every line up to uciok, recording the id
    // name, the UCI_Elo option's min and max and the MultiPV option's max, and ignoring anything
    // else; setoption Threads and Hash; isready; readyok.
    [[nodiscard]] static tl::expected<Engine, EngineError>
    start(const std::filesystem::path& binary, EngineOptions options = {});

    // Not while a search runs on another thread.
    Engine(Engine&& other) noexcept;
    Engine& operator=(Engine&& other) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    // Sends quit, then terminates and kills the process if it lingers.
    ~Engine();

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] bool is_alive() const noexcept;

    // ucinewgame, isready, readyok: between games.
    [[nodiscard]] tl::expected<void, EngineError> new_game();

    // setoption MultiPV when the line count (clamped to the engine's maximum) changed,
    // UCI_LimitStrength when it changed and UCI_Elo when its value changed; isready, readyok;
    // position fen <normalized FEN> 0 1; go movetime; the info lines collected until bestmove,
    // which must come within the movetime plus reply_timeout, or within reply_timeout of a stop.
    [[nodiscard]] tl::expected<SearchResult, EngineError> search(const Position& position,
                                                                 const SearchRequest& request);

    // Ends the search in progress early: it then returns what arrived, with stopped set. No effect
    // when no search is in progress. Thread-safe; the searching thread itself writes stop.
    void stop() noexcept;

    // Records generation as stopped: the search in progress stops, as stop() does, when its
    // generation is below it, and every later search whose generation is below the highest one
    // recorded returns at once with stopped set, no lines and nothing sent. A stop that arrives
    // before the search it means starts is so never lost. Thread-safe.
    void stop_before(std::int64_t generation) noexcept;

  private:
    struct Impl;
    explicit Engine(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace chesscoach

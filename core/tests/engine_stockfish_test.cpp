// Integration tests of chesscoach/engine.hpp against the real Stockfish (core; engine), with one
// thread and a 16 MB hash so lanes share the machine. The binary is CHESS_COACH_STOCKFISH, else
// stockfish on the PATH (tools/gates.sh's convention); without one every test is skipped with a
// message, and the full gate, which requires one, fails instead.
#include <chesscoach/engine.hpp>
#include <chesscoach/position.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

using chesscoach::Engine;
using chesscoach::Position;
using namespace std::chrono_literals;

namespace {

std::optional<std::filesystem::path> find_stockfish() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read once, before any thread starts
    if (const char* given = std::getenv("CHESS_COACH_STOCKFISH"); given != nullptr && *given != 0) {
        return std::filesystem::path(given);
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read once, before any thread starts
    const char* path = std::getenv("PATH");
    if (path == nullptr) {
        return std::nullopt;
    }
#ifdef _WIN32
    constexpr char separator = ';';
    constexpr std::string_view name = "stockfish.exe";
#else
    constexpr char separator = ':';
    constexpr std::string_view name = "stockfish";
#endif
    std::string_view dirs(path);
    while (!dirs.empty()) {
        const auto end = dirs.find(separator);
        const auto dir = dirs.substr(0, end);
        dirs.remove_prefix(end == std::string_view::npos ? dirs.size() : end + 1);
        if (dir.empty()) {
            continue;
        }
        auto candidate = std::filesystem::path(dir) / name;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return std::nullopt;
}

Engine start_stockfish() {
    const auto binary = find_stockfish();
    if (!binary) {
        SKIP("no Stockfish: set CHESS_COACH_STOCKFISH or put stockfish on the PATH");
    }
    auto engine = Engine::start(*binary);
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

Position parse(std::string_view fen) {
    auto position = Position::from_fen(fen);
    REQUIRE(position.has_value());
    return *position;
}

} // namespace

TEST_CASE("Stockfish starts and gives three lines from the start position", "[engine][stockfish]") {
    auto engine = start_stockfish();
    CHECK(engine.name().starts_with("Stockfish"));
    const auto result = engine.search(Position::start(), {.lines = 3, .seconds = 0.5});
    REQUIRE(result.has_value());
    REQUIRE(result->lines.size() == 3);
    for (int index = 0; index < 3; ++index) {
        const auto& line = result->lines.at(static_cast<std::size_t>(index));
        CAPTURE(index);
        CHECK(line.multipv == index + 1);
        CHECK(line.depth > 1);
        CHECK_FALSE(line.pv.empty());
        CHECK(line.score_cp.has_value() != line.score_mate.has_value());
        CHECK(Position::start().san_of(line.pv.front()).has_value());
    }
    // The best move is legal; it need not be the first line's first move, because a final info
    // with a bound score (a fail high on a new best move) does not replace the line.
    CHECK(Position::start().san_of(result->best_move).has_value());
    CHECK(result->engine_name == engine.name());
    CHECK_FALSE(result->stopped);
}

TEST_CASE("Stockfish finds a mate in one, scored from the side to move", "[engine][stockfish]") {
    auto engine = start_stockfish();
    const auto white = engine.search(parse("6k1/5ppp/8/8/8/8/8/R5K1 w - -"), {.seconds = 0.5});
    REQUIRE(white.has_value());
    REQUIRE_FALSE(white->lines.empty());
    CHECK(white->lines.front().score_mate == 1);
    CHECK(white->best_move == "a1a8");
    const auto black = engine.search(parse("r5k1/8/8/8/8/8/5PPP/6K1 b - -"), {.seconds = 0.5});
    REQUIRE(black.has_value());
    REQUIRE_FALSE(black->lines.empty());
    CHECK(black->lines.front().score_mate == 1);
    CHECK(black->best_move == "a8a1");
}

TEST_CASE("A limited-strength search applies Stockfish's Elo floor", "[engine][stockfish]") {
    auto engine = start_stockfish();
    const auto result =
        engine.search(Position::start(), {.lines = 1, .seconds = 0.3, .limit_elo = 800});
    REQUIRE(result.has_value());
    CHECK(result->limited);
    CHECK(result->elo == 1320);
    CHECK_FALSE(result->best_move.empty());
    const auto full = engine.search(Position::start(), {.seconds = 0.2});
    REQUIRE(full.has_value());
    CHECK_FALSE(full->limited);
}

TEST_CASE("stop() from another thread ends a long search within a second", "[engine][stockfish]") {
    auto engine = start_stockfish();
    auto running = std::async(std::launch::async, [&] {
        return engine.search(Position::start(), {.lines = 1, .seconds = 10.0});
    });
    std::this_thread::sleep_for(300ms);
    const auto stopped_at = std::chrono::steady_clock::now();
    engine.stop();
    const auto result = running.get();
    const auto elapsed = std::chrono::steady_clock::now() - stopped_at;
    REQUIRE(result.has_value());
    CHECK(result->stopped);
    CHECK_FALSE(result->lines.empty());
    CHECK_FALSE(result->best_move.empty());
    CHECK(elapsed < 1s);
}

TEST_CASE("new_game works, and two engines run one after the other", "[engine][stockfish]") {
    {
        auto engine = start_stockfish();
        REQUIRE(engine.new_game().has_value());
        const auto result = engine.search(Position::start(), {.seconds = 0.2});
        REQUIRE(result.has_value());
        CHECK_FALSE(result->lines.empty());
    }
    auto again = start_stockfish();
    auto e4 = Position::start();
    REQUIRE(e4.play_san("e4").has_value());
    const auto result = again.search(e4, {.lines = 2, .seconds = 0.2});
    REQUIRE(result.has_value());
    CHECK(result->lines.size() == 2);
    CHECK(e4.san_of(result->best_move).has_value());
}

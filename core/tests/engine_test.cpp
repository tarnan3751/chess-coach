// Unit tests for chesscoach/engine.hpp against fake-uci (core; unit): every script of the fake
// engine, with the exact result, the engine's state afterwards and, from the fake's log, exactly
// what the client sent and in which order: isready before every position, one go at a time, stop
// once, ucinewgame through new_game, the options only when they change.
#include <chesscoach/engine.hpp>
#include <chesscoach/position.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

using chesscoach::Engine;
using chesscoach::EngineError;
using chesscoach::EngineOptions;
using chesscoach::Position;
using chesscoach::SearchLine;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view start_fen_sent =
    "position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// The fake engine's log of what it received, in a file of its own under the build directory.
class Log {
  public:
    explicit Log(std::string_view name)
        : path_(std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / "engine" /
                (std::string(name) + ".log")) {
        std::filesystem::create_directories(path_.parent_path());
        std::filesystem::remove(path_);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    // Waits until the fake has received the line, so a test acts after it rather than after a
    // guessed delay.
    void wait_for(std::string_view line) const {
        const auto until = std::chrono::steady_clock::now() + 10s;
        while (std::chrono::steady_clock::now() < until) {
            for (const auto& received : lines()) {
                if (received == line) {
                    return;
                }
            }
            std::this_thread::sleep_for(10ms);
        }
        FAIL("the fake never received " << line);
    }
    [[nodiscard]] std::vector<std::string> lines() const {
        std::ifstream in(path_);
        std::vector<std::string> lines;
        for (std::string line; std::getline(in, line);) {
            lines.push_back(line);
        }
        return lines;
    }

  private:
    std::filesystem::path path_;
};

// The tests' reply bound: generous, so that a loaded machine still starts the fake in time; a test
// that times a bound sets its own.
constexpr auto default_reply_timeout = 5000ms;

EngineOptions fake(std::string_view script, const Log& log, int delay_ms = 0,
                   std::chrono::milliseconds reply_timeout = default_reply_timeout) {
    EngineOptions options;
    options.arguments = {std::string(script), std::to_string(delay_ms), log.path().string()};
    options.reply_timeout = reply_timeout;
    return options;
}

Engine start(std::string_view script, const Log& log, int delay_ms = 0,
             std::chrono::milliseconds reply_timeout = default_reply_timeout) {
    auto engine = Engine::start(CHESSCOACH_FAKE_UCI, fake(script, log, delay_ms, reply_timeout));
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

SearchLine line(int multipv, int depth, int cp, std::vector<std::string> pv) {
    return {
        .multipv = multipv,
        .depth = depth,
        .score_cp = cp,
        .score_mate = {},
        .pv = std::move(pv),
    };
}

std::vector<std::string> handshake() {
    return {
        "uci",
        "setoption name Threads value 1",
        "setoption name Hash value 16",
        "isready",
    };
}

std::vector<std::string> then(std::vector<std::string> first,
                              std::initializer_list<std::string> more) {
    first.insert(first.end(), more);
    return first;
}

} // namespace

TEST_CASE("The handshake records the name and sends the options", "[engine]") {
    const Log log("handshake");
    {
        const auto engine = start("normal", log);
        CHECK(engine.name() == "FakeUCI 1.0");
        CHECK(engine.is_alive());
    }
    CHECK(log.lines() == then(handshake(), {"quit"}));
}

TEST_CASE("A search sends isready before its position and collects the last info per line",
          "[engine]") {
    const Log log("search");
    {
        auto engine = start("normal", log);
        const auto result = engine.search(Position::start(), {.lines = 3, .seconds = 0.05});
        REQUIRE(result.has_value());
        CHECK(result->lines == std::vector{line(1, 3, 29, {"e2e4", "e7e5"}),
                                           line(2, 3, 28, {"d2d4", "e7e5"}),
                                           line(3, 3, 27, {"g1f3", "e7e5"})});
        CHECK(result->best_move == "e2e4");
        CHECK(result->engine_name == "FakeUCI 1.0");
        CHECK_FALSE(result->limited);
        CHECK_FALSE(result->elo.has_value());
        CHECK_FALSE(result->stopped);

        // The same line count again: no MultiPV, but isready before the position once more.
        auto e4 = Position::start();
        REQUIRE(e4.play_san("e4").has_value());
        const auto again = engine.search(e4, {.lines = 3, .seconds = 0.05});
        REQUIRE(again.has_value());
        CHECK(again->lines.size() == 3);
        REQUIRE(engine.new_game().has_value());
        const auto one = engine.search(Position::start(), {.lines = 1, .seconds = 0.05});
        REQUIRE(one.has_value());
        CHECK(one->lines == std::vector{line(1, 3, 29, {"e2e4", "e7e5"})});
    }
    CHECK(log.lines() ==
          then(handshake(),
               {"setoption name MultiPV value 3", "isready", std::string(start_fen_sent),
                "go movetime 50", "isready",
                "position fen rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
                "go movetime 50", "ucinewgame", "isready", "setoption name MultiPV value 1",
                "isready", std::string(start_fen_sent), "go movetime 50", "quit"}));
}

TEST_CASE("A limited search clamps the Elo to the engine's range and resets after", "[engine]") {
    const Log log("limited");
    {
        auto engine = start("normal", log);
        const auto low = engine.search(Position::start(), {.seconds = 0.02, .limit_elo = 800});
        REQUIRE(low.has_value());
        CHECK(low->limited);
        CHECK(low->elo == 1320);
        const auto high = engine.search(Position::start(), {.seconds = 0.02, .limit_elo = 5000});
        REQUIRE(high.has_value());
        CHECK(high->elo == 3190);
        const auto full = engine.search(Position::start(), {.seconds = 0.02});
        REQUIRE(full.has_value());
        CHECK_FALSE(full->limited);
        CHECK_FALSE(full->elo.has_value());
    }
    CHECK(log.lines() ==
          then(handshake(),
               {"setoption name UCI_LimitStrength value true", "setoption name UCI_Elo value 1320",
                "isready", std::string(start_fen_sent), "go movetime 20",
                "setoption name UCI_Elo value 3190", "isready", std::string(start_fen_sent),
                "go movetime 20", "setoption name UCI_LimitStrength value false", "isready",
                std::string(start_fen_sent), "go movetime 20", "quit"}));
}

TEST_CASE("stop() ends a search in progress, and a second call is busy meanwhile", "[engine]") {
    const Log log("stop");
    {
        auto engine = start("normal", log);
        const auto began = std::chrono::steady_clock::now();
        auto running = std::async(std::launch::async, [&] {
            return engine.search(Position::start(), {.lines = 1, .seconds = 30.0});
        });
        log.wait_for("go movetime 30000");
        const auto second = engine.search(Position::start(), {});
        REQUIRE_FALSE(second.has_value());
        CHECK(second.error() == EngineError::busy);
        const auto new_game = engine.new_game();
        REQUIRE_FALSE(new_game.has_value());
        CHECK(new_game.error() == EngineError::busy);
        engine.stop();
        const auto result = running.get();
        const auto elapsed = std::chrono::steady_clock::now() - began;
        REQUIRE(result.has_value());
        CHECK(result->stopped);
        CHECK(result->lines.size() == 1);
        CHECK(result->best_move == "e2e4");
        CHECK(elapsed < 15s); // ended by the stop, well before the 30 s movetime
        CHECK(engine.is_alive());

        // stop() with no search in progress changes nothing.
        engine.stop();
        const auto next = engine.search(Position::start(), {.seconds = 0.02});
        REQUIRE(next.has_value());
        CHECK_FALSE(next->stopped);
    }
    CHECK(log.lines() ==
          then(handshake(), {"isready", std::string(start_fen_sent), "go movetime 30000", "stop",
                             "isready", std::string(start_fen_sent), "go movetime 20", "quit"}));
}

TEST_CASE("A search of a stopped generation sends nothing, and a current one searches",
          "[engine]") {
    const Log log("stop-before");
    {
        auto engine = start("normal", log);
        // Recorded with no search in progress: the stop is kept for the searches it means.
        engine.stop_before(5);
        const auto stale = engine.search(Position::start(), {.seconds = 0.02, .generation = 4});
        REQUIRE(stale.has_value());
        CHECK(stale->stopped);
        CHECK(stale->lines.empty());
        CHECK(stale->best_move.empty());
        CHECK(engine.is_alive());
        // A lower generation recorded later does not lower the bound.
        engine.stop_before(2);
        CHECK(engine.search(Position::start(), {.seconds = 0.02, .generation = 3})->stopped);
        // The current generation, and any later one, searches.
        const auto current = engine.search(Position::start(), {.seconds = 0.02, .generation = 5});
        REQUIRE(current.has_value());
        CHECK_FALSE(current->stopped);
        CHECK(current->best_move == "e2e4");
        const auto later = engine.search(Position::start(), {.seconds = 0.02, .generation = 6});
        REQUIRE(later.has_value());
        CHECK_FALSE(later->stopped);
    }
    // The stale searches sent nothing at all.
    CHECK(log.lines() ==
          then(handshake(), {"isready", std::string(start_fen_sent), "go movetime 20", "isready",
                             std::string(start_fen_sent), "go movetime 20", "quit"}));
}

TEST_CASE("stop_before stops a running search of a lower generation only", "[engine]") {
    SECTION("a higher generation stops it") {
        const Log log("stop-before-running");
        {
            auto engine = start("normal", log);
            const auto began = std::chrono::steady_clock::now();
            auto running = std::async(std::launch::async, [&] {
                return engine.search(Position::start(),
                                     {.lines = 1, .seconds = 30.0, .generation = 1});
            });
            log.wait_for("go movetime 30000");
            engine.stop_before(2);
            const auto result = running.get();
            REQUIRE(result.has_value());
            CHECK(result->stopped);
            CHECK(result->best_move == "e2e4");
            CHECK(std::chrono::steady_clock::now() - began < 15s);
        }
        CHECK(log.lines() == then(handshake(), {"isready", std::string(start_fen_sent),
                                                "go movetime 30000", "stop", "quit"}));
    }
    SECTION("its own generation does not") {
        const Log log("stop-before-own");
        {
            auto engine = start("normal", log);
            auto running = std::async(std::launch::async, [&] {
                return engine.search(Position::start(),
                                     {.lines = 1, .seconds = 0.5, .generation = 3});
            });
            log.wait_for("go movetime 500");
            engine.stop_before(3);
            const auto result = running.get();
            REQUIRE(result.has_value());
            CHECK_FALSE(result->stopped);
        }
        CHECK(log.lines() == then(handshake(), {"isready", std::string(start_fen_sent),
                                                "go movetime 500", "quit"}));
    }
}

TEST_CASE("A late readyok is waited for within the bound, and past it is a timeout", "[engine]") {
    SECTION("within the bound") {
        const Log log("late-readyok-within");
        auto engine = start("late-readyok", log, 100, default_reply_timeout);
        const auto result = engine.search(Position::start(), {.seconds = 0.02});
        REQUIRE(result.has_value());
        CHECK(result->lines.size() == 1);
        CHECK(engine.is_alive());
    }
    SECTION("past the bound") {
        const Log log("late-readyok-past");
        // The first readyok, the handshake's, is on time; the search's comes 4 s late.
        auto engine = start("late-readyok", log, 4000, 1000ms);
        const auto began = std::chrono::steady_clock::now();
        const auto result = engine.search(Position::start(), {.seconds = 0.02});
        CHECK(std::chrono::steady_clock::now() - began < 4000ms);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == EngineError::timeout);
        CHECK_FALSE(engine.is_alive());
        CHECK(engine.search(Position::start(), {}).error() == EngineError::died);
        CHECK(engine.new_game().error() == EngineError::died);
    }
}

TEST_CASE("Interleaved, repeated, bound and junk info lines leave the last exact one per line",
          "[engine]") {
    const Log log("interleaved");
    auto engine = start("interleaved-info", log);
    const auto result = engine.search(Position::start(), {.lines = 2, .seconds = 0.02});
    REQUIRE(result.has_value());
    CHECK(result->lines == std::vector{line(1, 2, 25, {"e2e4", "e7e5", "g1f3"}),
                                       SearchLine{.multipv = 2,
                                                  .depth = 3,
                                                  .score_cp = {},
                                                  .score_mate = -2,
                                                  .pv = {"d2d4", "e7e5"}}});
    CHECK(result->best_move == "e2e4");
}

TEST_CASE("A missing bestmove is a timeout, and the engine is unusable after", "[engine]") {
    const Log log("no-bestmove");
    auto engine = start("no-bestmove", log, 0, 1000ms);
    const auto began = std::chrono::steady_clock::now();
    const auto result = engine.search(Position::start(), {.seconds = 0.05});
    const auto elapsed = std::chrono::steady_clock::now() - began;
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == EngineError::timeout);
    CHECK(elapsed >= 1050ms); // the movetime and the reply bound, never less
    CHECK(elapsed < 15s);
    CHECK_FALSE(engine.is_alive());
    CHECK(engine.search(Position::start(), {}).error() == EngineError::died);
}

TEST_CASE("An engine dying mid-search is reported, and the engine is unusable after", "[engine]") {
    const Log log("die-mid-search");
    auto engine = start("die-mid-search", log);
    const auto result = engine.search(Position::start(), {.seconds = 1.0});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == EngineError::died);
    CHECK_FALSE(engine.is_alive());
    CHECK(engine.new_game().error() == EngineError::died);
}

TEST_CASE("Lines before uciok are ignored, and an engine without UCI_Elo cannot limit",
          "[engine]") {
    const Log log("garbage");
    auto engine = start("garbage", log);
    CHECK(engine.name() == "FakeUCI 1.0");
    const auto result = engine.search(Position::start(), {.seconds = 0.02});
    REQUIRE(result.has_value());
    CHECK(result->lines.size() == 1);
    const auto limited = engine.search(Position::start(), {.limit_elo = 1500});
    REQUIRE_FALSE(limited.has_value());
    CHECK(limited.error() == EngineError::unsupported);
    CHECK(engine.is_alive());
}

TEST_CASE("An invalid request sends nothing", "[engine]") {
    const Log log("invalid");
    {
        auto engine = start("normal", log);
        CHECK(engine.search(Position::start(), {.lines = 0}).error() ==
              EngineError::invalid_request);
        CHECK(engine.search(Position::start(), {.seconds = 0.0}).error() ==
              EngineError::invalid_request);
        CHECK(engine.search(Position::start(), {.seconds = -1.0}).error() ==
              EngineError::invalid_request);
        CHECK(engine.search(Position::start(), {.seconds = 3600.5}).error() ==
              EngineError::invalid_request);
        CHECK(engine.search(Position::start(), {.seconds = std::numeric_limits<double>::infinity()})
                  .error() == EngineError::invalid_request);
        CHECK(
            engine.search(Position::start(), {.seconds = std::numeric_limits<double>::quiet_NaN()})
                .error() == EngineError::invalid_request);
        CHECK(engine.is_alive());
    }
    CHECK(log.lines() == then(handshake(), {"quit"}));
}

TEST_CASE("A binary that cannot start is reported", "[engine]") {
    const auto engine =
        Engine::start(std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / "no-such-engine");
    REQUIRE_FALSE(engine.has_value());
    CHECK(engine.error() == EngineError::start_failed);
}

TEST_CASE("An engine moves with its process", "[engine]") {
    const Log log("moved");
    auto engine = start("normal", log);
    auto moved = std::move(engine);
    const auto result = moved.search(Position::start(), {.seconds = 0.02});
    REQUIRE(result.has_value());
    CHECK(moved.is_alive());
}

TEST_CASE("After a stop, the shorter bound holds even while info keeps coming", "[engine]") {
    const Log log("chatty-after-stop");
    auto engine = start("chatty-after-stop", log, 0, 1000ms);
    auto running = std::async(std::launch::async, [&] {
        return engine.search(Position::start(), {.lines = 1, .seconds = 60.0});
    });
    log.wait_for("go movetime 60000");
    const auto stopped_at = std::chrono::steady_clock::now();
    engine.stop();
    const auto result = running.get();
    const auto elapsed = std::chrono::steady_clock::now() - stopped_at;
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == EngineError::timeout);
    // The reply bound after the stop, not the minute of movetime.
    CHECK(elapsed >= 950ms);
    CHECK(elapsed < 30s);
    CHECK_FALSE(engine.is_alive());
}

TEST_CASE("A request above the engine's MultiPV maximum is clamped to it", "[engine]") {
    const Log log("multipv-max");
    {
        auto engine = start("multipv-max-2", log);
        const auto three = engine.search(Position::start(), {.lines = 3, .seconds = 0.02});
        REQUIRE(three.has_value());
        CHECK(three->lines ==
              std::vector{line(1, 3, 29, {"e2e4", "e7e5"}), line(2, 3, 28, {"d2d4", "e7e5"})});
        // Two lines were sent, so asking for two sends nothing more; one sends 1.
        const auto two = engine.search(Position::start(), {.lines = 2, .seconds = 0.02});
        REQUIRE(two.has_value());
        CHECK(two->lines.size() == 2);
        const auto one = engine.search(Position::start(), {.lines = 1, .seconds = 0.02});
        REQUIRE(one.has_value());
        CHECK(one->lines.size() == 1);
    }
    CHECK(
        log.lines() ==
        then(handshake(), {"setoption name MultiPV value 2", "isready", std::string(start_fen_sent),
                           "go movetime 20", "isready", std::string(start_fen_sent),
                           "go movetime 20", "setoption name MultiPV value 1", "isready",
                           std::string(start_fen_sent), "go movetime 20", "quit"}));
}

TEST_CASE("UCI_Elo is sent only when its value changes", "[engine]") {
    const Log log("elo-once");
    {
        auto engine = start("normal", log);
        for (int i = 0; i < 3; ++i) {
            const auto limited =
                engine.search(Position::start(), {.seconds = 0.02, .limit_elo = 1500});
            REQUIRE(limited.has_value());
            CHECK(limited->elo == 1500);
        }
        // Full strength, then 1500 again: the engine kept its UCI_Elo.
        REQUIRE(engine.search(Position::start(), {.seconds = 0.02}).has_value());
        REQUIRE(engine.search(Position::start(), {.seconds = 0.02, .limit_elo = 1500}).has_value());
    }
    CHECK(log.lines() == then(handshake(), {"setoption name UCI_LimitStrength value true",
                                            "setoption name UCI_Elo value 1500",
                                            "isready",
                                            std::string(start_fen_sent),
                                            "go movetime 20",
                                            "isready",
                                            std::string(start_fen_sent),
                                            "go movetime 20",
                                            "isready",
                                            std::string(start_fen_sent),
                                            "go movetime 20",
                                            "setoption name UCI_LimitStrength value false",
                                            "isready",
                                            std::string(start_fen_sent),
                                            "go movetime 20",
                                            "setoption name UCI_LimitStrength value true",
                                            "isready",
                                            std::string(start_fen_sent),
                                            "go movetime 20",
                                            "quit"}));
}

TEST_CASE("The legal script answers with the position's first legal move", "[engine]") {
    const Log log("legal");
    auto engine = start("legal", log);
    const auto start_reply = engine.search(Position::start(), {.seconds = 1.0});
    REQUIRE(start_reply.has_value());
    CHECK(start_reply->best_move == "a2a3");
    CHECK(start_reply->lines == std::vector{line(1, 1, 0, {"a2a3"})});
    auto e4 = Position::start();
    REQUIRE(e4.play_san("e4").has_value());
    const auto reply = engine.search(e4, {.seconds = 1.0});
    REQUIRE(reply.has_value());
    CHECK(reply->best_move == e4.legal_moves_uci().front());
    // Mated: no legal move.
    const auto mated =
        Position::from_fen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq -");
    REQUIRE(mated.has_value());
    const auto none = engine.search(*mated, {.seconds = 1.0});
    REQUIRE(none.has_value());
    CHECK(none->best_move.empty());
    REQUIRE(none->lines.size() == 1);
    CHECK(none->lines.front().score_mate == 0);
    CHECK(none->lines.front().pv.empty());
}

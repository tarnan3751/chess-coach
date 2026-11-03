// Unit tests for chesscoach/engine_reply_provider.hpp against fake-uci (core; unit): the Elo per
// tier, the limited one-line search the provider asks for (from the fake's log), the best move it
// returns, and an engine failure as a ProviderError.
#include <chesscoach/engine_reply_provider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using chesscoach::Engine;
using chesscoach::EngineOptions;
using chesscoach::EngineReplyProvider;
using chesscoach::Position;
using chesscoach::provider_elo;
using chesscoach::ProviderConfig;
using chesscoach::Tier;

namespace {

// The fake engine's log, under the build directory.
std::filesystem::path log_path(std::string_view name) {
    const auto path = std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / "provider" /
                      (std::string(name) + ".log");
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::remove(path);
    return path;
}

std::vector<std::string> lines_of(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

Engine start(std::string_view script, const std::filesystem::path& log) {
    EngineOptions options;
    options.arguments = {std::string(script), "0", log.string()};
    auto engine = Engine::start(CHESSCOACH_FAKE_UCI, options);
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

bool sent(const std::vector<std::string>& lines, std::string_view line) {
    return std::ranges::find(lines, line) != lines.end();
}

} // namespace

TEST_CASE("provider_elo is the tier's lower bound plus the offset, 1000 for tier 0", "[provider]") {
    CHECK(provider_elo(Tier::under_1200, {}) == 1100);
    CHECK(provider_elo(Tier::r1400_1599, {}) == 1500);
    CHECK(provider_elo(Tier::r2200_up, {}) == 2300);
    CHECK(provider_elo(Tier::r1800_1999, ProviderConfig{.seconds = 1.0, .elo_offset = -50}) ==
          1750);
}

TEST_CASE("EngineReplyProvider asks for one limited line and returns the best move", "[provider]") {
    const auto log = log_path("limited");
    Engine engine = start("normal", log);
    EngineReplyProvider provider(engine, Tier::r1400_1599,
                                 ProviderConfig{.seconds = 0.01, .elo_offset = 100});
    const auto reply = provider.reply(Position::start());
    REQUIRE(reply.has_value());
    CHECK(*reply == "e2e4");
    const auto lines = lines_of(log);
    CHECK(sent(lines, "setoption name UCI_LimitStrength value true"));
    CHECK(sent(lines, "setoption name UCI_Elo value 1500"));
    CHECK(sent(lines, "go movetime 10"));
    CHECK_FALSE(sent(lines, "setoption name MultiPV value 2"));
}

TEST_CASE("EngineReplyProvider clamps tier 0's Elo to the engine's floor", "[provider]") {
    const auto log = log_path("floor");
    Engine engine = start("normal", log);
    EngineReplyProvider provider(engine, Tier::under_1200,
                                 ProviderConfig{.seconds = 0.01, .elo_offset = 100});
    REQUIRE(provider.reply(Position::start()).has_value());
    CHECK(sent(lines_of(log), "setoption name UCI_Elo value 1320"));
}

TEST_CASE("An engine failure is a ProviderError with its message", "[provider]") {
    const auto log = log_path("dies");
    Engine engine = start("die-mid-search", log);
    EngineReplyProvider provider(engine, Tier::r1400_1599,
                                 ProviderConfig{.seconds = 0.01, .elo_offset = 100});
    const auto reply = provider.reply(Position::start());
    REQUIRE_FALSE(reply.has_value());
    CHECK(reply.error().message == "the engine died");
}

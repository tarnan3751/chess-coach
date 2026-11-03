// The engine reply provider (chesscoach/engine_reply_provider.hpp).
#include <chesscoach/engine_reply_provider.hpp>

#include <cstddef>
#include <string_view>

namespace chesscoach {

namespace {

constexpr int tier_0_base = 1000; // tier 0 has no lower bound

std::string_view describe(EngineError error) {
    switch (error) {
    case EngineError::start_failed:
        return "the engine could not be started";
    case EngineError::timeout:
        return "the engine did not answer in time";
    case EngineError::died:
        return "the engine died";
    case EngineError::busy:
        return "the engine is busy with another search";
    case EngineError::unsupported:
        return "the engine cannot limit its strength (no UCI_Elo option)";
    case EngineError::invalid_request:
        return "the search request is invalid";
    }
    return "the engine failed";
}

} // namespace

int provider_elo(Tier tier, const ProviderConfig& config) {
    const int index = tier_index(tier);
    const int base =
        index == 0 ? tier_0_base : tier_lower_bounds.at(static_cast<std::size_t>(index - 1));
    return base + config.elo_offset;
}

EngineReplyProvider::EngineReplyProvider(Engine& engine, Tier tier, ProviderConfig config)
    : engine_(&engine), tier_(tier), config_(config) {}

tl::expected<std::string, ProviderError> EngineReplyProvider::reply(const Position& position) {
    const SearchRequest request{
        .lines = 1,
        .seconds = config_.seconds,
        .limit_elo = provider_elo(tier_, config_),
    };
    const auto result = engine_->search(position, request);
    if (!result) {
        return tl::unexpected(ProviderError{.message = std::string(describe(result.error()))});
    }
    if (result->best_move.empty()) {
        return tl::unexpected(ProviderError{.message = "the engine has no move to give"});
    }
    return result->best_move;
}

} // namespace chesscoach

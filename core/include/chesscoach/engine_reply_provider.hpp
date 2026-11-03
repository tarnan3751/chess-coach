// EngineReplyProvider: the spar session's coach once the game has left the book (spar.hpp;
// docs/reference/core.md, **The engine reply provider**). Each reply is one line of a
// limited-strength search, UCI_LimitStrength with UCI_Elo at the tier's level, for a fixed time,
// straight through the Engine: a limited search neither reads nor writes the engine cache
// (docs/database.md §12.2). The engine is the caller's and must outlive the provider.
#pragma once

#include <chesscoach/engine.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/spar.hpp>
#include <chesscoach/tier.hpp>

#include <tl/expected.hpp>

#include <string>

namespace chesscoach {

struct ProviderConfig {
    double seconds = 0.5; // each reply's movetime
    int elo_offset = 100; // above the tier's lower bound
};

// The Elo a reply is searched at: the tier's lower bound (1000 for tier 0, which has none) plus
// elo_offset. The engine clamps it to its range; Stockfish's floor is 1320.
[[nodiscard]] int provider_elo(Tier tier, const ProviderConfig& config);

class EngineReplyProvider final : public ReplyProvider {
  public:
    EngineReplyProvider(Engine& engine, Tier tier, ProviderConfig config = {});

    // The best move of a one-line search at provider_elo for config.seconds; an engine error, or
    // an engine with no move to give, is a ProviderError carrying what happened.
    [[nodiscard]] tl::expected<std::string, ProviderError> reply(const Position& position) override;

  private:
    Engine* engine_;
    Tier tier_;
    ProviderConfig config_;
};

} // namespace chesscoach

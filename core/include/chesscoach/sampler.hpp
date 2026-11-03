// The sparring sampler (docs/database.md §13): a reply drawn from what players at a tier played
// from a position, in proportion to how often. The candidates are the position's moves rows at
// the tier with at least min_games plays, ordered by move text bytewise, each weighted by its
// total (temperature 1.0, the only one the contract defines); one raw output of the session's
// Splitmix64, modulo the sum of the weights, picks the first candidate whose cumulative weight
// exceeds it. No standard-library distribution, so a seed gives the same draws on every
// platform. Pure: the generator is the caller's.
#pragma once

#include <chesscoach/opening_store.hpp>
#include <chesscoach/random.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chesscoach {

struct SamplerConfig {
    std::int64_t min_games = 10; // §13: a candidate's plays at the tier, at least
};

struct Candidate {
    std::string move;        // UCI (§5.3)
    std::int64_t weight = 0; // the move's total at the tier

    friend bool operator==(const Candidate&, const Candidate&) = default;
};

// The moves with at least config.min_games plays, ordered by move text, weighted by total.
[[nodiscard]] std::vector<Candidate> sampler_candidates(const std::vector<MoveRow>& moves,
                                                        const SamplerConfig& config);

// §13's draw over candidates as sampler_candidates orders them: exactly one output of generator
// when there is a candidate, none and nullopt when there is not.
[[nodiscard]] std::optional<std::string> sample_reply(const std::vector<Candidate>& candidates,
                                                      Splitmix64& generator);

} // namespace chesscoach

// The sparring sampler (chesscoach/sampler.hpp).
#include <chesscoach/sampler.hpp>

#include <algorithm>

namespace chesscoach {

std::vector<Candidate> sampler_candidates(const std::vector<MoveRow>& moves,
                                          const SamplerConfig& config) {
    std::vector<Candidate> candidates;
    for (const MoveRow& move : moves) {
        if (move.total() >= config.min_games) {
            candidates.push_back(Candidate{.move = move.move, .weight = move.total()});
        }
    }
    std::ranges::sort(candidates, {}, &Candidate::move);
    return candidates;
}

std::optional<std::string> sample_reply(const std::vector<Candidate>& candidates,
                                        Splitmix64& generator) {
    std::uint64_t total = 0;
    for (const Candidate& candidate : candidates) {
        total += static_cast<std::uint64_t>(candidate.weight);
    }
    if (total == 0) {
        return std::nullopt;
    }
    const std::uint64_t drawn = generator.next() % total;
    std::uint64_t cumulative = 0;
    for (const Candidate& candidate : candidates) {
        cumulative += static_cast<std::uint64_t>(candidate.weight);
        if (cumulative > drawn) {
            return candidate.move;
        }
    }
    return candidates.back().move; // not reached: the last cumulative weight is the total
}

} // namespace chesscoach

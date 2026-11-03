// Pass two's exact counts (aggregate.hpp).
#include "aggregate.hpp"

#include <algorithm>
#include <limits>

namespace chesscoach::pipeline {

namespace {

// Adds count to counter, or stops it at 2^32 - 1 and returns false.
bool add_checked(std::uint32_t& counter, std::uint32_t count) {
    if (counter > std::numeric_limits<std::uint32_t>::max() - count) {
        counter = std::numeric_limits<std::uint32_t>::max();
        return false;
    }
    counter += count;
    return true;
}

} // namespace

bool Results::add(GameResult result, std::uint32_t count) {
    switch (result) {
    case GameResult::white:
        return add_checked(white, count);
    case GameResult::draw:
        return add_checked(draws, count);
    case GameResult::black:
        return add_checked(black, count);
    }
    return true;
}

bool Results::add(const Results& other) {
    const bool white_fits = add_checked(white, other.white);
    const bool draws_fit = add_checked(draws, other.draws);
    const bool black_fits = add_checked(black, other.black);
    return white_fits && draws_fit && black_fits;
}

bool NodeCounts::add_play(MoveCode move, Key child, const Results& results) {
    const auto found = std::ranges::find(moves, move, &MoveCounts::move);
    if (found != moves.end()) {
        return found->results.add(results);
    }
    moves.push_back(MoveCounts{.move = move, .child = child, .results = results});
    return true;
}

std::size_t NodeIdHash::operator()(const NodeId& id) const noexcept {
    // Zobrist keys are uniform already; the tier only has to separate the tiers of one key.
    return static_cast<std::size_t>(id.key ^ (std::uint64_t{id.tier} * 0x9E3779B97F4A7C15U));
}

BatchCounts::BatchCounts(const VisitSketch& sketch, std::uint16_t min_games,
                         const Aggregate& aggregate)
    : sketch_(&sketch), min_games_(min_games), aggregate_(&aggregate) {}

bool BatchCounts::candidate(std::uint8_t tier, Key key) const {
    return sketch_->estimate(tier, key) >= min_games_;
}

bool BatchCounts::fen_wanted(std::uint8_t tier, Key key) {
    if (!candidate(tier, key) || fens_.contains(key) || fen_known_.contains(key)) {
        return false;
    }
    if (aggregate_->has_fen(key)) {
        fen_known_.insert(key);
        return false;
    }
    // Asked again at the key's next visit if this game is not admitted after all.
    return true;
}

void BatchCounts::add(const ScannedGame& game) {
    Results one;
    static_cast<void>(one.add(game.result)); // one game: never near 2^32
    for (std::size_t ply = 0; ply < game.keys.size(); ++ply) {
        const Key key = game.keys.at(ply);
        if (!candidate(game.tier, key)) {
            continue;
        }
        NodeCounts& node = nodes_[NodeId{.key = key, .tier = game.tier}];
        bool fits = node.visits.add(one);
        if (ply < game.moves.size()) {
            fits = node.add_play(game.moves.at(ply), game.keys.at(ply + 1), one) && fits;
        }
        overflowed_ = overflowed_ || !fits;
    }
    for (const auto& [key, fen] : game.fens) {
        fens_.try_emplace(key, fen);
    }
}

void BatchCounts::flush(Aggregate& aggregate) {
    for (const auto& [id, counts] : nodes_) {
        aggregate.merge(id, counts);
    }
    for (const auto& [key, fen] : fens_) {
        aggregate.merge_fen(key, fen);
    }
    if (overflowed_) {
        aggregate.mark_overflowed();
    }
    nodes_.clear();
    fens_.clear();
    fen_known_.clear();
    overflowed_ = false;
}

Aggregate::Aggregate() {
    shards_.reserve(shard_count);
    for (std::size_t i = 0; i < shard_count; ++i) {
        shards_.push_back(std::make_unique<Shard>());
    }
}

bool Aggregate::has_fen(Key key) const {
    const Shard& shard = *shards_.at(shard_of(key));
    const std::scoped_lock lock(shard.mutex);
    return shard.fens.contains(key);
}

void Aggregate::merge(const NodeId& id, const NodeCounts& counts) {
    Shard& shard = *shards_.at(shard_of(id.key));
    const std::scoped_lock lock(shard.mutex);
    NodeCounts& node = shard.nodes[id];
    bool fits = node.visits.add(counts.visits);
    for (const MoveCounts& play : counts.moves) {
        fits = node.add_play(play.move, play.child, play.results) && fits;
    }
    if (!fits) {
        mark_overflowed();
    }
}

void Aggregate::merge_fen(Key key, const std::string& fen) {
    Shard& shard = *shards_.at(shard_of(key));
    const std::scoped_lock lock(shard.mutex);
    shard.fens.try_emplace(key, fen);
}

std::size_t Aggregate::candidate_count() const {
    std::size_t count = 0;
    for (const auto& shard : shards_) {
        const std::scoped_lock lock(shard->mutex);
        count += shard->nodes.size();
    }
    return count;
}

Aggregate::Survivors Aggregate::take_survivors(std::uint32_t min_games) {
    Survivors survivors;
    for (const auto& shard : shards_) {
        const std::scoped_lock lock(shard->mutex);
        for (auto& [id, counts] : shard->nodes) {
            if (counts.visits.total() < min_games) {
                continue;
            }
            std::ranges::sort(counts.moves, {}, &MoveCounts::move);
            survivors.nodes.push_back(
                SurvivingNode{.id = id, .visits = counts.visits, .moves = std::move(counts.moves)});
        }
        shard->nodes.clear();
    }
    for (const SurvivingNode& node : survivors.nodes) {
        if (survivors.fens.contains(node.id.key)) {
            continue;
        }
        Shard& shard = *shards_.at(shard_of(node.id.key));
        const auto found = shard.fens.find(node.id.key);
        if (found != shard.fens.end()) {
            survivors.fens.emplace(node.id.key, std::move(found->second));
        }
    }
    for (const auto& shard : shards_) {
        shard->fens.clear();
    }
    std::ranges::sort(survivors.nodes, [](const SurvivingNode& a, const SurvivingNode& b) {
        const auto a_key = static_cast<std::int64_t>(a.id.key);
        const auto b_key = static_cast<std::int64_t>(b.id.key);
        return a_key != b_key ? a_key < b_key : a.id.tier < b.id.tier;
    });
    return survivors;
}

} // namespace chesscoach::pipeline

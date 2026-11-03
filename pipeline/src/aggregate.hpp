// Aggregate: pass two's exact counts (docs/database.md §6.4, §8). For every (key, tier) whose
// sketch estimate reaches min_games (a candidate), the visits split by result and, per move
// played from it, the plays split by result and the child; and the normalized FEN of every
// candidate key. Each worker adds its batch's games to a BatchCounts of its own, which merges
// the batch's repeats, and flushes it into the shared counts, which are split into 256 shards
// under a mutex each. Sums commute, so the counts do not depend on the number of threads or the
// order of the batches. A counter that would pass 2^32 - 1 is a fatal error, never a wrap.
#pragma once

#include <chesscoach/position.hpp>

#include "game_filter.hpp"
#include "game_scanner.hpp"
#include "move_code.hpp"
#include "sketch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chesscoach::pipeline {

struct Results {
    std::uint32_t white = 0;
    std::uint32_t draws = 0;
    std::uint32_t black = 0;

    void add(GameResult result, std::uint32_t count = 1);
    void add(const Results& other);
    [[nodiscard]] std::uint64_t total() const {
        return std::uint64_t{white} + std::uint64_t{draws} + std::uint64_t{black};
    }
    friend bool operator==(const Results&, const Results&) = default;
};

struct MoveCounts {
    MoveCode move = 0;
    Key child = 0;
    Results results;
    std::uint8_t badges = 0; // set after the merge (badges.hpp)
};

struct NodeCounts {
    Results visits;
    std::vector<MoveCounts> moves;

    void add_play(MoveCode move, Key child, const Results& results);
};

struct NodeId {
    Key key = 0;
    std::uint8_t tier = 0;
    friend bool operator==(const NodeId&, const NodeId&) = default;
};

struct NodeIdHash {
    std::size_t operator()(const NodeId& id) const noexcept;
};

class Aggregate;

// One worker's counts for one batch, flushed into the Aggregate at the end of the batch.
class BatchCounts {
  public:
    BatchCounts(const VisitSketch& sketch, std::uint16_t min_games, const Aggregate& aggregate);

    // Whether the scanner should record this position's FEN: a candidate whose FEN neither the
    // aggregate nor this batch's admitted games have yet.
    [[nodiscard]] bool fen_wanted(std::uint8_t tier, Key key);

    void add(const ScannedGame& game);
    void flush(Aggregate& aggregate);

  private:
    [[nodiscard]] bool candidate(std::uint8_t tier, Key key) const;

    const VisitSketch* sketch_;
    std::uint16_t min_games_;
    const Aggregate* aggregate_;
    std::unordered_map<NodeId, NodeCounts, NodeIdHash> nodes_;
    std::unordered_map<Key, std::string> fens_;
    std::unordered_set<Key> fen_known_; // keys whose FEN the aggregate holds
};

// A surviving (key, tier) and its moves, ready to write.
struct SurvivingNode {
    NodeId id;
    Results visits;
    std::vector<MoveCounts> moves; // in move-code order
};

class Aggregate {
  public:
    static constexpr std::size_t shard_count = 256;

    Aggregate();

    [[nodiscard]] bool has_fen(Key key) const;
    void merge(const NodeId& id, const NodeCounts& counts);
    void merge_fen(Key key, const std::string& fen);

    [[nodiscard]] std::size_t candidate_count() const;

    // Moves out the (key, tier) with at least min_games visits, sorted by key as stored in SQLite
    // (signed) then tier, and the FENs of their keys; the aggregate is left empty.
    struct Survivors {
        std::vector<SurvivingNode> nodes;
        std::unordered_map<Key, std::string> fens;
    };
    [[nodiscard]] Survivors take_survivors(std::uint32_t min_games);

  private:
    struct Shard {
        mutable std::mutex mutex;
        std::unordered_map<NodeId, NodeCounts, NodeIdHash> nodes;
        std::unordered_map<Key, std::string> fens;
    };

    [[nodiscard]] static std::size_t shard_of(Key key) {
        return static_cast<std::size_t>(key >> 56U);
    }

    std::vector<std::unique_ptr<Shard>> shards_;
};

} // namespace chesscoach::pipeline

// Tree: what the build writes, held in memory between pass two and the writer: the surviving
// nodes with their moves, and one PositionRecord per surviving key, which the evaluation merge
// and the names fill in (docs/database.md §6.2-§6.4).
#pragma once

#include <chesscoach/position.hpp>

#include "aggregate.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chesscoach::pipeline {

// A stored evaluation, scores from white's side (docs/database.md §6.2, §9).
struct Evaluation {
    std::optional<std::int64_t> cp;
    std::optional<std::int64_t> mate;
    std::int64_t depth = 0;
    std::string best_move; // UCI, castling as the king's two-square move
};

struct PositionRecord {
    Key key = 0;
    std::string fen;
    std::optional<Evaluation> evaluation;
    std::optional<std::string> eco;
    std::optional<std::string> name;
};

struct Tree {
    std::vector<SurvivingNode> nodes;            // by key as stored (signed), then tier
    std::vector<PositionRecord> positions;       // by key as stored (signed)
    std::unordered_map<Key, std::size_t> by_key; // key -> index in positions

    [[nodiscard]] const PositionRecord* find(Key key) const {
        const auto found = by_key.find(key);
        return found == by_key.end() ? nullptr : &positions.at(found->second);
    }
    [[nodiscard]] PositionRecord* find(Key key) {
        const auto found = by_key.find(key);
        return found == by_key.end() ? nullptr : &positions.at(found->second);
    }
};

} // namespace chesscoach::pipeline

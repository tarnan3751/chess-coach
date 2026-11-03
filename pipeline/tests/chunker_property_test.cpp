// Property tests for Chunker: whatever the batch size, the batches concatenate to the stream,
// every batch but the last ends where the next record begins, and split_games partitions a
// batch (pipeline; property).
#include "chunker.hpp"

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include <numeric>
#include <sstream>
#include <string>
#include <vector>

using chesscoach::pipeline::Chunker;
using chesscoach::pipeline::ChunkKind;
using chesscoach::pipeline::split_games;

TEST_CASE("Chunker batches concatenate to the stream and end on record boundaries",
          "[chunker][property]") {
    REQUIRE(rc::check("chunker", [] {
        const auto pieces =
            *rc::gen::container<std::vector<std::string>>(rc::gen::element<std::string>(
                "[Event \"x\"]\n", "1. e4 e5\n", "\n", "{ c }", "abc", "[Event"));
        const std::string text = std::accumulate(pieces.begin(), pieces.end(), std::string{});
        const auto size = *rc::gen::inRange<std::size_t>(1, 64);
        for (const ChunkKind kind : {ChunkKind::games, ChunkKind::lines}) {
            std::stringbuf source(text);
            Chunker chunker(source, kind, size);
            std::string joined;
            std::vector<std::string> batches;
            for (std::string batch = chunker.next(); !batch.empty(); batch = chunker.next()) {
                joined += batch;
                batches.push_back(batch);
            }
            RC_ASSERT(joined == text);
            for (std::size_t i = 0; i + 1 < batches.size(); ++i) {
                if (kind == ChunkKind::lines) {
                    RC_ASSERT(batches.at(i).back() == '\n');
                } else {
                    RC_ASSERT(batches.at(i).back() == '\n');
                    RC_ASSERT(batches.at(i + 1).starts_with("[Event "));
                }
            }
        }
        std::string rejoined;
        for (const auto game : split_games(text)) {
            rejoined += game;
        }
        RC_ASSERT(rejoined == text);
    }));
}

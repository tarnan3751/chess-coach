// Unit tests for Chunker and split_games: batches end at a game's Event line or at a line end,
// a record longer than a batch is kept whole, and split_games yields each game (pipeline; unit).
#include "chunker.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using chesscoach::pipeline::Chunker;
using chesscoach::pipeline::ChunkKind;
using chesscoach::pipeline::split_games;

namespace {

std::vector<std::string> all_batches(std::string_view text, ChunkKind kind, std::size_t size) {
    std::stringbuf source{std::string(text)};
    Chunker chunker(source, kind, size);
    std::vector<std::string> batches;
    for (std::string batch = chunker.next(); !batch.empty(); batch = chunker.next()) {
        batches.push_back(batch);
    }
    return batches;
}

constexpr std::string_view three_games = "[Event \"a\"]\n\n1. e4 1-0\n\n"
                                         "[Event \"b\"]\n\n1. d4 0-1\n\n"
                                         "[Event \"c\"]\n\n1. c4 1/2-1/2\n";

} // namespace

TEST_CASE("Chunker cuts games batches before an Event line", "[chunker]") {
    const auto batches = all_batches(three_games, ChunkKind::games, 8);
    REQUIRE(batches.size() == 3);
    CHECK(batches.at(0) == "[Event \"a\"]\n\n1. e4 1-0\n\n");
    CHECK(batches.at(1) == "[Event \"b\"]\n\n1. d4 0-1\n\n");
    CHECK(batches.at(2) == "[Event \"c\"]\n\n1. c4 1/2-1/2\n");
}

TEST_CASE("Chunker keeps several games in a large batch", "[chunker]") {
    const auto batches = all_batches(three_games, ChunkKind::games, std::size_t{1} << 20U);
    REQUIRE(batches.size() == 1);
    CHECK(batches.at(0) == three_games);
}

TEST_CASE("Chunker cuts lines batches after a line end", "[chunker]") {
    const auto batches = all_batches("one\ntwo\nthree", ChunkKind::lines, 2);
    REQUIRE(batches.size() == 3);
    CHECK(batches.at(0) == "one\n");
    CHECK(batches.at(1) == "two\n");
    CHECK(batches.at(2) == "three");
}

TEST_CASE("split_games yields each game of a batch", "[chunker]") {
    const auto games = split_games(three_games);
    REQUIRE(games.size() == 3);
    CHECK(games.at(0) == "[Event \"a\"]\n\n1. e4 1-0\n\n");
    CHECK(games.at(1) == "[Event \"b\"]\n\n1. d4 0-1\n\n");
    CHECK(games.at(2) == "[Event \"c\"]\n\n1. c4 1/2-1/2\n");
    CHECK(split_games("").empty());
}

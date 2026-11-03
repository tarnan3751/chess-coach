// Unit tests for chesscoach/sampler.hpp (core; unit): the candidates (min_games, move-text
// order, weights), the draw (nullopt and no output used without a candidate, the same sequence
// for a seed, the draw by cumulative weight), and the frequencies over 200,000 draws within one
// percentage point of the weight shares (docs/database.md §13).
#include <chesscoach/sampler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

using chesscoach::Candidate;
using chesscoach::MoveRow;
using chesscoach::sample_reply;
using chesscoach::sampler_candidates;
using chesscoach::SamplerConfig;
using chesscoach::Splitmix64;
using chesscoach::Tier;

namespace {

MoveRow row(std::string move, std::int64_t white, std::int64_t draws, std::int64_t black) {
    return MoveRow{
        .key = 1,
        .tier = Tier::r1400_1599,
        .move = std::move(move),
        .child = 2,
        .white = white,
        .draws = draws,
        .black = black,
        .badges = chesscoach::Badges{},
    };
}

} // namespace

TEST_CASE("sampler_candidates keeps moves of at least min_games, by move text", "[sampler]") {
    const std::vector<MoveRow> moves{
        row("g1f3", 5, 3, 2), // 10: kept at the default
        row("e2e4", 40, 5, 15),
        row("a2a3", 4, 0, 5), // 9: dropped
        row("d2d4", 20, 0, 0),
    };
    const std::vector<Candidate> expected{
        {.move = "d2d4", .weight = 20},
        {.move = "e2e4", .weight = 60},
        {.move = "g1f3", .weight = 10},
    };
    CHECK(sampler_candidates(moves, SamplerConfig{}) == expected);
    CHECK(sampler_candidates(moves, SamplerConfig{.min_games = 21}) ==
          std::vector<Candidate>{{.move = "e2e4", .weight = 60}});
    CHECK(sampler_candidates(moves, SamplerConfig{.min_games = 100}).empty());
    CHECK(sampler_candidates({}, SamplerConfig{}).empty());
}

TEST_CASE("sample_reply draws nothing, and uses no output, without a candidate", "[sampler]") {
    Splitmix64 generator(7);
    Splitmix64 untouched(7);
    CHECK_FALSE(sample_reply({}, generator).has_value());
    CHECK(generator.next() == untouched.next());
}

TEST_CASE("sample_reply picks the first candidate whose cumulative weight exceeds the draw",
          "[sampler]") {
    const std::vector<Candidate> candidates{
        {.move = "a", .weight = 1},
        {.move = "b", .weight = 2},
        {.move = "c", .weight = 3},
    };
    // The draw is one output modulo 6: 0 is a, 1 and 2 are b, 3 to 5 are c.
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        Splitmix64 probe(seed);
        const std::uint64_t drawn = probe.next() % 6;
        std::string expected = "c";
        if (drawn < 1) {
            expected = "a";
        } else if (drawn < 3) {
            expected = "b";
        }
        Splitmix64 generator(seed);
        CHECK(sample_reply(candidates, generator) == expected);
    }
}

TEST_CASE("sample_reply gives the same sequence for the same seed", "[sampler]") {
    const std::vector<Candidate> candidates{
        {.move = "d2d4", .weight = 20},
        {.move = "e2e4", .weight = 60},
        {.move = "g1f3", .weight = 10},
    };
    Splitmix64 a(20261103);
    Splitmix64 b(20261103);
    for (int i = 0; i < 1000; ++i) {
        CHECK(sample_reply(candidates, a) == sample_reply(candidates, b));
    }
}

TEST_CASE("sample_reply's frequencies match the weights within one point over 200,000 draws",
          "[sampler]") {
    const std::vector<Candidate> candidates{
        {.move = "c7c5", .weight = 250},
        {.move = "e7e5", .weight = 500},
        {.move = "e7e6", .weight = 150},
        {.move = "g8f6", .weight = 100},
    };
    constexpr int draws = 200000;
    Splitmix64 generator(20261103);
    std::map<std::string, int> counts;
    for (int i = 0; i < draws; ++i) {
        counts[sample_reply(candidates, generator).value_or("")] += 1;
    }
    CHECK(counts.size() == candidates.size());
    for (const Candidate& candidate : candidates) {
        const double frequency = static_cast<double>(counts[candidate.move]) / draws;
        const double share = static_cast<double>(candidate.weight) / 1000.0;
        CHECK(frequency > share - 0.01);
        CHECK(frequency < share + 0.01);
    }
}

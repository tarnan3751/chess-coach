// Unit tests for the §3 header checks (docs/database.md §3): each check's skip reason, the
// order in which they apply, and what an admitted game carries (pipeline; unit).
#include "game_filter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <utility>
#include <vector>

using chesscoach::pipeline::check_headers;
using chesscoach::pipeline::GameHeaders;
using chesscoach::pipeline::GameResult;
using chesscoach::pipeline::name;
using chesscoach::pipeline::SkipCounts;
using chesscoach::pipeline::SkipReason;

namespace {

using Tags = std::vector<std::pair<std::string_view, std::string_view>>;

GameHeaders headers(const Tags& overrides, const Tags& removed = {}) {
    const Tags tags{
        {"Event", "Rated Blitz game"},
        {"WhiteElo", "1500"},
        {"BlackElo", "1700"},
        {"Result", "1-0"},
    };
    GameHeaders result;
    for (const auto& [tag, value] : tags) {
        bool skip = false;
        for (const auto& [removed_tag, unused] : removed) {
            skip = skip || removed_tag == tag;
        }
        for (const auto& [override_tag, unused] : overrides) {
            skip = skip || override_tag == tag;
        }
        if (!skip) {
            result.set(tag, value);
        }
    }
    for (const auto& [tag, value] : overrides) {
        result.set(tag, value);
    }
    return result;
}

SkipReason reason(const GameHeaders& game) {
    const auto checked = check_headers(game);
    REQUIRE_FALSE(checked.has_value());
    return checked.error();
}

} // namespace

TEST_CASE("check_headers admits a rated blitz, rapid or classical game", "[filter]") {
    for (const std::string_view event : {
             "Rated Blitz game",
             "Rated Rapid game",
             "Rated Classical game",
             "Rated Blitz tournament https://lichess.org/tournament/abcd1234",
             "Rated Classical",
         }) {
        const auto admitted = check_headers(headers({{"Event", event}}));
        REQUIRE(admitted.has_value());
        CHECK(admitted->white_elo == 1500);
        CHECK(admitted->black_elo == 1700);
        CHECK(admitted->result == GameResult::white);
    }
    CHECK(check_headers(headers({{"Result", "0-1"}}))->result == GameResult::black);
    CHECK(check_headers(headers({{"Result", "1/2-1/2"}}))->result == GameResult::draw);
    CHECK(check_headers(headers({{"Variant", "Standard"}})).has_value());
}

TEST_CASE("check_headers skips a casual game or one without Event as unrated", "[filter]") {
    CHECK(reason(headers({{"Event", "Casual Blitz game"}})) == SkipReason::unrated);
    CHECK(reason(headers({}, {{"Event", ""}})) == SkipReason::unrated);
    CHECK(reason(headers({{"Event", "rated Blitz game"}})) == SkipReason::unrated);
}

TEST_CASE("check_headers skips other speeds as speed", "[filter]") {
    for (const std::string_view event : {
             "Rated Bullet game",
             "Rated UltraBullet game",
             "Rated Correspondence game",
             "Rated Bullet tournament https://lichess.org/tournament/x",
             "Rated Blitzkrieg game",
             "Rated  Blitz game",
             "Rated ",
         }) {
        CHECK(reason(headers({{"Event", event}})) == SkipReason::speed);
    }
}

TEST_CASE("check_headers skips a variant or a set-up position as variant", "[filter]") {
    CHECK(reason(headers({{"Variant", "Chess960"}})) == SkipReason::variant);
    CHECK(reason(headers({{"Variant", "From Position"}})) == SkipReason::variant);
    CHECK(reason(headers({{"SetUp", "1"}})) == SkipReason::variant);
    CHECK(reason(headers({{"FEN", "8/8/8/8/8/8/8/8 w - - 0 1"}})) == SkipReason::variant);
}

TEST_CASE("check_headers skips a missing or non-numeric rating as rating", "[filter]") {
    CHECK(reason(headers({{"WhiteElo", "?"}})) == SkipReason::rating);
    CHECK(reason(headers({{"BlackElo", "?"}})) == SkipReason::rating);
    CHECK(reason(headers({}, {{"BlackElo", ""}})) == SkipReason::rating);
    CHECK(reason(headers({{"WhiteElo", "-1500"}})) == SkipReason::rating);
    CHECK(reason(headers({{"WhiteElo", "15 00"}})) == SkipReason::rating);
    CHECK(reason(headers({{"WhiteElo", ""}})) == SkipReason::rating);
    CHECK(reason(headers({{"WhiteElo", "1234567890"}})) == SkipReason::rating);
}

TEST_CASE("check_headers skips an unfinished or unknown result as result", "[filter]") {
    CHECK(reason(headers({{"Result", "*"}})) == SkipReason::result);
    CHECK(reason(headers({{"Result", "1-1"}})) == SkipReason::result);
    CHECK(reason(headers({}, {{"Result", ""}})) == SkipReason::result);
}

TEST_CASE("check_headers names the first failing check", "[filter]") {
    CHECK(reason(headers({{"Event", "Casual Bullet game"}, {"WhiteElo", "?"}})) ==
          SkipReason::unrated);
    CHECK(reason(headers({{"Event", "Rated Bullet game"}, {"Variant", "Chess960"}})) ==
          SkipReason::speed);
    CHECK(reason(headers({{"Variant", "Atomic"}, {"WhiteElo", "?"}})) == SkipReason::variant);
    CHECK(reason(headers({{"WhiteElo", "?"}, {"Result", "*"}})) == SkipReason::rating);
}

TEST_CASE("Skip reasons are named as §3 names them, and counts add up", "[filter]") {
    CHECK(name(SkipReason::malformed) == "malformed");
    CHECK(name(SkipReason::unrated) == "unrated");
    CHECK(name(SkipReason::speed) == "speed");
    CHECK(name(SkipReason::variant) == "variant");
    CHECK(name(SkipReason::rating) == "rating");
    CHECK(name(SkipReason::result) == "result");
    CHECK(name(SkipReason::moves) == "moves");
    SkipCounts a;
    a.add(SkipReason::speed, 3);
    a.add(SkipReason::moves);
    SkipCounts b;
    b.add(SkipReason::speed);
    a += b;
    CHECK(a.count(SkipReason::speed) == 4);
    CHECK(a.count(SkipReason::moves) == 1);
    CHECK(a.total() == 5);
}

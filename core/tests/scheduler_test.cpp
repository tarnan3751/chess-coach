// Unit tests for chesscoach/scheduler.hpp (core; unit): SM-2's numbers worked out by hand, checked
// at compile time and at run time: a new card, a sequence of good and again reviews, hard and
// easy, the ease floor, the interval cap, and when a card is due.
#include <chesscoach/scheduler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

using chesscoach::CardState;
using chesscoach::Grade;
using chesscoach::Review;
using chesscoach::seconds_per_day;

namespace {

constexpr std::int64_t created = 1'700'000'000;

constexpr std::int64_t day(int n) { return created + (n * seconds_per_day); }

// good, good, good, again, good, each on the day the card falls due:
//   good:  repetitions 0 -> interval 1;  ease 2500 + 100 - 1 * (80 + 1 * 20) = 2500; due day 1
//   good:  repetitions 1 -> interval 6;  ease 2500;                                 due day 7
//   good:  repetitions 2 -> (6 * 2500 + 500) / 1000 = 15; ease 2500;                due day 22
//   again: interval 1, repetitions 0;  ease 2500 + 100 - 4 * (80 + 4 * 20) = 1960;  due day 23
//   good:  repetitions 0 -> interval 1;  ease 1960;                                 due day 24
constexpr std::array<Review, 5> history{
    {
        {.grade = Grade::good, .reviewed_at = day(0)},
        {.grade = Grade::good, .reviewed_at = day(1)},
        {.grade = Grade::good, .reviewed_at = day(7)},
        {.grade = Grade::again, .reviewed_at = day(22)},
        {.grade = Grade::good, .reviewed_at = day(23)},
    },
};

static_assert(chesscoach::replay(created, history) == CardState{
                                                          .repetitions = 1,
                                                          .interval_days = 1,
                                                          .ease_milli = 1960,
                                                          .due_at = day(24),
                                                      });

} // namespace

TEST_CASE("A new card is due when it is created", "[scheduler]") {
    const auto card = chesscoach::new_card(created);
    CHECK(card ==
          CardState{.repetitions = 0, .interval_days = 0, .ease_milli = 2500, .due_at = created});
    CHECK(chesscoach::is_due(card, created));
    CHECK_FALSE(chesscoach::is_due(card, created - 1));
}

TEST_CASE("Good, good, good, again, good gives 1, 6, 15, 1 and 1 days", "[scheduler]") {
    const std::array<CardState, 5> expected{
        {
            {.repetitions = 1, .interval_days = 1, .ease_milli = 2500, .due_at = day(1)},
            {.repetitions = 2, .interval_days = 6, .ease_milli = 2500, .due_at = day(7)},
            {.repetitions = 3, .interval_days = 15, .ease_milli = 2500, .due_at = day(22)},
            {.repetitions = 0, .interval_days = 1, .ease_milli = 1960, .due_at = day(23)},
            {.repetitions = 1, .interval_days = 1, .ease_milli = 1960, .due_at = day(24)},
        },
    };
    auto card = chesscoach::new_card(created);
    for (std::size_t i = 0; i < history.size(); ++i) {
        CAPTURE(i);
        card = chesscoach::review(card, history.at(i).grade, history.at(i).reviewed_at);
        CHECK(card == expected.at(i));
    }
    CHECK(chesscoach::replay(created, history) == expected.back());
}

TEST_CASE("Hard and easy change the ease after the interval is granted", "[scheduler]") {
    // Two passes reach interval 6; the third uses the ease from before its own update.
    auto card = chesscoach::review(chesscoach::new_card(created), Grade::good, day(0));
    card = chesscoach::review(card, Grade::good, day(1));
    const auto hard = chesscoach::review(card, Grade::hard, day(7));
    // (6 * 2500 + 500) / 1000 = 15; ease 2500 + 100 - 2 * (80 + 2 * 20) = 2360.
    CHECK(hard ==
          CardState{.repetitions = 3, .interval_days = 15, .ease_milli = 2360, .due_at = day(22)});
    const auto easy = chesscoach::review(card, Grade::easy, day(7));
    // 15 days as well; ease 2500 + 100 - 0 = 2600.
    CHECK(easy ==
          CardState{.repetitions = 3, .interval_days = 15, .ease_milli = 2600, .due_at = day(22)});
    // The next pass uses the new ease: (15 * 2360 + 500) / 1000 = 35, (15 * 2600 + 500) / 1000
    // = 39.
    CHECK(chesscoach::review(hard, Grade::good, day(22)).interval_days == 35);
    CHECK(chesscoach::review(easy, Grade::good, day(22)).interval_days == 39);
}

TEST_CASE("The ease never falls below 1300", "[scheduler]") {
    auto card = chesscoach::new_card(created);
    std::int64_t now = created;
    for (int i = 0; i < 5; ++i) {
        card = chesscoach::review(card, Grade::again, now);
        now = card.due_at;
    }
    // 2500 - 540 = 1960, 1420, then 880, floored at 1300.
    CHECK(card.ease_milli == 1300);
    CHECK(card.interval_days == 1);
    CHECK(card.repetitions == 0);
}

TEST_CASE("The interval stops at 36500 days", "[scheduler]") {
    auto card = chesscoach::new_card(created);
    std::int64_t now = created;
    std::int64_t last_review = created;
    std::vector<int> intervals;
    for (int i = 0; i < 12; ++i) {
        card = chesscoach::review(card, Grade::easy, now);
        intervals.push_back(card.interval_days);
        last_review = now;
        now = card.due_at;
    }
    // 1, 6, then times 2.7, 2.8, ... rounded: 6 * 2700 -> 16, 16 * 2800 -> 45, 45 * 2900 -> 131,
    // 131 * 3000 -> 393, 393 * 3100 -> 1218, 1218 * 3200 -> 3898, 3898 * 3300 -> 12863,
    // 12863 * 3400 -> 43734, capped at 36500.
    CHECK(intervals ==
          std::vector<int>{1, 6, 16, 45, 131, 393, 1218, 3898, 12863, 36500, 36500, 36500});
    CHECK(card.due_at == last_review + (36500 * seconds_per_day));
    CHECK(card.ease_milli == 2500 + (12 * 100));
}

TEST_CASE("A card is due from its due time on", "[scheduler]") {
    const auto card = chesscoach::review(chesscoach::new_card(created), Grade::good, created);
    CHECK(card.due_at == created + seconds_per_day);
    CHECK_FALSE(chesscoach::is_due(card, created));
    CHECK_FALSE(chesscoach::is_due(card, card.due_at - 1));
    CHECK(chesscoach::is_due(card, card.due_at));
    CHECK(chesscoach::is_due(card, card.due_at + 1));
}

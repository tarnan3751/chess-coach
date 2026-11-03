// Unit tests for chesscoach/scheduler.hpp (core; unit): SM-2's numbers worked out by hand, checked
// at compile time and at run time: a new card, a sequence of good and again reviews, hard and
// easy, the ease floor and cap, the interval cap, a stored card out of range, and when a card is
// due.
#include <chesscoach/scheduler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
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

// A stored card out of every range a file changed by other means could give it: reviewed without
// overflow (a constant expression, so the compiler would reject undefined behaviour), the ease
// clamped first, the repetitions kept below INT_MAX and the interval brought into its range.
constexpr CardState out_of_range{
    .repetitions = std::numeric_limits<int>::max(),
    .interval_days = 36500,
    .ease_milli = std::numeric_limits<int>::max(),
    .due_at = 0,
};
static_assert(chesscoach::review(out_of_range, Grade::easy, 0).ease_milli == 10000);
static_assert(chesscoach::review(out_of_range, Grade::easy, 0).repetitions ==
              std::numeric_limits<int>::max());
// A stored interval below one day grows from one day: round(1 × 2.5) half up is 3.
constexpr CardState negative_interval{
    .repetitions = 5,
    .interval_days = std::numeric_limits<int>::min(),
    .ease_milli = 2500,
    .due_at = 0,
};
static_assert(chesscoach::review(negative_interval, Grade::good, 0).interval_days == 3);

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

TEST_CASE("The ease stops at 10000, and falls from there", "[scheduler]") {
    auto card = chesscoach::new_card(created);
    std::int64_t now = created;
    // 2500 + 100 per easy review reaches 10000 at the 75th, and stays.
    for (int i = 0; i < 100; ++i) {
        card = chesscoach::review(card, Grade::easy, now);
        now = card.due_at;
        CHECK(card.ease_milli == std::min(10000, 2500 + ((i + 1) * 100)));
    }
    CHECK(card.interval_days == 36500);
    // An again takes 540 off the cap.
    card = chesscoach::review(card, Grade::again, now);
    CHECK(card.ease_milli == 9460);
    // Good keeps it, and easy brings it back to the cap.
    card = chesscoach::review(card, Grade::good, card.due_at);
    CHECK(card.ease_milli == 9460);
    for (int i = 0; i < 6; ++i) {
        card = chesscoach::review(card, Grade::easy, card.due_at);
    }
    CHECK(card.ease_milli == 10000);
}

TEST_CASE("A stored card out of range is brought into range before its review", "[scheduler]") {
    const auto easy = chesscoach::review(out_of_range, Grade::easy, created);
    CHECK(easy.ease_milli == 10000);
    CHECK(easy.repetitions == std::numeric_limits<int>::max());
    CHECK(easy.interval_days == 36500);
    CHECK(easy.due_at == created + (36500 * seconds_per_day));
    // Below the floor, and negative repetitions: a first pass from the floor.
    constexpr CardState below{
        .repetitions = -5,
        .interval_days = 0,
        .ease_milli = std::numeric_limits<int>::min(),
        .due_at = 0,
    };
    const auto good = chesscoach::review(below, Grade::good, created);
    CHECK(good.ease_milli == 1300);
    CHECK(good.repetitions == 1);
    CHECK(good.interval_days == 1);
    CHECK(chesscoach::review(below, Grade::again, created).ease_milli == 1300);
    // An interval out of range: INT_MIN grows from one day, INT_MAX stops at the cap.
    const auto from_one = chesscoach::review(negative_interval, Grade::good, created);
    CHECK(from_one.interval_days == 3);
    CHECK(from_one.due_at == created + (3 * seconds_per_day));
    CardState too_long = negative_interval;
    too_long.interval_days = std::numeric_limits<int>::max();
    CHECK(chesscoach::review(too_long, Grade::good, created).interval_days == 36500);
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

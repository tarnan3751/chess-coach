// Property tests for chesscoach/scheduler.hpp (core; property; PLAN.md §7 Phase 5's acceptance):
// over random histories of grades at non-decreasing times, intervals grow on consecutive passes
// and reset to a day on a failure, a card is never due before its creation or its last review,
// the ease stays between its floor and its cap and the interval at or below its cap, long runs of
// easy reviews (run under UBSan by the gate) stop at both caps, and replaying a history gives the
// same state as reviewing it one review at a time.
#include <chesscoach/scheduler.hpp>

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include <algorithm>
#include <cstdint>
#include <vector>

using chesscoach::CardState;
using chesscoach::Grade;
using chesscoach::Review;

namespace {

rc::Gen<Grade> grades() {
    return rc::gen::element(Grade::again, Grade::hard, Grade::good, Grade::easy);
}

// Reviews at non-decreasing times from created: each one up to sixty days after the last, early
// or late against the card's due date.
rc::Gen<std::vector<Review>> histories(std::int64_t created) {
    return rc::gen::map(
        rc::gen::container<std::vector<std::pair<Grade, std::int64_t>>>(
            rc::gen::pair(grades(), rc::gen::inRange<std::int64_t>(0, std::int64_t{60} * 86400))),
        [created](const std::vector<std::pair<Grade, std::int64_t>>& steps) {
            std::vector<Review> history;
            std::int64_t now = created;
            for (const auto& [grade, wait] : steps) {
                now += wait;
                history.push_back({.grade = grade, .reviewed_at = now});
            }
            return history;
        });
}

} // namespace

TEST_CASE("Reviews keep the scheduler's invariants", "[scheduler][property]") {
    REQUIRE(rc::check("SM-2 invariants", [] {
        const auto created = *rc::gen::inRange<std::int64_t>(0, 4'000'000'000);
        const auto history = *histories(created);
        auto card = chesscoach::new_card(created);
        RC_ASSERT(card.due_at == created);
        for (const Review& entry : history) {
            const CardState before = card;
            card = chesscoach::review(card, entry.grade, entry.reviewed_at);
            RC_ASSERT(card.due_at >= entry.reviewed_at);
            RC_ASSERT(card.due_at >= created);
            RC_ASSERT(card.due_at ==
                      entry.reviewed_at + (std::int64_t{card.interval_days} * 86400));
            RC_ASSERT(card.ease_milli >= chesscoach::minimum_ease_milli);
            RC_ASSERT(card.ease_milli <= chesscoach::maximum_ease_milli);
            RC_ASSERT(card.interval_days >= 1);
            RC_ASSERT(card.interval_days <= chesscoach::maximum_interval_days);
            if (entry.grade == Grade::again) {
                RC_ASSERT(card.interval_days == 1);
                RC_ASSERT(card.repetitions == 0);
            } else {
                RC_ASSERT(card.repetitions == before.repetitions + 1);
                // Consecutive passes never shrink the interval, and from the third on it grows
                // until the cap, the ease being at least 1.3.
                if (before.repetitions >= 1) {
                    RC_ASSERT(card.interval_days >= before.interval_days);
                }
                if (before.repetitions >= 2 &&
                    before.interval_days < chesscoach::maximum_interval_days) {
                    RC_ASSERT(card.interval_days > before.interval_days);
                }
            }
        }
        RC_ASSERT(chesscoach::replay(created, history) == card);
    }));
}

TEST_CASE("Long runs of easy reviews stay within the cap", "[scheduler][property]") {
    REQUIRE(rc::check("easy runs stay bounded", [] {
        const auto reviews = *rc::gen::inRange(1, 400);
        auto card = chesscoach::new_card(0);
        for (int i = 0; i < reviews; ++i) {
            card = chesscoach::review(card, Grade::easy, card.due_at);
            RC_ASSERT(card.interval_days <= chesscoach::maximum_interval_days);
        }
        RC_ASSERT(card.ease_milli == std::min(chesscoach::maximum_ease_milli,
                                              chesscoach::initial_ease_milli + (reviews * 100)));
        RC_ASSERT(card.repetitions == reviews);
    }));
}

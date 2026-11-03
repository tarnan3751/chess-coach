// The spaced-repetition scheduler: SM-2 (PLAN.md §5.2, §11) as pure functions over a card's
// integer state, so a card's history replays to exactly the same state on every platform
// (docs/reference/core.md, **The scheduler**). The numbers are tier A; FSRS may later replace the
// rules behind these same functions. No clock: every time is given, in Unix seconds UTC, and is
// bounded by the caller (the CLI takes times up to the end of the year 9999), so a due date, the
// time plus at most 36500 days, stays far from the limits of 64-bit arithmetic. The ease and the
// interval are capped, so their integer arithmetic is bounded however many reviews a card gets;
// repetitions counts passes in an int, which a person's reviews cannot overflow. A stored card
// may hold anything a file changed by other means gives it, so review brings its ease into
// [minimum_ease_milli, maximum_ease_milli], its repetitions below INT_MAX and its interval into
// [1, maximum_interval_days] before any arithmetic.
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

namespace chesscoach {

inline constexpr std::int64_t seconds_per_day = 86400;
inline constexpr int initial_ease_milli = 2500;     // SM-2's starting E-Factor, 2.5
inline constexpr int minimum_ease_milli = 1300;     // SM-2's floor, 1.3
inline constexpr int maximum_ease_milli = 10000;    // 10.0: keeps the ease an int, ours not SM-2's
inline constexpr int maximum_interval_days = 36500; // keeps the integer arithmetic bounded

// SM-2's quality of a recall; again fails, the others pass. The values are what is stored.
enum class Grade : std::uint8_t { again = 1, hard = 3, good = 4, easy = 5 };

struct CardState {
    int repetitions = 0;                 // passes since the last failure
    int interval_days = 0;               // the last interval granted; 0 before the first review
    int ease_milli = initial_ease_milli; // the E-Factor, in thousandths
    std::int64_t due_at = 0;             // when the card is next due

    friend bool operator==(const CardState&, const CardState&) = default;
};

struct Review {
    Grade grade = Grade::again;
    std::int64_t reviewed_at = 0;

    friend bool operator==(const Review&, const Review&) = default;
};

// A new card: due at once, when it is created, so never in the past.
[[nodiscard]] constexpr CardState new_card(std::int64_t created_at) noexcept {
    return CardState{
        .repetitions = 0,
        .interval_days = 0,
        .ease_milli = initial_ease_milli,
        .due_at = created_at,
    };
}

// One review. The interval: on again, 1 day and no repetitions; on a pass, 1 day after the first
// pass, 6 after the second, then round(interval × ease) half up in integers, with the ease from
// before this review (SuperMemo's order), at most maximum_interval_days, and one repetition more.
// The ease, for every grade q: ease_milli += 100 - (5 - q) * (80 + (5 - q) * 20), at least
// minimum_ease_milli and at most maximum_ease_milli. Due: reviewed_at plus the interval in days,
// so never before the review. The card's ease is first clamped into its range, its repetitions
// into [0, INT_MAX - 1] and its interval into [1, maximum_interval_days], which changes nothing
// for a card the scheduler made.
[[nodiscard]] constexpr CardState review(const CardState& card, Grade grade,
                                         std::int64_t reviewed_at) noexcept {
    const int quality = static_cast<int>(grade);
    const int miss = 5 - quality;
    const int stored_ease = std::clamp(card.ease_milli, minimum_ease_milli, maximum_ease_milli);
    const int repetitions = std::clamp(card.repetitions, 0, std::numeric_limits<int>::max() - 1);
    const int interval = std::clamp(card.interval_days, 1, maximum_interval_days);
    CardState next = card;
    if (grade == Grade::again) {
        next.repetitions = 0;
        next.interval_days = 1;
    } else {
        if (repetitions == 0) {
            next.interval_days = 1;
        } else if (repetitions == 1) {
            next.interval_days = 6;
        } else {
            const std::int64_t grown = ((std::int64_t{interval} * stored_ease) + 500) / 1000;
            next.interval_days =
                static_cast<int>(grown < maximum_interval_days ? grown : maximum_interval_days);
        }
        next.repetitions = repetitions + 1;
    }
    const int ease = stored_ease + 100 - (miss * (80 + (miss * 20)));
    next.ease_milli = std::clamp(ease, minimum_ease_milli, maximum_ease_milli);
    next.due_at = reviewed_at + (std::int64_t{next.interval_days} * seconds_per_day);
    return next;
}

// new_card(created_at), then each review of history in order.
[[nodiscard]] constexpr CardState replay(std::int64_t created_at,
                                         std::span<const Review> history) noexcept {
    CardState card = new_card(created_at);
    for (const Review& entry : history) {
        card = review(card, entry.grade, entry.reviewed_at);
    }
    return card;
}

// Whether the card is due at now.
[[nodiscard]] constexpr bool is_due(const CardState& card, std::int64_t now) noexcept {
    return now >= card.due_at;
}

} // namespace chesscoach

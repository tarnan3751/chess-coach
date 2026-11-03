// Property test for chesscoach/profile_store.hpp's cards (core; property; docs/database.md §12.4):
// over random choices for one position (a new node, the same move kept, another move) and random
// reviews at non-decreasing times, a card's stored state is always the scheduler's replay of its
// stored reviews from its created_at, and what record_review returns is what the store keeps.
#include <chesscoach/position.hpp>
#include <chesscoach/profile_store.hpp>
#include <chesscoach/scheduler.hpp>

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using chesscoach::Card;
using chesscoach::ChoiceChange;
using chesscoach::Color;
using chesscoach::Grade;
using chesscoach::Position;
using chesscoach::ProfileStore;

namespace {

// The test's data directory under the build directory, removed when the test ends.
class DataDir {
  public:
    DataDir() : path_(std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / "profiles-property") {
        std::filesystem::remove_all(path_);
    }
    DataDir(const DataDir&) = delete;
    DataDir& operator=(const DataDir&) = delete;
    DataDir(DataDir&&) = delete;
    DataDir& operator=(DataDir&&) = delete;
    ~DataDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("A card's stored state is the replay of its reviews", "[profiles][property]") {
    const DataDir dir;
    std::int64_t now = 0;
    auto opened = ProfileStore::open(dir.path(), [&now] { return now; });
    REQUIRE(opened.has_value());
    ProfileStore& store = *opened;
    const Position start = Position::start();
    const std::array<std::string, 3> moves{"e2e4", "d2d4", "c2c4"};
    REQUIRE(rc::check("stored == replay", [&] {
        now = *rc::gen::inRange<std::int64_t>(0, 4'000'000'000);
        const auto move = *rc::gen::elementOf(moves);
        const auto change = store.set_choice(
            1, Color::white, {.key = start.key(), .fen = start.fen(), .move = move});
        RC_ASSERT(change.has_value());
        const auto cards = store.cards(1, Color::white);
        RC_ASSERT(cards.size() == 1U);
        Card card = cards.front();
        RC_ASSERT(card.move == move);
        if (*change != ChoiceChange::unchanged) {
            RC_ASSERT(card.created_at == now);
            RC_ASSERT(card.state == chesscoach::new_card(now));
            RC_ASSERT(store.card_history(card.id).empty());
        }
        const auto count = *rc::gen::inRange(0, 6);
        std::int64_t at = card.created_at;
        for (int i = 0; i < count; ++i) {
            at +=
                *rc::gen::inRange<std::int64_t>(0, std::int64_t{90} * chesscoach::seconds_per_day);
            const auto grade =
                *rc::gen::element(Grade::again, Grade::hard, Grade::good, Grade::easy);
            const auto reviewed =
                store.record_review(1, Color::white, card.id, card.key, grade, at);
            RC_ASSERT(reviewed.has_value());
            RC_ASSERT(reviewed->state == chesscoach::review(card.state, grade, at));
            card = *reviewed;
        }
        const auto stored = store.cards(1, Color::white);
        RC_ASSERT(stored == std::vector{card});
        RC_ASSERT(card.state == chesscoach::replay(card.created_at, store.card_history(card.id)));
    }));
}

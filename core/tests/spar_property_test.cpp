// Property test for chesscoach/spar.hpp (core; property): sessions on the spar fixture from
// generated seeds, colours, end moves and user moves (any legal move, by index into the sorted
// list) always replay from their seed and record to the same summary.
#include <chesscoach/opening_store.hpp>
#include <chesscoach/spar.hpp>

#include "spar_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>

#include <cstdint>
#include <string>

using chesscoach::Color;
using chesscoach::OpeningStore;
using chesscoach::Phase;
using chesscoach::Position;
using chesscoach::ProviderError;
using chesscoach::ReplyProvider;
using chesscoach::SparConfig;
using chesscoach::SparSession;
using chesscoach::Tier;
using chesscoach::testing::SparFixture;

namespace {

class LastLegal final : public ReplyProvider {
  public:
    tl::expected<std::string, ProviderError> reply(const Position& position) override {
        return position.legal_moves_uci().back();
    }
};

} // namespace

TEST_CASE("Every session replays from its seed and record", "[spar][property]") {
    const SparFixture fixture("spar_property");
    auto opened = OpeningStore::open(fixture.file());
    REQUIRE(opened.has_value());
    const OpeningStore& store = *opened;
    REQUIRE(rc::check("replay", [&] {
        const auto seed = *rc::gen::arbitrary<std::uint64_t>();
        const Color colour = *rc::gen::arbitrary<bool>() ? Color::white : Color::black;
        const SparConfig config{.end_move = *rc::gen::inRange(1, 8), .sampler = {}};
        auto session = SparSession::start(store, Tier::r1600_1799, colour, seed, config);
        RC_ASSERT(session.has_value());
        LastLegal engine;
        while (session->phase() != Phase::ended) {
            if (session->user_to_move()) {
                const auto legal = session->position().legal_moves_uci();
                const auto index = *rc::gen::inRange<std::size_t>(0, legal.size());
                RC_ASSERT(session->play_user_move(legal.at(index)).has_value());
            } else {
                RC_ASSERT(session->coach_reply(engine).has_value());
            }
        }
        const auto summary = session->summary();
        const auto replayed =
            SparSession::replay(store, Tier::r1600_1799, colour, seed, summary.moves, config);
        RC_ASSERT(replayed.has_value());
        RC_ASSERT(*replayed == summary);
    }));
}

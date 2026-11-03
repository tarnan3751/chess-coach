// Unit tests for chesscoach/spar.hpp (core; unit) on the spar fixture (spar_fixture.hpp): a
// scripted session meeting a Dubious interruption with its better choices and an off-book one,
// book replies equal to the sampler's draws, the provider's replies once off book, the end at
// end_move, the summary and its replay; a session off book from the start, a book position
// without candidates, a mate's result, the provider's failures, the replay's refusals, and the
// calls out of turn.
#include <chesscoach/opening_store.hpp>
#include <chesscoach/spar.hpp>

#include "spar_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using chesscoach::Color;
using chesscoach::Interruption;
using chesscoach::MoveError;
using chesscoach::OpeningStore;
using chesscoach::Phase;
using chesscoach::Position;
using chesscoach::ProviderError;
using chesscoach::ReplyProvider;
using chesscoach::sample_reply;
using chesscoach::sampler_candidates;
using chesscoach::SparConfig;
using chesscoach::SparError;
using chesscoach::SparSession;
using chesscoach::Splitmix64;
using chesscoach::Tier;
using chesscoach::testing::spar_position;
using chesscoach::testing::SparFixture;

namespace {

constexpr Tier tier = Tier::r1600_1799;
constexpr std::uint64_t seed = 20261103;

// Plays the first legal move, in UCI order, and counts its calls.
class FirstLegal final : public ReplyProvider {
  public:
    tl::expected<std::string, ProviderError> reply(const Position& position) override {
        ++calls;
        return position.legal_moves_uci().front();
    }
    int calls = 0;
};

// Plays the scripted moves in turn.
class Scripted final : public ReplyProvider {
  public:
    explicit Scripted(std::vector<std::string> moves) : moves_(std::move(moves)) {}
    tl::expected<std::string, ProviderError> reply(const Position& /*position*/) override {
        if (next_ == moves_.size()) {
            return tl::unexpected(ProviderError{.message = "no more moves"});
        }
        return moves_.at(next_++);
    }

  private:
    std::vector<std::string> moves_;
    std::size_t next_ = 0;
};

// The value of an optional the test has just required, as the static analyzer can follow.
template <typename T> const T& engaged(const std::optional<T>& value) {
    if (!value) {
        throw std::logic_error("an expected value is absent");
    }
    return *value;
}

OpeningStore open(const SparFixture& fixture) {
    auto store = OpeningStore::open(fixture.file());
    REQUIRE(store.has_value());
    return std::move(*store);
}

// The book reply a fresh Splitmix64 of the seed draws at the position, after `used` draws.
std::string sampled(const OpeningStore& store, const Position& position, int used = 0) {
    Splitmix64 generator(seed);
    for (int i = 0; i < used; ++i) {
        static_cast<void>(generator.next());
    }
    return sample_reply(sampler_candidates(store.moves(position.key(), tier), {}), generator)
        .value_or("none");
}

} // namespace

TEST_CASE("A scripted session meets a Dubious and an off-book interruption", "[spar]") {
    const SparFixture fixture("spar_scripted");
    const OpeningStore store = open(fixture);
    auto session = SparSession::start(store, tier, Color::white, seed,
                                      SparConfig{.end_move = 4, .sampler = {}});
    REQUIRE(session.has_value());
    CHECK(session->phase() == Phase::book);
    CHECK(session->user_to_move());
    CHECK(session->move_number() == 1);

    // 1. f3: Dubious, from 20 cp to -60, a loss of 80; the better choices by total.
    const auto dubious = session->play_user_move("f2f3");
    REQUIRE(dubious.has_value());
    CHECK(dubious->san == "f3");
    REQUIRE(dubious->interruption.has_value());
    const Interruption& first = engaged(dubious->interruption);
    CHECK(first.reason == Interruption::Reason::dubious);
    CHECK(first.played == "f2f3");
    CHECK(first.badges.raw() == 6);
    CHECK(first.loss == 80);
    REQUIRE(first.better.size() == 3);
    CHECK(first.better.at(0).san == "e4");
    CHECK(first.better.at(0).row.total() == 60);
    CHECK(first.better.at(1).san == "d4");
    CHECK(first.better.at(2).san == "Nf3");
    CHECK(session->phase() == Phase::book);

    // The coach's book reply is the sampler's first draw at the position after 1. f3.
    FirstLegal engine;
    const std::string expected_reply = sampled(store, spar_position("f3"));
    const auto reply = session->coach_reply(engine);
    REQUIRE(reply.has_value());
    CHECK(*reply == expected_reply);
    CHECK(engine.calls == 0);

    // 2. a3: never played at the tier, and its child has no node: off book.
    const auto off = session->play_user_move("a2a3");
    REQUIRE(off.has_value());
    REQUIRE(off->interruption.has_value());
    const Interruption& second = engaged(off->interruption);
    CHECK(second.reason == Interruption::Reason::off_book);
    CHECK(second.badges.raw() == 0);
    CHECK_FALSE(second.loss.has_value());
    REQUIRE(second.better.size() == 1);
    CHECK(second.better.at(0).san == "e4");
    CHECK(session->phase() == Phase::off_book);

    // Off book: the provider replies, and the user's moves are no longer interrupted.
    const auto provided = session->coach_reply(engine);
    REQUIRE(provided.has_value());
    CHECK(engine.calls == 1);
    const auto quiet = session->play_user_move("h2h3");
    REQUIRE(quiet.has_value());
    CHECK_FALSE(quiet->interruption.has_value());
    REQUIRE(session->coach_reply(engine).has_value());

    // Six plies: white to move at move 4, the end.
    CHECK(session->phase() == Phase::ended);
    CHECK_FALSE(session->user_to_move());
    CHECK(session->move_number() == 4);
    const auto summary = session->summary();
    CHECK(summary.moves.size() == 6);
    CHECK(summary.moves.at(0) == "f2f3");
    CHECK(summary.moves.at(1) == expected_reply);
    CHECK(summary.interruptions.size() == 2);
    CHECK(summary.phase_reached == Phase::off_book);
    CHECK_FALSE(summary.result.has_value());
    CHECK(summary.seed == seed);

    // The replay reproduces it, interruptions and phases included.
    const auto replayed = SparSession::replay(store, tier, Color::white, seed, summary.moves,
                                              SparConfig{.end_move = 4, .sampler = {}});
    REQUIRE(replayed.has_value());
    CHECK(*replayed == summary);
}

TEST_CASE("The coach's book replies are the sampler's draws, one output each", "[spar]") {
    const SparFixture fixture("spar_book");
    const OpeningStore store = open(fixture);
    auto session = SparSession::start(store, tier, Color::black, seed);
    REQUIRE(session.has_value());
    CHECK_FALSE(session->user_to_move());
    FirstLegal engine;
    const auto reply = session->coach_reply(engine);
    REQUIRE(reply.has_value());
    CHECK(*reply == sampled(store, Position::start()));
    CHECK(engine.calls == 0);
    // Nf3's child is off book: the coach's own move can leave the book, without an interruption.
    CHECK(session->phase() == (*reply == "g1f3" ? Phase::off_book : Phase::book));
    CHECK(session->summary().interruptions.empty());
}

TEST_CASE("A session starts off book without a node, and hands off without candidates", "[spar]") {
    const SparFixture fixture("spar_handoff");
    const OpeningStore store = open(fixture);
    auto empty_tier = SparSession::start(store, Tier::under_1200, Color::black, seed);
    REQUIRE(empty_tier.has_value());
    CHECK(empty_tier->phase() == Phase::off_book);
    FirstLegal engine;
    REQUIRE(empty_tier->coach_reply(engine).has_value());
    CHECK(engine.calls == 1);
    CHECK(empty_tier->summary().phase_reached == Phase::off_book);

    // At min_games 100 the start position, in book, has no candidate: the provider replies.
    auto strict = SparSession::start(store, tier, Color::black, seed,
                                     SparConfig{.end_move = 25, .sampler = {.min_games = 100}});
    REQUIRE(strict.has_value());
    CHECK(strict->phase() == Phase::book);
    FirstLegal second;
    const auto reply = strict->coach_reply(second);
    REQUIRE(reply.has_value());
    CHECK(second.calls == 1);
    CHECK(*reply == Position::start().legal_moves_uci().front());
    CHECK(strict->phase() == Phase::off_book);
}

TEST_CASE("A provider's failure in a book position without candidates changes nothing", "[spar]") {
    const SparFixture fixture("spar_no_candidate");
    const OpeningStore store = open(fixture);
    auto session = SparSession::start(store, tier, Color::white, seed,
                                      SparConfig{.end_move = 25, .sampler = {}});
    REQUIRE(session.has_value());
    FirstLegal never;
    REQUIRE(session->play_user_move("b1c3").has_value());
    REQUIRE(session->coach_reply(never) == "g8f6"); // the only candidate
    REQUIRE(session->play_user_move("g1f3").has_value());
    // 1. Nc3 Nf6 2. Nf3 is in book at the tier with no moves: the provider is asked, and fails.
    CHECK(session->phase() == Phase::book);
    const auto before = session->summary();
    Scripted failing({});
    CHECK(session->coach_reply(failing).error() == SparError::provider_failed);
    Scripted illegal({"e2e4"});
    CHECK(session->coach_reply(illegal).error() == SparError::provider_illegal_move);
    CHECK(session->phase() == Phase::book);
    CHECK(session->summary() == before);
    CHECK(session->position().key() == spar_position("Nc3 Nf6 Nf3").key());
    // A working provider plays, and only then is the game off book.
    FirstLegal engine;
    REQUIRE(session->coach_reply(engine).has_value());
    CHECK(engine.calls == 1);
    CHECK(session->phase() == Phase::off_book);
    CHECK(session->summary().phase_reached == Phase::off_book);
    CHECK(session->summary().moves.size() == 4);
}

TEST_CASE("A mate ends the session with its result, and replays", "[spar]") {
    const SparFixture fixture("spar_mate");
    const OpeningStore store = open(fixture);
    auto session = SparSession::start(store, Tier::under_1200, Color::black, seed);
    REQUIRE(session.has_value());
    Scripted engine({"f2f3", "g2g4"});
    REQUIRE(session->coach_reply(engine) == "f2f3");
    REQUIRE(session->play_user_move("e7e5").has_value());
    REQUIRE(session->coach_reply(engine) == "g2g4");
    REQUIRE(session->play_user_move("d8h4").has_value());
    CHECK(session->phase() == Phase::ended);
    const auto summary = session->summary();
    CHECK(summary.result == "0-1");
    CHECK(summary.moves == std::vector<std::string>{"f2f3", "e7e5", "g2g4", "d8h4"});
    const auto replayed =
        SparSession::replay(store, Tier::under_1200, Color::black, seed, summary.moves);
    REQUIRE(replayed.has_value());
    CHECK(*replayed == summary);
}

TEST_CASE("A provider's failure and illegal move are errors that change nothing", "[spar]") {
    const SparFixture fixture("spar_provider");
    const OpeningStore store = open(fixture);
    auto session = SparSession::start(store, Tier::under_1200, Color::black, seed);
    REQUIRE(session.has_value());
    Scripted failing({});
    CHECK(session->coach_reply(failing).error() == SparError::provider_failed);
    Scripted illegal({"e2e5"});
    CHECK(session->coach_reply(illegal).error() == SparError::provider_illegal_move);
    CHECK(session->summary().moves.empty());
    CHECK(session->position().key() == Position::start().key());
}

TEST_CASE("replay refuses a record the session does not reproduce", "[spar]") {
    const SparFixture fixture("spar_replay");
    const OpeningStore store = open(fixture);
    const SparConfig config{.end_move = 3, .sampler = {}};
    // The book reply after 1. e4 is drawn; recording the other one is a mismatch.
    const std::string drawn = sampled(store, spar_position("e4"));
    const std::string other = drawn == "e7e5" ? "c7c5" : "e7e5";
    CHECK(
        SparSession::replay(store, tier, Color::white, seed, {"e2e4", drawn}, config).has_value());
    CHECK(SparSession::replay(store, tier, Color::white, seed, {"e2e4", other}, config).error() ==
          SparError::replay_mismatch);
    CHECK(SparSession::replay(store, tier, Color::white, seed, {"e2e5"}, config).error() ==
          SparError::replay_illegal_move);
    // Past the end: at end_move 1 the session ends before the first move.
    CHECK(SparSession::replay(store, tier, Color::white, seed, {"e2e4"},
                              SparConfig{.end_move = 1, .sampler = {}})
              .error() == SparError::replay_mismatch);
}

TEST_CASE("Illegal moves, calls out of turn and a bad config are refused", "[spar]") {
    const SparFixture fixture("spar_refusals");
    const OpeningStore store = open(fixture);
    CHECK(SparSession::start(store, tier, Color::white, seed,
                             SparConfig{.end_move = 0, .sampler = {}})
              .error() == SparError::bad_config);
    CHECK(SparSession::start(store, tier, Color::white, seed,
                             SparConfig{.end_move = 25, .sampler = {.min_games = 0}})
              .error() == SparError::bad_config);
    auto session = SparSession::start(store, tier, Color::white, seed);
    REQUIRE(session.has_value());
    CHECK(session->play_user_move("e2e5").error() == MoveError::illegal);
    CHECK(session->play_user_move("e2").error() == MoveError::malformed);
    CHECK(session->summary().moves.empty());
    FirstLegal engine;
    CHECK_THROWS_AS(static_cast<void>(session->coach_reply(engine)), std::logic_error);
    REQUIRE(session->play_user_move("e2e4").has_value());
    CHECK_THROWS_AS(static_cast<void>(session->play_user_move("e7e5")), std::logic_error);
}

// SparSession: one sparring game against the coach at a tier (PLAN.md §5.4; docs/reference/
// core.md, **The spar session**). The user plays a colour from the start position; the coach
// replies with the sampler's draw from the tier's moves while the game is in book, and with a
// ReplyProvider (Stockfish at limited strength in the app) once it has left the book, which it
// never re-enters. While in book, a user move that carries the Dubious badge or leads off book at
// the tier (§7.3) is answered with an Interruption: the move's badges and loss and the better
// choices; the move stands either way. The session ends when the user is to move at the
// configured move number, or when the game is over. Deterministic: the session's Splitmix64,
// seeded with its seed, is used only for the coach's book replies, one output each, so replay()
// reproduces a session from its seed and recorded moves without the provider. Reads the store,
// which the caller keeps alive for the session's life; no clock.
#pragma once

#include <chesscoach/badges.hpp>
#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/random.hpp>
#include <chesscoach/sampler.hpp>
#include <chesscoach/tier.hpp>

#include <tl/expected.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chesscoach {

struct SparConfig {
    int end_move = 25;     // the session ends when the user is to move at this move number
    SamplerConfig sampler; // the coach's book replies
};

enum class SparError : std::uint8_t {
    bad_config,            // end_move or sampler.min_games below 1
    provider_failed,       // the ReplyProvider returned an error
    provider_illegal_move, // the ReplyProvider returned a move that is not legal
    replay_illegal_move,   // a recorded move is not legal where it is played
    replay_mismatch,       // a recorded coach move differs from the sampler's book reply, or the
                           // record runs past the session's end
};

enum class Phase : std::uint8_t {
    book,     // the position has a nodes row at the tier; the coach samples
    off_book, // the game has left the book; the provider replies
    ended,
};

// One of the moves the coach would rather have seen.
struct BetterChoice {
    std::string san;
    MoveRow row; // its stats at the tier

    friend bool operator==(const BetterChoice&, const BetterChoice&) = default;
};

struct Interruption {
    enum class Reason : std::uint8_t {
        dubious,  // the move carries the Dubious badge (also when it leads off book)
        off_book, // the move's child has no nodes row at the tier
    };
    Reason reason = Reason::off_book;
    std::string played;               // UCI
    Badges badges;                    // the played move's badges at the tier; none without a row
    std::optional<std::int64_t> loss; // evaluation_loss (score.hpp) of the move, when defined
    // The position's other moves at the tier that are Book, or Popular and not Dubious, by total
    // descending, then by move text.
    std::vector<BetterChoice> better;

    friend bool operator==(const Interruption&, const Interruption&) = default;
};

struct UserMoveOutcome {
    std::string san;
    std::optional<Interruption> interruption;
};

struct ProviderError {
    std::string message;
};

// Where the coach's replies come from once the game is off book: Phase 4b adapts the engine at
// limited strength; tests script one.
class ReplyProvider {
  public:
    ReplyProvider() = default;
    ReplyProvider(const ReplyProvider&) = delete;
    ReplyProvider& operator=(const ReplyProvider&) = delete;
    ReplyProvider(ReplyProvider&&) = delete;
    ReplyProvider& operator=(ReplyProvider&&) = delete;
    virtual ~ReplyProvider() = default;

    // A legal move of the side to move, in UCI.
    [[nodiscard]] virtual tl::expected<std::string, ProviderError>
    reply(const Position& position) = 0;
};

struct Summary {
    std::vector<std::string> moves;          // every move from the start, UCI, both sides
    std::vector<Interruption> interruptions; // in the order they happened
    Phase phase_reached = Phase::book;       // off_book when the game left the book, else book
    std::optional<std::string> result;       // "1-0", "0-1" or "1/2-1/2" after mate or
                                             // stalemate; nullopt when ended at end_move
    std::uint64_t seed = 0;

    friend bool operator==(const Summary&, const Summary&) = default;
};

class SparSession {
  public:
    // A session at the start position, in book when the start position has a nodes row at the
    // tier and off book otherwise; the coach moves first when the user plays black.
    [[nodiscard]] static tl::expected<SparSession, SparError> start(const OpeningStore& store,
                                                                    Tier tier, Color user_colour,
                                                                    std::uint64_t seed,
                                                                    const SparConfig& config = {});

    // Re-runs a session from its seed and recorded moves (Summary::moves): the user's moves and
    // the coach's off-book moves are played as recorded, the coach's book replies are drawn again
    // and must equal the record. The summary, interruptions and phases included, is the
    // original's when the record is.
    [[nodiscard]] static tl::expected<Summary, SparError>
    replay(const OpeningStore& store, Tier tier, Color user_colour, std::uint64_t seed,
           const std::vector<std::string>& moves, const SparConfig& config = {});

    [[nodiscard]] const Position& position() const noexcept { return position_; }
    [[nodiscard]] Phase phase() const noexcept { return phase_; }
    [[nodiscard]] Tier tier() const noexcept { return tier_; }
    [[nodiscard]] Color user_colour() const noexcept { return user_colour_; }
    // Whether the user is to move; false once the session has ended.
    [[nodiscard]] bool user_to_move() const noexcept;
    // The move number the game is at: 1 at the start, one more after each black move.
    [[nodiscard]] int move_number() const noexcept;

    // Plays the user's move (UCI) when user_to_move(); an illegal or malformed move is an error
    // and changes nothing. Calling it when the user is not to move is a programming error
    // (std::logic_error).
    [[nodiscard]] tl::expected<UserMoveOutcome, MoveError> play_user_move(std::string_view uci);

    // Plays the coach's reply when the session has not ended and the user is not to move: the
    // sampler's draw while in book; the provider's move once off book, or when the book position
    // has no candidate, which leaves the book. Returns the reply's UCI. Calling it otherwise is a
    // programming error (std::logic_error).
    [[nodiscard]] tl::expected<std::string, SparError> coach_reply(ReplyProvider& provider);

    [[nodiscard]] Summary summary() const;

  private:
    SparSession(const OpeningStore& store, Tier tier, Color user_colour, std::uint64_t seed,
                const SparConfig& config);

    void leave_book();
    // Records a game over, or the end at end_move, after every move.
    void finish_move();

    const OpeningStore* store_;
    Tier tier_;
    Color user_colour_;
    SparConfig config_;
    Splitmix64 generator_;
    Position position_;
    Phase phase_ = Phase::book;
    Phase reached_ = Phase::book; // the furthest of book and off_book
    std::vector<std::string> moves_;
    std::vector<Interruption> interruptions_;
    std::optional<std::string> result_;
    std::uint64_t seed_;
};

} // namespace chesscoach

// The spar session (chesscoach/spar.hpp).
#include <chesscoach/score.hpp>
#include <chesscoach/spar.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace chesscoach {

namespace {

// The result of a game over, or nullopt while the side to move has a legal move.
std::optional<std::string> result_of(const Position& position) {
    if (!position.legal_moves_uci().empty()) {
        return std::nullopt;
    }
    if (!position.in_check()) {
        return "1/2-1/2";
    }
    return position.side_to_move() == Color::white ? "0-1" : "1-0";
}

// The position's other moves at the tier the coach would rather have seen: Book, or Popular and
// not Dubious; by total descending, then by move text.
std::vector<BetterChoice> better_choices(const Position& position,
                                         const std::vector<MoveRow>& moves,
                                         const std::string& played) {
    std::vector<BetterChoice> better;
    for (const MoveRow& row : moves) {
        const bool wanted = row.badges.has(Badge::book) ||
                            (row.badges.has(Badge::popular) && !row.badges.has(Badge::dubious));
        if (row.move == played || !wanted) {
            continue;
        }
        if (auto san = position.san_of(row.move)) {
            better.push_back(BetterChoice{.san = std::move(*san), .row = row});
        }
    }
    std::ranges::sort(better, [](const BetterChoice& a, const BetterChoice& b) {
        return a.row.total() != b.row.total() ? a.row.total() > b.row.total()
                                              : a.row.move < b.row.move;
    });
    return better;
}

// Replays a recorded move as the coach's off-book reply.
class RecordedReply final : public ReplyProvider {
  public:
    explicit RecordedReply(std::string move) : move_(std::move(move)) {}

    tl::expected<std::string, ProviderError> reply(const Position& /*position*/) override {
        return move_;
    }

  private:
    std::string move_;
};

} // namespace

std::int64_t opening_data_version(const OpeningStore& store) {
    const std::string* text = store.meta().find("data_version");
    if (text == nullptr || text->empty() || text->size() > 18 ||
        text->find_first_not_of("0123456789") != std::string::npos) {
        return 0;
    }
    return std::stoll(*text);
}

SparSession::SparSession(const OpeningStore& store, Tier tier, Color user_colour,
                         std::uint64_t seed, const SparConfig& config)
    : store_(&store), tier_(tier), user_colour_(user_colour), config_(config), generator_(seed),
      position_(Position::start()), seed_(seed), data_version_(opening_data_version(store)) {
    if (!store_->node(position_.key(), tier_)) {
        phase_ = Phase::off_book;
        reached_ = Phase::off_book;
    }
    finish_move();
}

tl::expected<SparSession, SparError> SparSession::start(const OpeningStore& store, Tier tier,
                                                        Color user_colour, std::uint64_t seed,
                                                        const SparConfig& config) {
    if (config.end_move < 1 || config.sampler.min_games < 1) {
        return tl::unexpected(SparError::bad_config);
    }
    return SparSession(store, tier, user_colour, seed, config);
}

bool SparSession::user_to_move() const noexcept {
    return phase_ != Phase::ended && position_.side_to_move() == user_colour_;
}

int SparSession::move_number() const noexcept { return 1 + static_cast<int>(moves_.size() / 2); }

void SparSession::leave_book() {
    phase_ = Phase::off_book;
    reached_ = Phase::off_book;
}

void SparSession::finish_move() {
    result_ = result_of(position_);
    if (result_ ||
        (position_.side_to_move() == user_colour_ && move_number() >= config_.end_move)) {
        phase_ = Phase::ended;
    }
}

tl::expected<UserMoveOutcome, MoveError> SparSession::play_user_move(std::string_view uci) {
    if (!user_to_move()) {
        throw std::logic_error("SparSession::play_user_move: the user is not to move");
    }
    const Position before = position_;
    const bool in_book = phase_ == Phase::book;
    const auto played = position_.play_uci(uci);
    if (!played) {
        return tl::unexpected(played.error());
    }
    moves_.push_back(played->uci);
    UserMoveOutcome outcome{.san = played->san, .interruption = std::nullopt};
    if (in_book) {
        const std::vector<MoveRow> rows = store_->moves(before.key(), tier_);
        const auto row = std::ranges::find(rows, played->uci, &MoveRow::move);
        const bool has_row = row != rows.end();
        const bool dubious = has_row && row->badges.has(Badge::dubious);
        const bool child_in_book = store_->node(played->child, tier_).has_value();
        if (dubious || !child_in_book) {
            const auto parent = store_->position(before.key());
            Interruption interruption{
                .reason = dubious ? Interruption::Reason::dubious : Interruption::Reason::off_book,
                .played = played->uci,
                .badges = has_row ? row->badges : Badges{},
                .loss = parent ? evaluation_loss(*parent, store_->position(played->child))
                               : std::nullopt,
                .better = better_choices(before, rows, played->uci),
            };
            interruptions_.push_back(interruption);
            outcome.interruption = std::move(interruption);
        }
        if (!child_in_book) {
            leave_book();
        }
    }
    finish_move();
    return outcome;
}

tl::expected<std::string, SparError> SparSession::coach_reply(ReplyProvider& provider) {
    if (phase_ == Phase::ended || user_to_move()) {
        throw std::logic_error("SparSession::coach_reply: the coach is not to move");
    }
    std::optional<std::string> reply;
    if (phase_ == Phase::book) {
        reply = sample_reply(
            sampler_candidates(store_->moves(position_.key(), tier_), config_.sampler), generator_);
    }
    if (!reply) {
        // Off book, or a book position without a candidate: the provider's move. Nothing changes,
        // the phase included, until it has played.
        auto provided = provider.reply(position_);
        if (!provided) {
            return tl::unexpected(SparError::provider_failed);
        }
        const auto played = position_.play_uci(*provided);
        if (!played) {
            return tl::unexpected(SparError::provider_illegal_move);
        }
        leave_book();
        moves_.push_back(played->uci);
        finish_move();
        return played->uci;
    }
    const auto played = position_.play_uci(*reply);
    if (!played) {
        throw std::runtime_error("SparSession::coach_reply: the opening database holds the move " +
                                 *reply + ", which is not legal in " + position_.fen());
    }
    moves_.push_back(played->uci);
    if (!store_->node(played->child, tier_)) {
        leave_book();
    }
    finish_move();
    return played->uci;
}

Summary SparSession::summary() const {
    return Summary{
        .moves = moves_,
        .interruptions = interruptions_,
        .phase_reached = reached_,
        .result = result_,
        .seed = seed_,
    };
}

tl::expected<Summary, SparError> SparSession::replay(const OpeningStore& store, Tier tier,
                                                     Color user_colour, std::uint64_t seed,
                                                     const std::vector<std::string>& moves,
                                                     const SparConfig& config) {
    auto session = start(store, tier, user_colour, seed, config);
    if (!session) {
        return tl::unexpected(session.error());
    }
    for (const std::string& move : moves) {
        if (session->phase() == Phase::ended) {
            return tl::unexpected(SparError::replay_mismatch);
        }
        if (session->user_to_move()) {
            if (!session->play_user_move(move)) {
                return tl::unexpected(SparError::replay_illegal_move);
            }
            continue;
        }
        RecordedReply recorded(move);
        const auto reply = session->coach_reply(recorded);
        if (!reply) {
            return tl::unexpected(reply.error() == SparError::provider_illegal_move
                                      ? SparError::replay_illegal_move
                                      : reply.error());
        }
        if (*reply != move) {
            return tl::unexpected(SparError::replay_mismatch);
        }
    }
    return session->summary();
}

} // namespace chesscoach

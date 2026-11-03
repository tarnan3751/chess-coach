// The spar worker (spar_worker.hpp).
#include "spar_worker.hpp"

#include <chesscoach/engine_reply_provider.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/tier.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace chesscoach::app {

namespace {

QString translate(const char* text) {
    return QCoreApplication::translate("chesscoach::app::SparWorker", text);
}

QString phase_text(Phase phase) {
    switch (phase) {
    case Phase::book:
        return translate("In book");
    case Phase::off_book:
        return translate("Off book: engine replies");
    case Phase::ended:
        return translate("Ended");
    }
    return {};
}

QString badge_letters(Badges badges) {
    QString letters;
    if (badges.has(Badge::book)) {
        letters += QLatin1Char('B');
    }
    if (badges.has(Badge::popular)) {
        letters += QLatin1Char('P');
    }
    if (badges.has(Badge::dubious)) {
        letters += QLatin1Char('D');
    }
    return letters;
}

// The interruption as the window shows it; played_san is the move's SAN where it was played.
SparInterruption converted(const Interruption& interruption, const QString& played_san) {
    SparInterruption out;
    out.dubious = interruption.reason == Interruption::Reason::dubious;
    out.reason = out.dubious ? translate("Dubious move") : translate("Off book");
    out.played_san = played_san;
    out.played_uci = QString::fromStdString(interruption.played);
    out.badges = badge_letters(interruption.badges);
    out.loss = interruption.loss;
    for (const BetterChoice& choice : interruption.better) {
        out.better.append(SparChoice{
            .san = QString::fromStdString(choice.san),
            .uci = QString::fromStdString(choice.row.move),
            .badges = badge_letters(choice.row.badges),
            .total = choice.row.total(),
            .white = choice.row.white,
            .draws = choice.row.draws,
            .black = choice.row.black,
        });
    }
    return out;
}

std::int64_t now() { return QDateTime::currentSecsSinceEpoch(); }

// A read of a corrupt opening database throws (OpeningStore, SparSession::coach_reply).
QString unreadable() {
    return translate("The opening database could not be read; end the session to save it.");
}

} // namespace

void register_spar_types() {
    qRegisterMetaType<SparChoice>();
    qRegisterMetaType<SparInterruption>();
    qRegisterMetaType<std::optional<SparInterruption>>();
    qRegisterMetaType<SparState>();
    qRegisterMetaType<SparSummary>();
    qRegisterMetaType<SparSessionInfo>();
    qRegisterMetaType<QVector<SparSessionInfo>>();
    qRegisterMetaType<SparReplay>();
}

// The coach once off book: the worker's engine, started at the first reply asked of it with the
// engine settings, through an EngineReplyProvider at the session's tier.
class SparWorker::Coach final : public ReplyProvider {
  public:
    Coach(EngineSettings settings, double seconds)
        : settings_(std::move(settings)), seconds_(seconds) {}

    void set_tier(Tier tier) { tier_ = tier; }

    tl::expected<std::string, ProviderError> reply(const Position& position) override {
        if (!engine_.has_value() || !engine_->is_alive()) {
            engine_.reset();
            const auto binary = find_engine_binary(settings_);
            if (!binary) {
                return tl::unexpected(ProviderError{translate("no engine found").toStdString()});
            }
            EngineOptions options;
            options.threads = settings_.threads;
            options.hash_mb = settings_.hash_mb;
            auto started = Engine::start(*binary, options);
            if (!started) {
                return tl::unexpected(
                    ProviderError{translate("the engine did not start").toStdString()});
            }
            engine_.emplace(std::move(*started));
        }
        EngineReplyProvider provider(*engine_, tier_,
                                     ProviderConfig{.seconds = seconds_, .elo_offset = 100});
        return provider.reply(position);
    }

  private:
    EngineSettings settings_;
    double seconds_;
    Tier tier_{};
    std::optional<Engine> engine_;
};

SparWorker::SparWorker(const QString& data_dir, QString db, QObject* parent)
    : QObject(parent), data_dir_(data_dir.toStdString()), db_(std::move(db)),
      coach_(std::make_unique<Coach>(engine_settings_, seconds_)) {}

SparWorker::~SparWorker() = default;

void SparWorker::configure(const EngineSettings& engine, double seconds) {
    engine_settings_ = engine;
    seconds_ = seconds;
    coach_ = std::make_unique<Coach>(engine_settings_, seconds_);
}

const OpeningStore* SparWorker::open_store() {
    if (store_.has_value()) {
        return &*store_;
    }
    if (db_.isEmpty()) {
        return nullptr;
    }
    auto store = OpeningStore::open(std::filesystem::path(db_.toStdString()));
    if (!store) {
        return nullptr;
    }
    return &store_.emplace(std::move(*store));
}

ProfileStore* SparWorker::open_profiles(bool create) {
    if (profiles_.has_value()) {
        return &*profiles_;
    }
    if (!create && !QFileInfo::exists(QDir(QString::fromStdString(data_dir_.string()))
                                          .filePath(QStringLiteral("profiles.db")))) {
        return nullptr;
    }
    auto profiles = ProfileStore::open(data_dir_);
    if (!profiles) {
        return nullptr;
    }
    return &profiles_.emplace(std::move(*profiles));
}

std::optional<std::int64_t> SparWorker::default_profile(const ProfileStore& profiles) {
    const std::vector<Profile> all = profiles.profiles();
    const auto named = std::ranges::find(all, std::string("Default"), &Profile::name);
    if (named != all.end()) {
        return named->id;
    }
    if (all.empty()) {
        return std::nullopt;
    }
    return all.front().id;
}

SparState SparWorker::state() const {
    if (!session_.has_value()) {
        return SparState{
            .phase = phase_text(Phase::ended),
            .move_number = 1,
            .user_to_move = false,
            .ended = true,
        };
    }
    return SparState{
        .phase = phase_text(session_->phase()),
        .move_number = session_->move_number(),
        .user_to_move = session_->user_to_move(),
        .ended = session_->phase() == Phase::ended,
    };
}

void SparWorker::start(int tier, bool user_white, quint64 seed, int end_move) {
    if (session_.has_value()) {
        finish(); // the running session is saved as stopped
    }
    const auto at = tier_from_index(tier);
    const OpeningStore* store = open_store();
    if (!at.has_value() || store == nullptr) {
        emit session_failed(tr("Sparring needs an opening database: open one with --db."));
        return;
    }
    try {
        auto started = SparSession::start(*store, *at, user_white ? Color::white : Color::black,
                                          seed, SparConfig{.end_move = end_move, .sampler = {}});
        if (!started) {
            emit session_failed(tr("The session could not start."));
            return;
        }
        session_.emplace(std::move(*started));
    } catch (const std::exception&) {
        emit session_failed(unreadable());
        return;
    }
    const SparSession& session = *session_;
    started_at_ = now();
    coach_->set_tier(*at);
    emit session_started(seed, QString::fromStdString(session.position().fen()), state());
    if (session.phase() != Phase::ended && !session.user_to_move()) {
        coach_turn();
    }
}

void SparWorker::user_move(const QString& uci) {
    if (!session_.has_value() || !session_->user_to_move()) {
        return;
    }
    SparSession& session = *session_;
    try {
        const auto outcome = session.play_user_move(uci.toStdString());
        if (!outcome) {
            emit session_failed(tr("The move %1 is not legal here.").arg(uci));
            return;
        }
        const UserMoveOutcome& played = *outcome;
        const QString san = QString::fromStdString(played.san);
        std::optional<SparInterruption> interruption;
        if (played.interruption.has_value()) {
            interruption = converted(*played.interruption, san);
        }
        emit move_accepted(san, uci, interruption, state());
    } catch (const std::exception&) {
        emit session_failed(unreadable());
        return;
    }
    if (session.phase() == Phase::ended) {
        finish();
        return;
    }
    coach_turn();
}

void SparWorker::coach_turn() {
    if (!session_.has_value()) {
        return;
    }
    SparSession& session = *session_;
    const Position before = session.position();
    try {
        const auto reply = session.coach_reply(*coach_);
        if (!reply) {
            emit session_failed(tr("The coach could not reply; end the session to save it."));
            return;
        }
        const auto san = before.san_of(*reply);
        emit coach_moved(QString::fromStdString(san.value_or(*reply)),
                         QString::fromStdString(*reply), state());
    } catch (const std::exception&) {
        emit session_failed(unreadable());
        return;
    }
    if (session.phase() == Phase::ended) {
        finish();
    }
}

void SparWorker::end_session() {
    if (session_.has_value()) {
        finish();
    }
}

void SparWorker::finish() {
    if (!session_.has_value()) {
        return;
    }
    const SparSession& session = *session_;
    const Summary summary = session.summary();
    const bool ended = session.phase() == Phase::ended;
    SparSummary out;
    out.seed = summary.seed;
    out.interruptions = static_cast<int>(summary.interruptions.size());
    out.phase_reached = phase_text(summary.phase_reached);
    out.result = QString::fromStdString(summary.result.value_or(""));
    out.ended = ended;
    Position position = Position::start();
    for (const std::string& move : summary.moves) {
        const auto played = position.play_uci(move);
        out.moves_san << QString::fromStdString(played ? played->san : move);
    }
    try {
        if (ProfileStore* profiles = open_profiles(true)) {
            if (const auto profile = default_profile(*profiles)) {
                const auto saved = profiles->save_spar_session(
                    *profile, session, started_at_,
                    ended ? std::optional<std::int64_t>(now()) : std::nullopt);
                if (saved) {
                    out.saved_id = *saved;
                }
            }
        }
    } catch (const std::exception&) {
        profiles_.reset();
    }
    session_.reset();
    emit session_ended(out);
    if (out.saved_id == 0) {
        emit session_failed(tr("The session could not be saved."));
    }
    list_sessions();
}

void SparWorker::list_sessions() {
    QVector<SparSessionInfo> sessions;
    try {
        const ProfileStore* profiles = open_profiles(false);
        const auto profile =
            profiles != nullptr ? default_profile(*profiles) : std::optional<std::int64_t>();
        if (profiles != nullptr && profile.has_value()) {
            for (const SparSessionRow& row : profiles->spar_sessions(*profile)) {
                QString result = tr("stopped");
                if (row.phase == Phase::ended) {
                    result = QString::fromStdString(row.result.value_or(""));
                }
                sessions.append(SparSessionInfo{
                    .id = row.id,
                    .started_at = row.started_at,
                    .tier = tier_index(row.tier),
                    .user_white = row.colour == Color::white,
                    .moves = static_cast<int>(row.moves.size()),
                    .interruptions = row.interruptions,
                    .result = result,
                });
            }
        }
    } catch (const std::exception&) {
        profiles_.reset();
    }
    std::ranges::reverse(sessions); // the newest first
    emit sessions_ready(sessions);
}

void SparWorker::replay_session(qint64 id) {
    try {
        const ProfileStore* profiles = open_profiles(false);
        const OpeningStore* store = open_store();
        if (profiles == nullptr || store == nullptr) {
            emit session_failed(tr("The session cannot be replayed without its databases."));
            return;
        }
        const auto found = profiles->spar_session(id);
        if (!found.has_value()) {
            emit session_failed(tr("No saved session %1.").arg(id));
            return;
        }
        const SparSessionRow& row = *found;
        const auto replayed = replay_spar_session(*store, row);
        if (!replayed.has_value()) {
            if (replayed.error() == SparError::replay_database_mismatch) {
                emit session_failed(tr("The saved session was played against another build of "
                                       "the opening database."));
            } else {
                emit session_failed(tr("The saved session does not replay on this database."));
            }
            return;
        }
        const std::vector<Interruption>& interruptions = replayed->interruptions;
        SparReplay replay;
        replay.id = row.id;
        replay.tier = tier_index(row.tier);
        replay.user_white = row.colour == Color::white;
        Position position = Position::start();
        std::size_t next = 0; // the next interruption to place
        for (std::size_t ply = 0; ply < row.moves.size(); ++ply) {
            const std::string& move = row.moves.at(ply);
            const bool users = (ply % 2 == 0) == replay.user_white;
            const auto played = position.play_uci(move);
            SparReplayPly out;
            out.uci = QString::fromStdString(move);
            out.san = QString::fromStdString(played ? played->san : move);
            if (users && next < interruptions.size() && interruptions.at(next).played == move) {
                out.interruption = converted(interruptions.at(next), out.san);
                ++next;
            }
            replay.plies.append(out);
        }
        emit replay_ready(replay);
    } catch (const std::exception&) {
        profiles_.reset();
        emit session_failed(tr("The saved session could not be read."));
    }
}

} // namespace chesscoach::app

// The spar worker: a QObject on its own QThread for the window's life, owning the live
// SparSession and everything it needs (docs/reference/app.md, **Spar**): its own read-only
// OpeningStore connection to --db, its own Engine (started at the first off-book reply, separate
// from the engine worker's, and searched at limited strength through EngineReplyProvider), and
// a ProfileStore connection that saves each session when it ends or is stopped. The window calls
// its slots by queued connection and gets every answer back the same way; the GUI thread never
// touches the session, the store or the engine.
#pragma once

#include <chesscoach/engine.hpp>
#include <chesscoach/opening_store.hpp>
#include <chesscoach/profile_store.hpp>
#include <chesscoach/spar.hpp>

#include "engine_worker.hpp"
#include "spar_data.hpp"

#include <QObject>
#include <QString>
#include <QtGlobal>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

namespace chesscoach::app {

class SparWorker : public QObject {
    Q_OBJECT

  public:
    // data_dir holds profiles.db, opened when a session is saved or the list is asked for and it
    // exists; db is the opening database, opened read-only at the first start.
    SparWorker(const QString& data_dir, QString db, QObject* parent = nullptr);
    ~SparWorker() override;
    Q_DISABLE_COPY_MOVE(SparWorker)

    // Thread-safe, for the window as it closes: the coach's search in progress stops at once, and
    // the coach searches no more.
    void stop_coach();

    // The engine for off-book replies and each reply's seconds; a running engine is dropped.
    Q_SLOT void configure(const chesscoach::app::EngineSettings& engine, double seconds);
    // A new session (a running one is saved as stopped first): session_started, then, when the
    // user plays black, the coach's first move.
    Q_SLOT void start(int tier, bool user_white, quint64 seed, int end_move);
    // The user's move (UCI): move_accepted, then the coach's reply unless the session ended.
    Q_SLOT void user_move(const QString& uci);
    // Saves the running session as stopped and answers session_ended.
    Q_SLOT void end_session();
    // The Default profile's saved sessions: sessions_ready.
    Q_SLOT void list_sessions();
    // A saved session played again from its seed: replay_ready with its interruptions.
    Q_SLOT void replay_session(qint64 id);

  signals:
    void session_started(quint64 seed, const QString& fen, const chesscoach::app::SparState& state);
    void move_accepted(const QString& san, const QString& uci,
                       const std::optional<chesscoach::app::SparInterruption>& interruption,
                       const chesscoach::app::SparState& state);
    void coach_moved(const QString& san, const QString& uci,
                     const chesscoach::app::SparState& state);
    void session_ended(const chesscoach::app::SparSummary& summary);
    void session_failed(const QString& message);
    void sessions_ready(const QVector<chesscoach::app::SparSessionInfo>& sessions);
    void replay_ready(const chesscoach::app::SparReplay& replay);

  private:
    class Coach;

    [[nodiscard]] const OpeningStore* open_store();
    [[nodiscard]] ProfileStore* open_profiles(bool create);
    [[nodiscard]] static std::optional<std::int64_t> default_profile(const ProfileStore& profiles);
    [[nodiscard]] SparState state() const;
    void coach_turn();
    void finish();

    std::filesystem::path data_dir_;
    QString db_;
    EngineSettings engine_settings_;
    double seconds_ = 0.5;
    std::optional<OpeningStore> store_;
    std::optional<ProfileStore> profiles_;
    std::mutex coach_mutex_; // guards coach_'s replacement against stop_coach()
    std::unique_ptr<Coach> coach_;
    std::optional<SparSession> session_;
    std::int64_t started_at_ = 0;
};

} // namespace chesscoach::app

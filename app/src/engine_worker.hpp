// The engine worker: a QObject that lives on its own QThread for the window's life, owning one
// core Engine (Stockfish over UCI, started at the first search and again after it dies or times
// out) and its own ProfileStore connection for the engine cache (docs/reference/app.md, **Engine
// worker**). Every evaluation looks in the cache first and searches only on a miss, storing what
// it finds; its slots run one at a time on its thread, so there is never more than one search.
// The GUI thread reaches it by queued connection, and calls cancel_before directly, which stops
// the search in progress (Engine::stop may come from any thread) and makes older requests
// stale. Lines leave it with their scores from white's side, as openings.db's are.
#pragma once

#include <chesscoach/engine.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/profile_store.hpp>

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>

namespace chesscoach::app {

struct EngineSettings {
    // The engine's path; empty for the default: CHESS_COACH_STOCKFISH, else stockfish on the
    // PATH, else stockfish beside the app (the bundle's, from Phase 6).
    QString binary;
    int threads = 1;
    int hash_mb = 64;
    double seconds = 2.0;
    int lines = 3;
    friend bool operator==(const EngineSettings&, const EngineSettings&) = default;
};

// One line of a search: scores from white's side (negated from UCI's side-to-move scores when
// black is to move), the principal variation in SAN.
struct EngineLine {
    int multipv = 1;
    int depth = 0;
    std::optional<int> cp;
    std::optional<int> mate;
    QString pv_san;
    friend bool operator==(const EngineLine&, const EngineLine&) = default;
};

// Registers EngineSettings and the lines for queued connections; idempotent.
void register_engine_types();

// The engine to run: the settings' binary; else CHESS_COACH_STOCKFISH; else stockfish on the
// PATH; else stockfish beside the app (the bundle's, from Phase 6). nullopt when the one chosen
// is not a file.
[[nodiscard]] std::optional<std::filesystem::path>
find_engine_binary(const EngineSettings& settings);

class EngineWorker : public QObject {
    Q_OBJECT

  public:
    // The least depth a cached search needs to be served, as the CLI's eval.
    static constexpr int min_cached_depth = 12;

    // data_dir holds profiles.db, opened only when it exists or when a search is to be stored.
    explicit EngineWorker(const QString& data_dir, QObject* parent = nullptr);

    // From the GUI thread: requests before request_id are dropped unanswered, and the search in
    // progress, if any, stops.
    void cancel_before(int request_id);

    // The settings of the next search; a running engine is dropped, so the next search starts
    // one with them.
    Q_SLOT void configure(const chesscoach::app::EngineSettings& settings);
    // Answers lines_ready for the position, from the cache or a search, unless a later request
    // has come since; engine_state follows the engine; engine_failed when it cannot run.
    Q_SLOT void evaluate(quint64 key, const QString& fen, int request_id);

  signals:
    // "not found", the engine's name when it is idle, or "searching".
    void engine_state(const QString& text);
    void lines_ready(quint64 key, int request_id, const QVector<chesscoach::app::EngineLine>& lines,
                     bool cached);
    void engine_failed(const QString& message);

  private:
    [[nodiscard]] bool stale(int request_id) const { return request_id < latest_request_.load(); }
    // The cache connection, opened when profiles.db exists or create is set; nullptr when it
    // cannot be.
    [[nodiscard]] ProfileStore* open_profiles(bool create);
    void drop_engine();

    std::filesystem::path data_dir_;
    EngineSettings settings_;
    std::optional<ProfileStore> profiles_;
    std::atomic<int> latest_request_{0};
    // Guards engine_ against being replaced while the GUI thread stops it; the worker's own
    // search does not hold it.
    std::mutex engine_mutex_;
    std::optional<Engine> engine_;
};

} // namespace chesscoach::app

Q_DECLARE_METATYPE(chesscoach::app::EngineSettings)
Q_DECLARE_METATYPE(chesscoach::app::EngineLine)

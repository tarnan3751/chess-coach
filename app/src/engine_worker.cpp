// The engine worker (engine_worker.hpp).
#include "engine_worker.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QtGlobal>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace chesscoach::app {

namespace {

QString translate(const char* text) {
    return QCoreApplication::translate("chesscoach::app::EngineWorker", text);
}

QString describe(EngineError error) {
    switch (error) {
    case EngineError::start_failed:
        return translate("did not start");
    case EngineError::timeout:
        return translate("did not answer in time");
    case EngineError::died:
        return translate("died");
    case EngineError::busy:
        return translate("was busy");
    case EngineError::unsupported:
        return translate("does not support the request");
    case EngineError::invalid_request:
        return translate("was given an invalid request");
    }
    return translate("failed");
}

std::optional<std::filesystem::path> existing(const QString& path) {
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        return std::nullopt;
    }
    return std::filesystem::path(path.toStdString());
}

// The principal variation in SAN, at most max_moves of it; a move that does not play ends it,
// written in UCI.
QString pv_san(const Position& position, const std::vector<std::string>& pv) {
    constexpr std::size_t max_moves = 10;
    Position current = position;
    QStringList moves;
    for (std::size_t i = 0; i < pv.size() && i < max_moves; ++i) {
        const auto played = current.play_uci(pv.at(i));
        if (!played) {
            moves << QString::fromStdString(pv.at(i));
            break;
        }
        moves << QString::fromStdString(played->san);
    }
    if (pv.size() > max_moves) {
        moves << QStringLiteral("…");
    }
    return moves.join(QLatin1Char(' '));
}

QVector<EngineLine> white_side_lines(const Position& position,
                                     const std::vector<SearchLine>& lines) {
    const int sign = position.side_to_move() == Color::white ? 1 : -1;
    QVector<EngineLine> converted;
    for (const SearchLine& line : lines) {
        EngineLine out;
        out.multipv = line.multipv;
        out.depth = line.depth;
        if (line.score_cp) {
            out.cp = sign * *line.score_cp;
        }
        if (line.score_mate) {
            out.mate = sign * *line.score_mate;
        }
        out.pv_san = pv_san(position, line.pv);
        converted.append(out);
    }
    return converted;
}

} // namespace

void register_engine_types() {
    qRegisterMetaType<EngineSettings>();
    qRegisterMetaType<EngineLine>();
    qRegisterMetaType<QVector<EngineLine>>();
}

EngineWorker::EngineWorker(const QString& data_dir, QObject* parent)
    : QObject(parent), data_dir_(data_dir.toStdString()) {}

void EngineWorker::cancel_before(int request_id) {
    latest_request_.store(request_id);
    const std::scoped_lock lock(engine_mutex_);
    if (engine_) {
        engine_->stop_before(request_id);
    }
}

void EngineWorker::close_stores() { profiles_.reset(); }

void EngineWorker::configure(const EngineSettings& settings) {
    settings_ = settings;
    drop_engine();
}

std::optional<std::filesystem::path> find_engine_binary(const EngineSettings& settings) {
    if (!settings.binary.isEmpty()) {
        return existing(settings.binary);
    }
    if (const QString named = qEnvironmentVariable("CHESS_COACH_STOCKFISH"); !named.isEmpty()) {
        return existing(named);
    }
    if (const QString found = QStandardPaths::findExecutable(QStringLiteral("stockfish"));
        !found.isEmpty()) {
        return existing(found);
    }
    return existing(
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("stockfish")));
}

ProfileStore* EngineWorker::open_profiles(bool create) {
    if (profiles_.has_value()) {
        return &*profiles_;
    }
    if (!create && !QFileInfo::exists(QDir(QString::fromStdString(data_dir_.string()))
                                          .filePath(QStringLiteral("profiles.db")))) {
        return nullptr;
    }
    auto profiles = ProfileStore::open(data_dir_);
    if (!profiles) {
        emit engine_failed(tr("The engine cache in %1 cannot be opened")
                               .arg(QString::fromStdString(data_dir_.string())));
        return nullptr;
    }
    return &profiles_.emplace(std::move(*profiles));
}

void EngineWorker::drop_engine() {
    std::optional<Engine> dropped;
    {
        const std::scoped_lock lock(engine_mutex_);
        dropped = std::move(engine_);
        engine_.reset();
    }
    // dropped quits its process here, outside the lock.
}

void EngineWorker::evaluate(quint64 key, const QString& fen, int request_id) {
    if (stale(request_id)) {
        return;
    }
    const auto position = Position::from_fen(fen.toStdString());
    if (!position) {
        return;
    }
    const int lines = settings_.lines;
    try {
        if (const ProfileStore* profiles = open_profiles(false)) {
            if (auto hit = profiles->cache_lookup(key, lines, min_cached_depth)) {
                emit lines_ready(key, request_id, white_side_lines(*position, hit->lines), true);
                return;
            }
        }
    } catch (const std::exception& error) {
        profiles_.reset();
        emit engine_failed(tr("The engine cache could not be read: %1").arg(error.what()));
    }

    const auto path = find_engine_binary(settings_);
    if (!path) {
        drop_engine();
        emit engine_state(tr("not found"));
        return;
    }
    if (!engine_ || !engine_->is_alive()) {
        drop_engine();
        EngineOptions options;
        options.threads = settings_.threads;
        options.hash_mb = settings_.hash_mb;
        auto started = Engine::start(*path, options);
        if (!started) {
            emit engine_state(tr("not found"));
            emit engine_failed(
                tr("The engine %1 %2")
                    .arg(QString::fromStdString(path->string()), describe(started.error())));
            return;
        }
        const std::scoped_lock lock(engine_mutex_);
        engine_.emplace(std::move(*started));
    }
    if (stale(request_id) || !engine_.has_value()) {
        return;
    }
    // Only this thread replaces engine_, and not before the search below returns.
    Engine& engine = *engine_;
    const QString name = QString::fromStdString(engine.name());
    emit engine_state(tr("searching"));
    const auto result = engine.search(
        *position, {.lines = lines, .seconds = settings_.seconds, .generation = request_id});
    emit engine_state(name);
    if (!result) {
        emit engine_failed(tr("The engine %1 %2").arg(name, describe(result.error())));
        if (!engine.is_alive()) {
            drop_engine(); // the next search starts a new one
        }
        return;
    }
    if (stale(request_id) || result->stopped) {
        return; // a stopped search answers a request already left, and is not cached
    }
    std::vector<SearchLine> found = result->lines;
    if (ProfileStore* profiles = open_profiles(true)) {
        auto stored = profiles->cache_store(key, lines, *result);
        if (stored) {
            found = stored->lines;
        } else {
            emit engine_failed(tr("The engine cache could not be written"));
        }
    }
    emit lines_ready(key, request_id, white_side_lines(*position, found), false);
}

} // namespace chesscoach::app

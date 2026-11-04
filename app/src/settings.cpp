// The app's settings (settings.hpp).
#include "settings.hpp"

#include <chesscoach/tier.hpp>

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QThread>
#include <QVariant>
#include <algorithm>

namespace chesscoach::app {

void keep_qt_settings_in(const QString& data_dir) {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data_dir);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, data_dir);
}

Settings::Settings(const QString& data_dir)
    : data_dir_(data_dir), file_(QDir(data_dir).filePath(QStringLiteral("settings.ini"))) {}

QString Settings::theme() const {
    return read(QStringLiteral("theme")) == QStringLiteral("dark") ? QStringLiteral("dark")
                                                                   : QStringLiteral("light");
}

std::optional<int> Settings::last_tier() const {
    bool ok = false;
    const int tier = read(QStringLiteral("tier")).toInt(&ok);
    if (!ok || !tier_from_index(tier)) {
        return std::nullopt;
    }
    return tier;
}

EngineSettings Settings::default_engine() {
    EngineSettings engine;
    engine.threads = std::max(1, QThread::idealThreadCount() / 2);
    return engine;
}

EngineSettings Settings::engine() const {
    EngineSettings engine = default_engine();
    engine.binary = read(QStringLiteral("engine/binary"));
    bool ok = false;
    if (const int threads = read(QStringLiteral("engine/threads")).toInt(&ok); ok && threads >= 1) {
        engine.threads = threads;
    }
    if (const int hash = read(QStringLiteral("engine/hash_mb")).toInt(&ok); ok && hash >= 1) {
        engine.hash_mb = hash;
    }
    if (const double seconds = read(QStringLiteral("engine/seconds")).toDouble(&ok);
        ok && seconds >= 0.1 && seconds <= 60) {
        engine.seconds = seconds;
    }
    if (const int lines = read(QStringLiteral("engine/lines")).toInt(&ok);
        ok && lines >= 1 && lines <= 10) {
        engine.lines = lines;
    }
    return engine;
}

SparSettings Settings::spar() const {
    SparSettings spar;
    bool ok = false;
    if (const int end_move = read(QStringLiteral("spar/end_move")).toInt(&ok);
        ok && end_move >= 1 && end_move <= 200) {
        spar.end_move = end_move;
    }
    if (const double seconds = read(QStringLiteral("spar/seconds")).toDouble(&ok);
        ok && seconds >= 0.1 && seconds <= 60) {
        spar.seconds = seconds;
    }
    return spar;
}

std::optional<quint64> Settings::spar_seed() const {
    bool ok = false;
    const quint64 seed = read(QStringLiteral("spar/seed")).toULongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return seed;
}

void Settings::set_engine(const EngineSettings& engine) {
    write({
        {QStringLiteral("engine/binary"), engine.binary},
        {QStringLiteral("engine/threads"), QString::number(engine.threads)},
        {QStringLiteral("engine/hash_mb"), QString::number(engine.hash_mb)},
        {QStringLiteral("engine/seconds"), QString::number(engine.seconds)},
        {QStringLiteral("engine/lines"), QString::number(engine.lines)},
    });
}

int Settings::repertoire_depth() const {
    bool ok = false;
    const int depth = read(QStringLiteral("repertoire/depth")).toInt(&ok);
    return ok && depth >= 2 && depth <= 40 ? depth : 20;
}

QString Settings::last_dir() const {
    const QString directory = read(QStringLiteral("files/last_dir"));
    return !directory.isEmpty() && QFileInfo(directory).isDir() ? directory : QDir::homePath();
}

void Settings::set_last_dir(const QString& directory) {
    write(QStringLiteral("files/last_dir"), directory);
}

bool Settings::sounds_enabled() const {
    return read(QStringLiteral("sounds")) != QStringLiteral("false");
}

void Settings::set_sounds_enabled(bool enabled) {
    write(QStringLiteral("sounds"), enabled ? QStringLiteral("true") : QStringLiteral("false"));
}

void Settings::set_repertoire_depth(int depth) {
    write(QStringLiteral("repertoire/depth"), QString::number(depth));
}

void Settings::set_spar(const SparSettings& spar) {
    write({
        {QStringLiteral("spar/end_move"), QString::number(spar.end_move)},
        {QStringLiteral("spar/seconds"), QString::number(spar.seconds)},
    });
}

void Settings::set_theme(const QString& theme) { write(QStringLiteral("theme"), theme); }

void Settings::set_last_tier(int tier) { write(QStringLiteral("tier"), QString::number(tier)); }

QString Settings::read(const QString& key) const {
    // A QSettings on a file that does not exist reads nothing and, with nothing set, writes
    // nothing; the check keeps even that from happening.
    if (!QFileInfo::exists(file_)) {
        return {};
    }
    const QSettings settings(file_, QSettings::IniFormat);
    return settings.value(key).toString();
}

void Settings::write(const QString& key, const QString& value) { write({{key, value}}); }

void Settings::write(const QList<std::pair<QString, QString>>& values) {
    QDir().mkpath(data_dir_);
    QSettings settings(file_, QSettings::IniFormat);
    for (const auto& [key, value] : values) {
        settings.setValue(key, value);
    }
    settings.sync();
}

} // namespace chesscoach::app

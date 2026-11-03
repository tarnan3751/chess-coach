// The app's settings (settings.hpp).
#include "settings.hpp"

#include <chesscoach/tier.hpp>

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QVariant>

namespace chesscoach::app {

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

void Settings::write(const QString& key, const QString& value) {
    QDir().mkpath(data_dir_);
    QSettings settings(file_, QSettings::IniFormat);
    settings.setValue(key, value);
    settings.sync();
}

} // namespace chesscoach::app

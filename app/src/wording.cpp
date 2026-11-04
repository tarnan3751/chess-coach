// How the app words ratings, counts, numbers, pawns, days and months (wording.hpp).
#include "wording.hpp"

#include <chesscoach/tier.hpp>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QStringList>
#include <cstddef>
#include <optional>

namespace chesscoach::app {

namespace {

QString translate(const char* text) {
    return QCoreApplication::translate("chesscoach::app::wording", text);
}

QLocale english() { return {QLocale::English}; }

// "2026-10" as October 2026's first day, or nullopt.
std::optional<QDate> month(const QString& text) {
    const QDate date = QDate::fromString(text.trimmed(), QStringLiteral("yyyy-MM"));
    return date.isValid() ? std::optional<QDate>(date) : std::nullopt;
}

} // namespace

QString rating_band(int tier) {
    // Tier's values are the indices 0 to tier_count - 1; tier t starts at bound t - 1 and ends
    // below bound t.
    const auto& bounds = tier_lower_bounds;
    if (tier <= 0) {
        return translate("under %1").arg(bounds.front());
    }
    if (tier >= tier_count - 1) {
        return translate("%1 and above").arg(bounds.back());
    }
    const auto index = static_cast<std::size_t>(tier);
    return translate("%1–%2").arg(bounds.at(index - 1)).arg(bounds.at(index) - 1);
}

QString number(qint64 n) { return english().toString(n); }

QString counted(qint64 n, const QString& one, const QString& many) {
    return (n == 1 ? one : many).arg(number(n));
}

QString pawns(qint64 centipawns) {
    return english().toString(static_cast<double>(centipawns) / 100.0, 'f', 1);
}

QString day(qint64 seconds) {
    return english().toString(QDateTime::fromSecsSinceEpoch(seconds).date(),
                              QStringLiteral("d MMMM yyyy"));
}

QString month_range(const QString& stored) {
    const QStringList parts = stored.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return {};
    }
    const auto first = month(parts.front());
    const auto last = month(parts.back());
    if (!first || !last) {
        return stored;
    }
    const QString format = QStringLiteral("MMMM yyyy");
    if (*first == *last) {
        return english().toString(*first, format);
    }
    return translate("%1 to %2")
        .arg(english().toString(*first, format), english().toString(*last, format));
}

} // namespace chesscoach::app

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
#include <vector>

namespace chesscoach::app {

namespace {

QString translate(const char* text) {
    return QCoreApplication::translate("chesscoach::app::wording", text);
}

QLocale english() { return {QLocale::English}; }

// A loss below this many centipawns is almost nothing; from the second, a forced mate's value
// (docs/database.md §7.2) or as good as one, the move loses by force.
constexpr qint64 almost_nothing_cp = 5;
constexpr qint64 by_force_cp = 1000;

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
    // Rounded in integers, so 95 is "1.0" and not the 0.9 that 0.95 as a double prints.
    const qint64 tenths = centipawns >= 0 ? (centipawns + 5) / 10 : -((5 - centipawns) / 10);
    return english().toString(static_cast<double>(tenths) / 10.0, 'f', 1);
}

QString loss_sentence(qint64 centipawns) {
    if (centipawns < almost_nothing_cp) {
        return translate("The engine says it gives away almost nothing.");
    }
    if (centipawns >= by_force_cp) {
        return translate("The engine says it loses by force.");
    }
    if (pawns(centipawns) == pawns(100)) {
        return translate("The engine says it gives away about 1 pawn.");
    }
    return translate("The engine says it gives away about %1 pawns.").arg(pawns(centipawns));
}

QString loss_cell(qint64 centipawns) {
    if (centipawns < almost_nothing_cp) {
        return translate("almost none");
    }
    if (centipawns >= by_force_cp) {
        return translate("loses by force");
    }
    return pawns(centipawns);
}

QString day(qint64 seconds) {
    return english().toString(QDateTime::fromSecsSinceEpoch(seconds).date(),
                              QStringLiteral("d MMMM yyyy"));
}

QString month_range(const QString& stored) {
    std::vector<QDate> dates;
    for (const QString& part : stored.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const auto date = month(part);
        if (!date) {
            return stored;
        }
        dates.push_back(*date);
    }
    if (dates.empty()) {
        return {};
    }
    const QString format = QStringLiteral("MMMM yyyy");
    if (dates.size() == 1) {
        return english().toString(dates.front(), format);
    }
    bool run = true;
    for (std::size_t i = 1; i < dates.size(); ++i) {
        run = run && dates.at(i) == dates.at(i - 1).addMonths(1);
    }
    if (run) {
        return translate("%1 to %2")
            .arg(english().toString(dates.front(), format),
                 english().toString(dates.back(), format));
    }
    // With gaps, each month by name, and the year after the last month of each year.
    QStringList names;
    for (std::size_t i = 0; i < dates.size(); ++i) {
        const bool year_ends =
            i + 1 == dates.size() || dates.at(i + 1).year() != dates.at(i).year();
        names << english().toString(dates.at(i), year_ends ? format : QStringLiteral("MMMM"));
    }
    const QString last = names.takeLast();
    return translate("%1 and %2").arg(names.join(QStringLiteral(", ")), last);
}

} // namespace chesscoach::app

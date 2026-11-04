// How the app words ratings, counts, numbers, pawns, days and months (wording.hpp).
#include "wording.hpp"

#include <chesscoach/score.hpp>
#include <chesscoach/tier.hpp>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QStringList>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace chesscoach::app {

namespace {

QString translate(const char* text) {
    return QCoreApplication::translate("chesscoach::app::wording", text);
}

QLocale english() { return {QLocale::English}; }

// A loss below this many centipawns is almost nothing.
constexpr qint64 almost_nothing_cp = 5;
// §7.2 values a mate in m as 100,000 - |m| centipawns; no centipawn evaluation comes near this.
constexpr qint64 mate_floor = 90000;

// How a move's loss reads, from its two evaluations.
enum class LossCase : std::uint8_t { by_force, missed_win, almost_nothing, pawns };

LossCase loss_case(const MoveEvaluations& evaluations) {
    if (evaluations.after <= -mate_floor) {
        return LossCase::by_force;
    }
    if (evaluations.before >= mate_floor && evaluations.after < mate_floor) {
        return LossCase::missed_win;
    }
    return evaluations.before - evaluations.after < almost_nothing_cp ? LossCase::almost_nothing
                                                                      : LossCase::pawns;
}

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

MoveEvaluations move_evaluations(Color mover, const PositionRow& before, const PositionRow& after) {
    const qint64 side = mover == Color::white ? 1 : -1;
    return MoveEvaluations{
        .before = side * evaluation_value(before.eval_cp, before.eval_mate),
        .after = side * evaluation_value(after.eval_cp, after.eval_mate),
    };
}

QString loss_sentence(const MoveEvaluations& evaluations) {
    const qint64 loss = evaluations.before - evaluations.after;
    switch (loss_case(evaluations)) {
    case LossCase::by_force:
        return translate("The engine says it loses by force.");
    case LossCase::missed_win:
        return translate("The engine says it misses a forced win.");
    case LossCase::almost_nothing:
        return translate("The engine says it gives away almost nothing.");
    case LossCase::pawns:
        break;
    }
    if (pawns(loss) == pawns(100)) {
        return translate("The engine says it gives away about 1 pawn.");
    }
    return translate("The engine says it gives away about %1 pawns.").arg(pawns(loss));
}

QString loss_cell(const MoveEvaluations& evaluations) {
    switch (loss_case(evaluations)) {
    case LossCase::by_force:
        return translate("loses by force");
    case LossCase::missed_win:
        return translate("misses a win");
    case LossCase::almost_nothing:
        return translate("almost none");
    case LossCase::pawns:
        break;
    }
    return pawns(evaluations.before - evaluations.after);
}

int moves_from_plies(int plies) { return (plies + 1) / 2; }

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

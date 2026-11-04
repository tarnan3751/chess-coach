// What the database worker answers for one position at one tier, as the Explore screen shows it
// (docs/reference/app.md, **Explore**): the stored rows, the deepest name along the line, and
// one entry per move with its SAN, badges, results and top-tier share. Plain values, copied across
// the queued connection from the worker's thread to the GUI thread, so both are registered
// metatypes (register_explore_types).
#pragma once

#include <chesscoach/badges.hpp>
#include <chesscoach/opening_store.hpp>

#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <optional>

namespace chesscoach::app {

struct ExploreMove {
    QString san;
    QString uci;
    Badges badges;
    qint64 total = 0;
    qint64 white = 0;
    qint64 draws = 0;
    qint64 black = 0;
    // The move's share of the plays from the position at tier 6 (docs/database.md §7.2), as a
    // percentage; nullopt when the position has no tier-6 node.
    std::optional<double> top_share;
};

struct ExploreData {
    quint64 key = 0;
    int tier = 0;
    QString fen;
    std::optional<PositionRow> position;
    std::optional<NodeRow> node;
    // Ordered by total, most played first, then by UCI.
    QVector<ExploreMove> moves;
    // The ECO code and name of the deepest named position along the line (§10), or empty.
    QString name;
    // The sum of the totals of the position's moves at tier 6, when it has a tier-6 node.
    std::optional<qint64> top_tier_sum;
};

// Registers ExploreData, ExploreMove and the line of keys for queued connections; idempotent.
void register_explore_types();

} // namespace chesscoach::app

Q_DECLARE_METATYPE(chesscoach::app::ExploreMove)
Q_DECLARE_METATYPE(chesscoach::app::ExploreData)

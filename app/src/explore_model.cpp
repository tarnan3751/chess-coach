// The Explore screen's table model (explore_model.hpp).
#include "explore_model.hpp"

#include "tables.hpp"
#include "wording.hpp"

#include <QLatin1Char>

#include <QCoreApplication>
#include <QList>
#include <QStringList>

namespace chesscoach::app {

namespace {

QString translate(const char* text) {
    return QCoreApplication::translate("chesscoach::app::ExploreModel", text);
}

QString percent(double value) { return QString::number(value, 'f', 1); }

// The shares of white wins, draws and black wins in the move's plays.
QList<double> shares_of(const ExploreMove& move) {
    if (move.total <= 0) {
        return {0, 0, 0};
    }
    const auto total = static_cast<double>(move.total);
    return {
        static_cast<double>(move.white) / total,
        static_cast<double>(move.draws) / total,
        static_cast<double>(move.black) / total,
    };
}

// White's score in the move's plays, wins plus half the draws, as a percentage, whichever side
// is to move.
double white_score(const ExploreMove& move) {
    if (move.total <= 0) {
        return 0;
    }
    return 100.0 * (static_cast<double>(move.white) + (static_cast<double>(move.draws) / 2)) /
           static_cast<double>(move.total);
}

// The columns of numbers, which read right-aligned.
bool numeric(int column) {
    return column == ExploreModel::games_column || column == ExploreModel::score_column ||
           column == ExploreModel::top_tier_column;
}

} // namespace

QString badge_names(Badges badges) {
    QStringList names;
    for (const Badge badge : {Badge::book, Badge::popular, Badge::dubious}) {
        if (badges.has(badge)) {
            names << badge_word(badge);
        }
    }
    return names.join(QStringLiteral(", "));
}

ExploreModel::ExploreModel(QObject* parent) : QAbstractTableModel(parent) {}

void ExploreModel::set_moves(const QVector<ExploreMove>& moves) {
    // Rows out and rows in rather than a reset, which would drop the view's column settings.
    if (!moves_.isEmpty()) {
        beginRemoveRows(QModelIndex(), 0, static_cast<int>(moves_.size()) - 1);
        moves_.clear();
        endRemoveRows();
    }
    if (!moves.isEmpty()) {
        beginInsertRows(QModelIndex(), 0, static_cast<int>(moves.size()) - 1);
        moves_ = moves;
        endInsertRows();
    }
}

int ExploreModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(moves_.size());
}

int ExploreModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : column_count;
}

QVariant ExploreModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= moves_.size()) {
        return {};
    }
    const ExploreMove& move = moves_.at(index.row());
    if (role == uci_role) {
        return move.uci;
    }
    if (role == shares_role) {
        return QVariant::fromValue(shares_of(move));
    }
    if (role == badges_role) {
        return static_cast<uint>(move.badges.raw());
    }
    if (role == Qt::TextAlignmentRole && numeric(index.column())) {
        return QVariant::fromValue(Qt::Alignment(Qt::AlignRight | Qt::AlignVCenter));
    }
    if (role == Qt::ToolTipRole && index.column() == badges_column) {
        return badge_names(move.badges);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case move_column:
        return move.san;
    case badges_column:
        return badge_names(move.badges);
    case games_column:
        return number(move.total);
    case result_column: {
        const QList<double> shares = shares_of(move);
        return QStringLiteral("%1 / %2 / %3")
            .arg(percent(100 * shares.at(0)), percent(100 * shares.at(1)),
                 percent(100 * shares.at(2)));
    }
    case score_column:
        return percent(white_score(move)) + QLatin1Char('%');
    case top_tier_column:
        return move.top_share ? QString::number(*move.top_share, 'f', 0) + QLatin1Char('%')
                              : QStringLiteral("–");
    default:
        return {};
    }
}

QVariant ExploreModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    if (role == Qt::TextAlignmentRole) {
        return QVariant::fromValue(
            Qt::Alignment((numeric(section) ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter));
    }
    if (role == header_note_role) {
        return section == result_column ? translate("white · draw · black") : QVariant();
    }
    if (role == Qt::ToolTipRole) {
        switch (section) {
        case games_column:
            return translate("How many games at this rating reached this move");
        case result_column:
            return translate("White wins · draws · Black wins, as a share of the games");
        case score_column:
            return translate("Wins plus half the draws, for White");
        case top_tier_column:
            return translate("How often players rated 2200 and above choose this move");
        default:
            return {};
        }
    }
    if (role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
    case move_column:
        return translate("Move");
    case badges_column:
        return translate("Badges");
    case games_column:
        return translate("Games");
    case result_column:
        return translate("Results");
    case score_column:
        return translate("Score for White");
    case top_tier_column:
        return translate("Top players");
    default:
        return {};
    }
}

} // namespace chesscoach::app

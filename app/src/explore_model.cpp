// The Explore screen's table model (explore_model.hpp).
#include "explore_model.hpp"

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

} // namespace

QString badge_letters(Badges badges) {
    QString letters;
    if (badges.has(Badge::book)) {
        letters += QLatin1Char('B');
    }
    if (badges.has(Badge::popular)) {
        letters += QLatin1Char('P');
    }
    if (badges.has(Badge::dubious)) {
        letters += QLatin1Char('D');
    }
    return letters;
}

QString badge_names(Badges badges) {
    QStringList names;
    if (badges.has(Badge::book)) {
        names << translate("Book");
    }
    if (badges.has(Badge::popular)) {
        names << translate("Popular");
    }
    if (badges.has(Badge::dubious)) {
        names << translate("Dubious");
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
    if (role == Qt::TextAlignmentRole && index.column() != move_column &&
        index.column() != badges_column && index.column() != in_book_column) {
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
        return badge_letters(move.badges);
    case games_column:
        return move.total;
    case result_column: {
        const QList<double> shares = shares_of(move);
        return QStringLiteral("%1 / %2 / %3")
            .arg(percent(100 * shares.at(0)), percent(100 * shares.at(1)),
                 percent(100 * shares.at(2)));
    }
    case score_column:
        return percent(move.score_percent);
    case top_tier_column:
        return move.top_share ? percent(*move.top_share) : QStringLiteral("—");
    case in_book_column:
        return move.child_in_book ? translate("yes") : translate("no");
    default:
        return {};
    }
}

QVariant ExploreModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
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
        return translate("Result");
    case score_column:
        return translate("Score %");
    case top_tier_column:
        return translate("Top tier %");
    case in_book_column:
        return translate("In book");
    default:
        return {};
    }
}

} // namespace chesscoach::app

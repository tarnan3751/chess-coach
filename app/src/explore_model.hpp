// The Explore screen's table model: one row per move of the position at the tier, in the order
// the worker gives them (most played first), with the columns Move, Badges, Games, Results, Score
// for White and Top players, their tooltips, and the cells as text, the badges' bits and the
// results' shares for the delegate that paints them (docs/reference/app.md, **Explore**). GUI
// thread only.
#pragma once

#include "explore_data.hpp"
#include "result_delegate.hpp"

#include <QAbstractTableModel>
#include <QModelIndex>
#include <QVariant>
#include <QVector>

namespace chesscoach::app {

class ExploreModel : public QAbstractTableModel {
    Q_OBJECT

  public:
    // The columns.
    static constexpr int move_column = 0;
    static constexpr int badges_column = 1;
    static constexpr int games_column = 2;
    static constexpr int result_column = 3;
    static constexpr int score_column = 4;
    static constexpr int top_tier_column = 5;
    static constexpr int column_count = 6;
    // The row's UCI move, under every column.
    static constexpr int uci_role = Qt::UserRole;
    // The Results column's shares of white wins, draws and black wins, a QList<double> of three
    // fractions summing to 1, as the results bar reads them.
    static constexpr int shares_role = chesscoach::app::shares_role;
    // The Badges column's badges, the bits as an unsigned int, as the pills read them.
    static constexpr int badges_role = chesscoach::app::badges_role;

    explicit ExploreModel(QObject* parent = nullptr);

    void set_moves(const QVector<ExploreMove>& moves);
    [[nodiscard]] const QVector<ExploreMove>& moves() const { return moves_; }

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

  private:
    QVector<ExploreMove> moves_;
};

// The badges by name, "Book, Popular", empty for none.
[[nodiscard]] QString badge_names(Badges badges);

} // namespace chesscoach::app

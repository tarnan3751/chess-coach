// The result bar: a delegate painting a cell as white wins, draws and black wins side by side,
// each with its percentage when it is wide enough to hold it, from the cell's shares_role data,
// a QList<double> of three fractions summing to 1 (docs/reference/app.md, **Explore**). Used by
// Explore's move table and Spar's better choices. GUI thread only.
#pragma once

#include <QStyledItemDelegate>
#include <QtGlobal>

namespace chesscoach::app {

// The role under which a cell holds its three shares.
inline constexpr int shares_role = Qt::UserRole + 1;

class ResultDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};

} // namespace chesscoach::app

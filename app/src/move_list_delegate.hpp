// The move list's rows (docs/reference/app.md, **Shell**): "1. e4 c5", the move number in the muted
// colour (the palette's placeholder text), right-aligned in a column of its own, then each move in
// its column, the last move of the list, the board's, marked in the selection colour (the
// palette's highlight). The item's text stays the plain row, which the tests read.
// GUI thread only.
#pragma once

#include <QModelIndex>
#include <QSize>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>

class QPainter;

namespace chesscoach::app {

class MoveListDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

  private:
    static constexpr int move_padding = 10; // the panel's inner margin
    static constexpr int move_gap = 8;
    static constexpr int mark_padding = 4;
};

} // namespace chesscoach::app

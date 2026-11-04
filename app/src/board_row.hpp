// The evaluation bar and the board side by side at the top of the board column
// (docs/reference/app.md, **Shell**): the board square, drawn edge to edge in its widget
// (BoardView::resizeEvent), the bar 14 px wide, exactly the board's height and 8 px from it.
// Columns sets the side the row asks for; the row is as tall as it is wide, less the bar and the
// gap, so that the move list below takes the rest of the column. GUI thread only.
#pragma once

#include <QSize>
#include <QWidget>

class QResizeEvent;

namespace chesscoach::app {

class BoardRow : public QWidget {
  public:
    static constexpr int bar_width = 14;
    static constexpr int gap = 8;
    static constexpr int min_side = 300; // the board's least side

    // Takes bar and board as its children.
    BoardRow(QWidget* bar, QWidget* board, QWidget* parent = nullptr);

    // The board's side the row asks its column for.
    void set_side(int side);

    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    QWidget* bar_;
    QWidget* board_;
    int side_ = min_side;
};

} // namespace chesscoach::app

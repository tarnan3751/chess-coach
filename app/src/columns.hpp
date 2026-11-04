// The main window's content (docs/reference/app.md, **Shell**): the board column (the board row
// over the move list) and the screen column (the tabs over the screens), margin pixels from the
// window's edges and from each other, spacing pixels between a column's parts. The board takes the
// side the window's height allows, as long as the screen column keeps its minimum, at least
// screen_min_width, and never less than BoardRow::min_side; the screen column takes the rest.
// GUI thread only.
#pragma once

#include <QWidget>

class QResizeEvent;
class QVBoxLayout;

namespace chesscoach::app {

class BoardRow;

class Columns : public QWidget {
  public:
    static constexpr int margin = 14;
    static constexpr int spacing = 10;
    static constexpr int screen_min_width = 700;

    // Lays the four out as its children; the move list's minimum height is kept below the board.
    Columns(BoardRow* row, QWidget* move_list, QWidget* tabs, QWidget* screens,
            QWidget* parent = nullptr);

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    BoardRow* row_;
    QWidget* move_list_;
    QVBoxLayout* right_; // the screen column, owned by the columns' layout
};

} // namespace chesscoach::app

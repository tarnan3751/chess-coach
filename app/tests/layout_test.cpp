// Unit tests of the main window's content (app; unit): the column split, the board's floor, and
// the evaluation bar against the board as drawn (columns.hpp, board_row.hpp), with the real bar
// and board and stand-ins for the move list, the tabs and the screens, on an offscreen
// application (offscreen_application.hpp).
#include "board_row.hpp"
#include "board_view.hpp"
#include "columns.hpp"
#include "eval_bar.hpp"
#include "offscreen_application.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QListWidget>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QStackedWidget>
#include <QTransform>
#include <QWidget>

using chesscoach::app::BoardRow;
using chesscoach::app::BoardView;
using chesscoach::app::Columns;
using chesscoach::app::EvalBar;
using chesscoach::app::test::offscreen_application;

namespace {

// The content as the window holds it, inside a host that leaves its size to the test.
struct Content {
    QWidget host;
    EvalBar* bar = new EvalBar();
    BoardView* board = new BoardView();
    QListWidget* moves = new QListWidget();
    QWidget* tabs = new QWidget();
    QStackedWidget* screens = new QStackedWidget();
    BoardRow* row = new BoardRow(bar, board);
    Columns* columns;

    Content() : columns(new Columns(row, moves, tabs, screens, &host)) {
        moves->setMinimumHeight(96);
        tabs->setFixedHeight(30);
        host.show();
    }

    // Lays the content out at the size, as a window's resize does.
    void resize(QSize size) {
        host.resize(size);
        columns->setGeometry(QRect(QPoint(0, 0), size));
        for (int pass = 0; pass < 3; ++pass) {
            QCoreApplication::sendPostedEvents();
            QCoreApplication::processEvents();
        }
    }

    // The board as drawn, in the row's coordinates.
    [[nodiscard]] QRect drawn_board() const {
        const QRectF in_view = board->viewportTransform().mapRect(board->sceneRect());
        return {board->viewport()->mapTo(row, in_view.topLeft().toPoint()),
                in_view.size().toSize()};
    }
};

} // namespace

TEST_CASE("At 1200 px wide the board is 436 px and the screen column 700", "[layout]") {
    static_cast<void>(offscreen_application());
    Content content;
    // The window's content at 1200 × 780 is 1200 wide; the height, less the toolbar and the
    // status bar, leaves the board to the width.
    content.resize(QSize(1200, 640));
    CHECK(content.drawn_board().width() == 436);
    CHECK(content.screens->width() == 700);
}

TEST_CASE("At the minimum size the board is 300 px and the screen column 700", "[layout]") {
    static_cast<void>(offscreen_application());
    Content content;
    const QSize minimum = content.columns->minimumSizeHint();
    CHECK(minimum.width() ==
          (3 * Columns::margin) + BoardRow::bar_width + BoardRow::gap + BoardRow::min_side + 700);
    content.resize(minimum);
    CHECK(content.drawn_board().width() == 300);
    CHECK(content.screens->width() == 700);
}

TEST_CASE("The board never falls below 300 px", "[layout]") {
    static_cast<void>(offscreen_application());
    Content content;
    // As short as the content can be, at any width, the board is still 300 px.
    const int shortest = content.columns->minimumSizeHint().height();
    content.resize(QSize(1600, shortest));
    CHECK(content.drawn_board().width() == BoardRow::min_side);
    // Shorter than that, the row still asks for 300 px.
    content.resize(QSize(1600, shortest - 50));
    CHECK(content.row->sizeHint().height() == BoardRow::min_side);
}

TEST_CASE("The evaluation bar is the drawn board's height, 8 px from it", "[layout]") {
    static_cast<void>(offscreen_application());
    Content content;
    for (const QSize size : {
             QSize(1200, 640),
             QSize(1600, 1000),
             QSize(1064, 500),
             QSize(1333, 777),
             QSize(1480, 911),
         }) {
        content.resize(size);
        const QRect board = content.drawn_board();
        const QRect bar = content.bar->geometry();
        CHECK(board.width() == board.height());
        CHECK(bar.top() == board.top());
        CHECK(bar.height() == board.height());
        CHECK(bar.width() == BoardRow::bar_width);
        CHECK(board.left() - (bar.right() + 1) == BoardRow::gap);
    }
}

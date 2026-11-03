// UI tests of the board under the offscreen platform (app; ui): the start position's pieces on
// their squares, a move by two clicks and by drag and drop, illegal input playing nothing, the
// flip, a promotion through the popup, the check highlight and set_position from a FEN, with a
// screenshot of each state (ui_harness.hpp).
#include <chesscoach/position.hpp>

#include "board_scene.hpp"
#include "board_view.hpp"
#include "piece_set.hpp"
#include "squares.hpp"
#include "ui_harness.hpp"

#include <QAction>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsSvgItem>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>
#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

using chesscoach::Position;
using chesscoach::app::BoardScene;
using chesscoach::app::BoardView;
using chesscoach::app::placement_of;
using chesscoach::app::test::child;
using chesscoach::app::test::click;
using chesscoach::app::test::Moves;
using chesscoach::app::test::save_screenshot;
using chesscoach::app::test::square;
using chesscoach::app::test::square_point;
using chesscoach::app::test::taken;

namespace {

constexpr int board_pixels = 480;

// The position after the moves (UCI), which the tests spell correctly.
Position after(std::initializer_list<std::string_view> moves) {
    Position position = Position::start();
    for (const std::string_view move : moves) {
        static_cast<void>(position.play_uci(move));
    }
    return position;
}

// Whether every piece item stands on the square the position puts its piece on.
bool items_match(const BoardView& view) {
    const auto placement = placement_of(view.position());
    const auto occupied = std::ranges::count_if(placement, [](char piece) { return piece != 0; });
    const auto items = view.board_scene().piece_items();
    return std::cmp_equal(items.size(), occupied) &&
           std::ranges::all_of(items, [&](const QGraphicsSvgItem* item) {
               const auto on = view.board_scene().square_at(item->sceneBoundingRect().center());
               return on &&
                      placement.at(static_cast<std::size_t>(*on)) == BoardScene::piece_of(item);
           });
}

class BoardTest : public QObject {
    Q_OBJECT

    BoardView* view_ = nullptr;

  private slots:
    void init() {
        view_ = new BoardView();
        view_->resize(board_pixels, board_pixels);
        view_->show();
        QVERIFY(QTest::qWaitForWindowExposed(view_));
    }

    void cleanup() {
        delete view_;
        view_ = nullptr;
    }

    void start_position_shows_32_pieces_on_their_squares() {
        QCOMPARE(view_->board_scene().piece_items().size(), std::size_t{32});
        QVERIFY(items_match(*view_));
        QCOMPARE(BoardScene::piece_of(view_->board_scene().piece_item(square("e1"))), 'K');
        QCOMPARE(BoardScene::piece_of(view_->board_scene().piece_item(square("d8"))), 'q');
        QVERIFY(view_->board_scene().pieces().valid());
        save_screenshot(*view_, QStringLiteral("board"), QStringLiteral("start"));
    }

    void two_clicks_play_a_move() {
        QSignalSpy played(view_, &BoardView::move_played);
        click(*view_, "e2");
        QCOMPARE(view_->board_scene().selected_square(), std::optional(square("e2")));
        QCOMPARE(view_->board_scene().hint_squares(), (std::vector{square("e3"), square("e4")}));
        save_screenshot(*view_, QStringLiteral("board"), QStringLiteral("e2-picked"));
        click(*view_, "e4");
        QCOMPARE(taken(played), (Moves{{QStringLiteral("e2e4"), QStringLiteral("e4")}}));
        QVERIFY(view_->animating());
        QTRY_VERIFY(!view_->animating());
        QVERIFY(items_match(*view_));
        QCOMPARE(view_->board_scene().last_move(),
                 std::optional(std::pair{square("e2"), square("e4")}));
        QCOMPARE(view_->board_scene().selected_square(), std::nullopt);
        QVERIFY(view_->board_scene().hint_squares().empty());
        save_screenshot(*view_, QStringLiteral("board"), QStringLiteral("after-e4"));
    }

    void a_drag_and_drop_plays_a_move() {
        view_->set_position(after({"e2e4", "e7e5"}));
        QSignalSpy played(view_, &BoardView::move_played);
        QWidget* viewport = view_->viewport();
        const QPoint from = square_point(*view_, "g1");
        const QPoint to = square_point(*view_, "f3");
        QTest::mousePress(viewport, Qt::LeftButton, Qt::KeyboardModifiers(), from);
        QTest::mouseMove(viewport, (from + to) / 2);
        save_screenshot(*view_, QStringLiteral("board"), QStringLiteral("dragging"));
        QTest::mouseMove(viewport, to);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::KeyboardModifiers(), to);
        QCOMPARE(taken(played), (Moves{{QStringLiteral("g1f3"), QStringLiteral("Nf3")}}));
        QVERIFY(!view_->animating()); // a dropped piece is already where it goes
        QVERIFY(items_match(*view_));
        QCOMPARE(BoardScene::piece_of(view_->board_scene().piece_item(square("f3"))), 'N');
    }

    void illegal_input_plays_nothing() {
        QSignalSpy played(view_, &BoardView::move_played);
        const std::string fen = view_->position().fen();
        click(*view_, "e2");
        click(*view_, "e5"); // not a target of e2
        QCOMPARE(view_->board_scene().selected_square(), std::nullopt);
        click(*view_, "e7"); // black's pawn, white to move
        QCOMPARE(view_->board_scene().selected_square(), std::nullopt);
        click(*view_, "e4"); // an empty square
        QCOMPARE(view_->board_scene().selected_square(), std::nullopt);

        // A drop on a square that is not a target puts the piece back.
        QWidget* viewport = view_->viewport();
        QTest::mousePress(viewport, Qt::LeftButton, Qt::KeyboardModifiers(),
                          square_point(*view_, "d2"));
        QTest::mouseMove(viewport, square_point(*view_, "d5"));
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::KeyboardModifiers(),
                            square_point(*view_, "d5"));
        QVERIFY(taken(played).empty());
        QCOMPARE(view_->position().fen(), fen);
        QVERIFY(items_match(*view_));
        QCOMPARE(view_->board_scene().square_at(
                     view_->board_scene().piece_item(square("d2"))->sceneBoundingRect().center()),
                 std::optional(square("d2")));
    }

    void flip_mirrors_the_board_and_its_coordinates() {
        const BoardScene& scene = view_->board_scene();
        const QPointF bottom_left(BoardScene::square_size / 2, 7.5 * BoardScene::square_size);
        QCOMPARE(scene.square_at(bottom_left), std::optional(square("a1")));
        QVERIFY(scene.file_label(0)->x() < scene.file_label(7)->x());
        QVERIFY(scene.rank_label(0)->y() > scene.rank_label(7)->y());
        view_->flip();
        QVERIFY(view_->flipped());
        QCOMPARE(scene.square_at(bottom_left), std::optional(square("h8")));
        QVERIFY(scene.file_label(0)->x() > scene.file_label(7)->x());
        QVERIFY(scene.rank_label(0)->y() < scene.rank_label(7)->y());
        QVERIFY(items_match(*view_));
        // White's king is now near the top of the board.
        QVERIFY(scene.piece_item(square("e1"))->y() < BoardScene::square_size);
        // Hit testing follows: e2 is still e2, wherever it is drawn.
        QSignalSpy played(view_, &BoardView::move_played);
        click(*view_, "e2");
        click(*view_, "e4");
        QCOMPARE(taken(played), (Moves{{QStringLiteral("e2e4"), QStringLiteral("e4")}}));
        QTRY_VERIFY(!view_->animating());
        save_screenshot(*view_, QStringLiteral("board"), QStringLiteral("flipped"));
        view_->flip();
        QVERIFY(!view_->flipped());
    }

    void a_promotion_asks_for_the_piece_first() {
        auto position = Position::from_fen("7k/4P3/8/8/8/8/8/4K3 w - -");
        QVERIFY(position.has_value());
        view_->set_position(*position);
        QSignalSpy played(view_, &BoardView::move_played);
        click(*view_, "e7");
        click(*view_, "e8");
        QVERIFY(taken(played).empty());
        const auto& chooser = child<QWidget>(*view_, QStringLiteral("promotion_chooser"));
        QVERIFY(chooser.isVisible());
        save_screenshot(*view_, QStringLiteral("board"), QStringLiteral("promotion"));
        child<QAction>(*view_, QStringLiteral("promote_q")).trigger();
        QCOMPARE(taken(played), (Moves{{QStringLiteral("e7e8q"), QStringLiteral("e8=Q+")}}));
        QVERIFY(!chooser.isVisible());
        QTRY_VERIFY(!view_->animating());
        QCOMPARE(BoardScene::piece_of(view_->board_scene().piece_item(square("e8"))), 'Q');

        // A press on the board while the popup is open cancels the promotion.
        view_->set_position(*position);
        click(*view_, "e7");
        click(*view_, "e8");
        QVERIFY(chooser.isVisible());
        click(*view_, "a1");
        QVERIFY(!chooser.isVisible());
        QVERIFY(taken(played).empty());
        QCOMPARE(view_->position().fen(), position->fen());
        QVERIFY(items_match(*view_));
    }

    void a_checking_move_highlights_the_king() {
        QSignalSpy played(view_, &BoardView::move_played);
        click(*view_, "e2");
        click(*view_, "e4");
        click(*view_, "f7");
        click(*view_, "f5");
        QCOMPARE(view_->board_scene().check_square(), std::nullopt);
        click(*view_, "d1");
        click(*view_, "h5");
        const Moves moves = taken(played);
        QCOMPARE(moves.size(), std::size_t{3});
        QCOMPARE(moves.back(), std::pair(QStringLiteral("d1h5"), QStringLiteral("Qh5+")));
        QCOMPARE(view_->board_scene().check_square(), std::optional(square("e8")));
        QTRY_VERIFY(!view_->animating());
        save_screenshot(*view_, QStringLiteral("board"), QStringLiteral("check"));
    }

    void set_position_shows_a_fen() {
        auto position =
            Position::from_fen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
        QVERIFY(position.has_value());
        click(*view_, "e2"); // a picked piece is dropped by set_position
        view_->set_position(*position);
        QCOMPARE(view_->position().fen(), position->fen());
        QVERIFY(!view_->animating());
        QVERIFY(items_match(*view_));
        QCOMPARE(view_->board_scene().piece_items().size(), std::size_t{32});
        QCOMPARE(BoardScene::piece_of(view_->board_scene().piece_item(square("h4"))), 'q');
        QCOMPARE(view_->board_scene().check_square(), std::optional(square("e1")));
        QCOMPARE(view_->board_scene().last_move(), std::nullopt);
        QCOMPARE(view_->board_scene().selected_square(), std::nullopt);
        save_screenshot(*view_, QStringLiteral("board"), QStringLiteral("fen"));
    }
};

} // namespace

QTEST_MAIN(BoardTest)
#include "ui_board_test.moc"

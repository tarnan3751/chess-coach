// UI tests of the Explore screen on a fixture database (app; ui): opening it and seeing the start
// position's moves with their badges and bars, switching tiers, playing moves on the board and
// from the table, jumping back through the move list, an off-book position, the evaluation bar
// and a database that does not open, with screenshots (ui_harness.hpp, fixture_db.hpp).
#include <chesscoach/position.hpp>
#include <chesscoach/version.hpp>

#include "board_view.hpp"
#include "eval_bar.hpp"
#include "explore_model.hpp"
#include "explore_screen.hpp"
#include "fixture_db.hpp"
#include "main_window.hpp"
#include "ui_harness.hpp"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QObject>
#include <QStringList>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using chesscoach::Position;
using chesscoach::app::BoardView;
using chesscoach::app::EvalBar;
using chesscoach::app::ExploreModel;
using chesscoach::app::ExploreScreen;
using chesscoach::app::MainWindow;
using chesscoach::app::test::child;
using chesscoach::app::test::click;
using chesscoach::app::test::FixtureDb;
using chesscoach::app::test::save_screenshot;

namespace {

// A window on the fixture database with a data directory of its own, which does not exist
// until the window writes to it.
struct Session {
    QTemporaryDir root;
    QString data_dir = root.filePath(QStringLiteral("data"));
    std::unique_ptr<MainWindow> window;

    explicit Session(const QString& db) : window(std::make_unique<MainWindow>(data_dir, db)) {
        window->show();
    }
};

const ExploreModel& model(const MainWindow& window) {
    return child<ExploreScreen>(window, QStringLiteral("explore_screen")).model();
}

QString cell(const MainWindow& window, int row, int column) {
    return model(window).index(row, column).data().toString();
}

// The moves of the table, in its order, by SAN.
QStringList table_moves(const MainWindow& window) {
    QStringList moves;
    for (int row = 0; row < model(window).rowCount(); ++row) {
        moves << cell(window, row, ExploreModel::move_column);
    }
    return moves;
}

bool opened(const MainWindow& window) {
    return child<QLabel>(window, QStringLiteral("data_version_status")).isVisible();
}

void select_tier(MainWindow& window, int tier) {
    child<QComboBox>(window, QStringLiteral("tier_selector")).setCurrentIndex(tier);
}

QString message(const MainWindow& window) {
    return child<QLabel>(window, QStringLiteral("explore_message")).text();
}

bool message_shown(const MainWindow& window) {
    return child<QLabel>(window, QStringLiteral("explore_message")).isVisible();
}

class ExploreTest : public QObject {
    Q_OBJECT

    std::unique_ptr<FixtureDb> db_;

  private slots:
    void initTestCase() { db_ = std::make_unique<FixtureDb>(); }
    void cleanupTestCase() { db_.reset(); }

    void opening_the_database_shows_the_start_position() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        QCOMPARE(child<QLabel>(window, QStringLiteral("data_version_status")).text(),
                 QStringLiteral("Data version 0"));
        QCOMPARE(child<QLabel>(window, QStringLiteral("database_status")).text(),
                 QStringLiteral("openings.db"));
        // Tier 2 to start, where the fixture has no games: the start position is off book.
        QCOMPARE(window.tier(), MainWindow::default_tier);
        QTRY_COMPARE(message(window), QStringLiteral("Off book at 1400-1599: 0 games"));
        QVERIFY(message_shown(window));

        select_tier(window, 3);
        QTRY_COMPARE(model(window).rowCount(), 4);
        QCOMPARE(table_moves(window), (QStringList{QStringLiteral("e4"), QStringLiteral("d4"),
                                                   QStringLiteral("Nf3"), QStringLiteral("f3")}));
        QCOMPARE(cell(window, 0, ExploreModel::badges_column), QStringLiteral("BP"));
        QCOMPARE(model(window).index(0, ExploreModel::badges_column).data(Qt::ToolTipRole),
                 QStringLiteral("Book, Popular"));
        QCOMPARE(cell(window, 0, ExploreModel::games_column), QStringLiteral("600"));
        QCOMPARE(cell(window, 0, ExploreModel::result_column),
                 QStringLiteral("43.3 / 10.0 / 46.7"));
        const auto shares = model(window)
                                .index(0, ExploreModel::result_column)
                                .data(ExploreModel::shares_role)
                                .value<QList<double>>();
        QCOMPARE(shares.size(), 3);
        QCOMPARE(cell(window, 0, ExploreModel::score_column), QStringLiteral("48.3"));
        QCOMPARE(cell(window, 0, ExploreModel::top_tier_column), QStringLiteral("45.0"));
        QCOMPARE(cell(window, 2, ExploreModel::top_tier_column), QStringLiteral("0.0"));
        QCOMPARE(cell(window, 0, ExploreModel::in_book_column), QStringLiteral("yes"));
        QCOMPARE(cell(window, 3, ExploreModel::badges_column), QString());
        QCOMPARE(cell(window, 3, ExploreModel::in_book_column), QStringLiteral("no"));
        QCOMPARE(child<QLabel>(window, QStringLiteral("explore_name")).text(),
                 QStringLiteral("Unnamed"));
        QCOMPARE(child<QLabel>(window, QStringLiteral("explore_evaluation")).text(),
                 QStringLiteral("Evaluation: +0.2"));
        QCOMPARE(child<QLabel>(window, QStringLiteral("explore_games")).text(),
                 QStringLiteral("1000 games at 1600-1799"));
        save_screenshot(window, QStringLiteral("explore"), QStringLiteral("start-tier3"));
    }

    void switching_tiers_changes_the_table() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        select_tier(window, 3);
        QTRY_COMPARE(model(window).rowCount(), 4);
        select_tier(window, 6);
        QTRY_COMPARE(model(window).rowCount(), 2);
        QCOMPARE(table_moves(window), (QStringList{QStringLiteral("d4"), QStringLiteral("e4")}));
        // At the top tier a move's top-tier share is its share.
        QCOMPARE(cell(window, 0, ExploreModel::top_tier_column), QStringLiteral("55.0"));
        select_tier(window, 0);
        QTRY_COMPARE(table_moves(window),
                     (QStringList{QStringLiteral("e4"), QStringLiteral("f3")}));
        QCOMPARE(child<QLabel>(window, QStringLiteral("explore_games")).text(),
                 QStringLiteral("20 games at <1200"));
    }

    void moves_on_the_board_show_the_replies_and_the_name() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        select_tier(window, 3);
        QTRY_COMPARE(model(window).rowCount(), 4);
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        QTRY_COMPARE(table_moves(window), (QStringList{QStringLiteral("e5"), QStringLiteral("c5"),
                                                       QStringLiteral("e6")}));
        QCOMPARE(child<QLabel>(window, QStringLiteral("explore_name")).text(),
                 QStringLiteral("B00 King's Pawn Game"));
        // Black to move: the score is black's.
        QCOMPARE(cell(window, 0, ExploreModel::score_column), QStringLiteral("48.3"));
        QCOMPARE(cell(window, 2, ExploreModel::badges_column), QStringLiteral("PD"));
        QCOMPARE(model(window).index(2, ExploreModel::badges_column).data(Qt::ToolTipRole),
                 QStringLiteral("Popular, Dubious"));

        click(board, "c7");
        click(board, "c5");
        QTRY_COMPARE(table_moves(window),
                     (QStringList{QStringLiteral("Nf3"), QStringLiteral("Nc3")}));
        QCOMPARE(child<QLabel>(window, QStringLiteral("explore_name")).text(),
                 QStringLiteral("B20 Sicilian Defense"));
        QTRY_VERIFY(!board.animating());
        save_screenshot(window, QStringLiteral("explore"), QStringLiteral("sicilian-light"));
    }

    void the_table_plays_a_move_on_double_click_and_enter() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        select_tier(window, 3);
        QTRY_COMPARE(model(window).rowCount(), 4);
        auto& table = child<QTableView>(window, QStringLiteral("explore_table"));
        const QRect d4 = table.visualRect(model(window).index(1, ExploreModel::move_column));
        QTest::mouseClick(table.viewport(), Qt::LeftButton, Qt::KeyboardModifiers(), d4.center());
        QTest::mouseDClick(table.viewport(), Qt::LeftButton, Qt::KeyboardModifiers(), d4.center());
        QCOMPARE(window.line(), (std::vector<std::string>{"d2d4"}));
        QTRY_COMPARE(table_moves(window),
                     (QStringList{QStringLiteral("d5"), QStringLiteral("Nf6")}));

        table.setFocus();
        table.setCurrentIndex(model(window).index(0, ExploreModel::move_column));
        QTest::keyClick(&table, Qt::Key_Return);
        QCOMPARE(window.line(), (std::vector<std::string>{"d2d4", "d7d5"}));
        QTRY_COMPARE(table_moves(window),
                     (QStringList{QStringLiteral("c4"), QStringLiteral("Nf3")}));
    }

    void the_move_list_and_back_jump_to_an_earlier_position() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        select_tier(window, 3);
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        click(board, "e7");
        click(board, "e5");
        click(board, "g1");
        click(board, "f3");
        QCOMPARE(window.line().size(), std::size_t{3});
        const auto& moves = child<QListWidget>(window, QStringLiteral("move_list"));
        QCOMPARE(moves.count(), 2);

        // The first row is 1. e4 e5: the board goes back to the position after it.
        QTest::mouseClick(moves.viewport(), Qt::LeftButton, Qt::KeyboardModifiers(),
                          moves.visualItemRect(moves.item(0)).center());
        QCOMPARE(window.line(), (std::vector<std::string>{"e2e4", "e7e5"}));
        Position expected = Position::start();
        static_cast<void>(expected.play_uci("e2e4"));
        static_cast<void>(expected.play_uci("e7e5"));
        QCOMPARE(board.position().fen(), expected.fen());
        QCOMPARE(moves.count(), 1);
        // The node at the depth the fixture stops: in book, named, no moves.
        QTRY_COMPARE(child<QLabel>(window, QStringLiteral("explore_name")).text(),
                     QStringLiteral("C20 King's Pawn Game"));
        QCOMPARE(model(window).rowCount(), 0);

        child<QAction>(window, QStringLiteral("back_action")).trigger();
        QCOMPARE(window.line(), (std::vector<std::string>{"e2e4"}));
        QTRY_COMPARE(table_moves(window), (QStringList{QStringLiteral("e5"), QStringLiteral("c5"),
                                                       QStringLiteral("e6")}));
        child<QAction>(window, QStringLiteral("new_game_action")).trigger();
        QVERIFY(window.line().empty());
        QTRY_COMPARE(model(window).rowCount(), 4);
    }

    void an_off_book_position_shows_the_label() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        select_tier(window, 3);
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        click(board, "e7");
        click(board, "e6");
        QTRY_COMPARE(message(window), QStringLiteral("Off book at 1600-1799: 0 games"));
        QVERIFY(message_shown(window));
        QVERIFY(!child<QTableView>(window, QStringLiteral("explore_table")).isVisible());
        QCOMPARE(child<QLabel>(window, QStringLiteral("explore_name")).text(),
                 QStringLiteral("B00 King's Pawn Game"));
        QTRY_VERIFY(!board.animating());
        save_screenshot(window, QStringLiteral("explore"), QStringLiteral("off-book"));
    }

    void the_eval_bar_follows_the_stored_evaluation() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        select_tier(window, 0);
        const auto& bar = child<EvalBar>(window, QStringLiteral("eval_bar"));
        // The start position: +0.19 for white.
        QTRY_VERIFY(bar.isVisible());
        const double share = bar.white_share().value_or(-1);
        QVERIFY(std::abs(share - EvalBar::white_share_of_cp(19)) < 1e-9);
        QVERIFY(share > 0.5);
        // After 1. f3 e5 2. g4, black mates in one: the bar is black's.
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "f2");
        click(board, "f3");
        click(board, "e7");
        click(board, "e5");
        click(board, "g2");
        click(board, "g4");
        QTRY_COMPARE(child<QLabel>(window, QStringLiteral("explore_evaluation")).text(),
                     QStringLiteral("Evaluation: -M1"));
        QCOMPARE(bar.white_share(), std::optional(0.0));
        QVERIFY(bar.isVisible());
        // 1. Nf3 d5 has no evaluation: the bar hides.
        child<QAction>(window, QStringLiteral("new_game_action")).trigger();
        click(board, "g1");
        click(board, "f3");
        click(board, "d7");
        click(board, "d5");
        QTRY_COMPARE(child<QLabel>(window, QStringLiteral("explore_evaluation")).text(),
                     QStringLiteral("Evaluation: no evaluation"));
        QVERIFY(!bar.isVisible());
        QCOMPARE(bar.white_share(), std::nullopt);
    }

    void a_database_that_does_not_open_leaves_the_empty_state() const {
        const QTemporaryDir root;
        const QString missing = root.filePath(QStringLiteral("missing.db"));
        QVERIFY(missing != QString::fromStdString(db_->path()));
        const Session session(missing);
        MainWindow& window = *session.window;
        QTRY_VERIFY(window.findChild<QMessageBox*>(QStringLiteral("database_error")) != nullptr);
        auto& box = child<QMessageBox>(window, QStringLiteral("database_error"));
        QVERIFY(box.text().contains(missing));
        QVERIFY(box.text().contains(QStringLiteral("no such file")));
        QCOMPARE(child<QLabel>(window, QStringLiteral("database_status")).text(),
                 QStringLiteral("No database"));
        QVERIFY(!child<QLabel>(window, QStringLiteral("data_version_status")).isVisible());
        QVERIFY(message(window).startsWith(QStringLiteral("No database loaded")));
        box.accept();
        // The board still plays.
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        QCOMPARE(window.line(), (std::vector<std::string>{"e2e4"}));
    }
};

} // namespace

QTEST_MAIN(ExploreTest)
#include "ui_explore_test.moc"

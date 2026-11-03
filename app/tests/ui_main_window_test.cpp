// UI tests of the main window's frame under the offscreen platform (app; ui): the title, the
// tier selector, the move list following the board, New game, Flip board, the screen switcher
// and the status bar, with screenshots (ui_harness.hpp).
#include <chesscoach/position.hpp>
#include <chesscoach/tier.hpp>
#include <chesscoach/version.hpp>

#include "board_view.hpp"
#include "drill_screen.hpp"
#include "explore_screen.hpp"
#include "main_window.hpp"
#include "repertoire_screen.hpp"
#include "spar_screen.hpp"
#include "ui_harness.hpp"

#include <QAction>
#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QObject>
#include <QStackedWidget>
#include <QString>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <string_view>

using chesscoach::app::BoardView;
using chesscoach::app::DrillScreen;
using chesscoach::app::ExploreScreen;
using chesscoach::app::MainWindow;
using chesscoach::app::RepertoireScreen;
using chesscoach::app::SparScreen;
using chesscoach::app::test::child;
using chesscoach::app::test::click;
using chesscoach::app::test::save_screenshot;

namespace {

class MainWindowTest : public QObject {
    Q_OBJECT

    // A data directory the windows never write to: these tests change neither the theme nor
    // the tier.
    QTemporaryDir root_;
    const QString data_dir_ = root_.filePath(QStringLiteral("data"));

  private slots:
    void initTestCase() const {
        QVERIFY(root_.isValid());
        // These tests keep the engine out: the binary named here does not exist.
        qputenv("CHESS_COACH_STOCKFISH", "/nonexistent/stockfish-for-the-window-tests");
    }

    void title_names_the_version() const {
        const MainWindow window(data_dir_, QString());
        QCOMPARE(window.windowTitle(),
                 QStringLiteral("chess-coach ") +
                     QString::fromStdString(chesscoach::to_string(chesscoach::current_version())));
    }

    void tier_selector_lists_the_seven_tiers() const {
        const MainWindow window(data_dir_, QString());
        const auto& tiers = child<QComboBox>(window, QStringLiteral("tier_selector"));
        QCOMPARE(tiers.count(), chesscoach::tier_count);
        for (int index = 0; index < chesscoach::tier_count; ++index) {
            const std::string_view label =
                chesscoach::tier_label(static_cast<chesscoach::Tier>(index));
            QCOMPARE(tiers.itemText(index),
                     QString::fromUtf8(label.data(), static_cast<qsizetype>(label.size())));
            QCOMPARE(tiers.itemData(index).toInt(), index);
        }
        QCOMPARE(tiers.currentIndex(), MainWindow::default_tier);
    }

    void the_move_list_follows_the_board_and_new_game_clears_it() const {
        MainWindow window(data_dir_, QString());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        save_screenshot(window, QStringLiteral("main-window"), QStringLiteral("start"));
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        const auto& moves = child<QListWidget>(window, QStringLiteral("move_list"));

        click(board, "e2");
        click(board, "e4");
        QCOMPARE(moves.count(), 1);
        QCOMPARE(moves.item(0)->text(), QStringLiteral("1. e4"));
        click(board, "e7");
        click(board, "e5");
        QCOMPARE(moves.item(0)->text(), QStringLiteral("1. e4 e5"));
        click(board, "g1");
        click(board, "f3");
        QCOMPARE(moves.count(), 2);
        QCOMPARE(moves.item(1)->text(), QStringLiteral("2. Nf3"));
        QTRY_VERIFY(!board.animating());
        save_screenshot(window, QStringLiteral("main-window"), QStringLiteral("after-moves"));

        child<QAction>(window, QStringLiteral("new_game_action")).trigger();
        QCOMPARE(moves.count(), 0);
        QCOMPARE(board.position().fen(), chesscoach::Position::start().fen());
        click(board, "d2");
        click(board, "d4");
        QCOMPARE(moves.item(0)->text(), QStringLiteral("1. d4"));
    }

    void flip_board_flips_the_board() const {
        MainWindow window(data_dir_, QString());
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        QVERIFY(!board.flipped());
        child<QAction>(window, QStringLiteral("flip_action")).trigger();
        QVERIFY(board.flipped());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        save_screenshot(window, QStringLiteral("main-window"), QStringLiteral("flipped"));
    }

    void the_switcher_shows_four_screens() const {
        const MainWindow window(data_dir_, QString());
        auto& tabs = child<QTabBar>(window, QStringLiteral("screen_tabs"));
        const auto& screens = child<QStackedWidget>(window, QStringLiteral("screens"));
        QCOMPARE(tabs.count(), 4);
        QCOMPARE(screens.count(), 4);
        const QStringList names{
            QStringLiteral("Explore"),
            QStringLiteral("Spar"),
            QStringLiteral("Repertoire"),
            QStringLiteral("Drill"),
        };
        QVERIFY(qobject_cast<const ExploreScreen*>(screens.widget(0)) != nullptr);
        QVERIFY(qobject_cast<const SparScreen*>(screens.widget(1)) != nullptr);
        QVERIFY(qobject_cast<const RepertoireScreen*>(screens.widget(2)) != nullptr);
        QVERIFY(qobject_cast<const DrillScreen*>(screens.widget(3)) != nullptr);
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(tabs.tabText(i), names.at(i));
        }
        tabs.setCurrentIndex(2);
        QCOMPARE(screens.currentIndex(), 2);
    }

    void the_status_bar_shows_no_database_and_the_engine() const {
        const MainWindow window(data_dir_, QString());
        QCOMPARE(child<QLabel>(window, QStringLiteral("database_status")).text(),
                 QStringLiteral("No database"));
        QVERIFY(child<QLabel>(window, QStringLiteral("data_version_status")).isHidden());
        QCOMPARE(child<QLabel>(window, QStringLiteral("engine_status")).text(),
                 QStringLiteral("Engine: not started"));
        QVERIFY(child<QLabel>(window, QStringLiteral("explore_message"))
                    .text()
                    .startsWith(QStringLiteral("No database loaded")));
        QVERIFY(!QFileInfo::exists(data_dir_));
    }
};

} // namespace

QTEST_MAIN(MainWindowTest)
#include "ui_main_window_test.moc"

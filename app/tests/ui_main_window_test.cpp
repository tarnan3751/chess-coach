// UI tests of the main window's frame under the offscreen platform (app; ui): the title, the
// tier selector, the move list following the board, New game, Flip board, the screen switcher,
// Help > How to use chess-coach, the toolbar's tooltips, the screens' descriptions and the status
// bar, with screenshots (ui_harness.hpp); and the sounds, one per move by its SAN and none for a
// position set, which the Settings dialog and the toolbar's toggle turn off and on, each following
// the other.
#include <chesscoach/position.hpp>
#include <chesscoach/tier.hpp>
#include <chesscoach/version.hpp>

#include "application.hpp"
#include "board_view.hpp"
#include "drill_screen.hpp"
#include "explore_screen.hpp"
#include "icons.hpp"
#include "main_window.hpp"
#include "repertoire_screen.hpp"
#include "settings.hpp"
#include "sounds.hpp"
#include "spar_screen.hpp"
#include "study_palette.hpp"
#include "ui_harness.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QObject>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QToolButton>
#include <string_view>
#include <utility>
#include <vector>

using chesscoach::app::BoardView;
using chesscoach::app::DrillScreen;
using chesscoach::app::ExploreScreen;
using chesscoach::app::MainWindow;
using chesscoach::app::NullSoundBackend;
using chesscoach::app::RepertoireScreen;
using chesscoach::app::Settings;
using chesscoach::app::SoundEvent;
using chesscoach::app::SparScreen;
using chesscoach::app::StudyPalette;
using chesscoach::app::themed_icon;
using chesscoach::app::test::child;
using chesscoach::app::test::click;
using chesscoach::app::test::played;
using chesscoach::app::test::save_screenshot;

namespace {

// An icon as the toolbar draws it, to compare two.
QImage drawn(const QIcon& icon) { return icon.pixmap(QSize(16, 16)).toImage(); }

class MainWindowTest : public QObject {
    Q_OBJECT

    // A data directory the windows never write to: these tests change neither the theme nor
    // the tier. The sound tests write theirs beside it, each its own.
    QTemporaryDir root_;
    const QString data_dir_ = root_.filePath(QStringLiteral("data"));

  private slots:
    void initTestCase() const {
        chesscoach::app::configure_application();
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
        const QStringList bands{
            QStringLiteral("Under 1200"),     QStringLiteral("1200–1399"),
            QStringLiteral("1400–1599"),      QStringLiteral("1600–1799"),
            QStringLiteral("1800–1999"),      QStringLiteral("2000–2199"),
            QStringLiteral("2200 and above"),
        };
        for (int index = 0; index < chesscoach::tier_count; ++index) {
            QCOMPARE(tiers.itemText(index), bands.at(index));
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
            QStringLiteral("Practice"),
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

    void help_says_how_to_use_each_screen() const {
        MainWindow window(data_dir_, QString());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        child<QAction>(window, QStringLiteral("help_action")).trigger();
        auto& dialog = child<QDialog>(window, QStringLiteral("help_dialog"));
        QTRY_VERIFY(dialog.isVisible());
        const auto& browser = child<QTextBrowser>(dialog, QStringLiteral("help_text"));
        QVERIFY(!browser.openLinks());
        QVERIFY(!browser.openExternalLinks());
        const QString text = browser.toPlainText();
        const QStringList headings{
            QStringLiteral("Explore"),
            QStringLiteral("Practice"),
            QStringLiteral("Repertoire"),
            QStringLiteral("Drill"),
            QStringLiteral("The database and the engine"),
        };
        for (const QString& heading : headings) {
            QVERIFY2(text.contains(heading), qPrintable(heading));
        }
        // Each word explained where it first appears, and what the screens' numbers mean.
        for (const QString& said : {
                 QStringLiteral("White wins, draws and Black wins"),
                 QStringLiteral("plus when White is better and minus when Black is"),
                 QStringLiteral("The known lines are the moves played often enough"),
                 QStringLiteral("a Dubious move, one that players at your rating often choose"),
                 QStringLiteral("Book (what strong players, rated 2200 and above, choose)"),
                 QStringLiteral("comes back tomorrow"),
                 QStringLiteral("nothing you do leaves it"),
                 QStringLiteral("File > Back up your work saves everything"),
             }) {
            QVERIFY2(text.contains(said), qPrintable(said));
        }
        // The headings in the spec's serif, which the window registered.
        QVERIFY(QFontDatabase::hasFamily(QStringLiteral("Source Serif 4")));
        QVERIFY(browser.toHtml().contains(QStringLiteral("Source Serif 4")));
        // Nothing links out of the app, and the house style has no semicolons.
        QVERIFY(!browser.toHtml().contains(QStringLiteral("href")));
        QVERIFY(!text.contains(QLatin1Char(';')));
        save_screenshot(dialog, QStringLiteral("help"), QStringLiteral("dialog"));
    }

    void the_toolbar_explains_its_buttons() const {
        const MainWindow window(data_dir_, QString());
        QCOMPARE(child<QAction>(window, QStringLiteral("new_game_action")).toolTip(),
                 QStringLiteral("Go back to the starting position."));
        QCOMPARE(child<QAction>(window, QStringLiteral("back_action")).toolTip(),
                 QStringLiteral("Take back the last move."));
        QCOMPARE(child<QAction>(window, QStringLiteral("flip_action")).toolTip(),
                 QStringLiteral("Turn the board around."));
        QCOMPARE(child<QWidget>(window, QStringLiteral("tier_selector")).toolTip(),
                 QStringLiteral("Choose the rating of the players whose games you see."));
    }

    void every_screen_says_what_it_is_for() const {
        const MainWindow window(data_dir_, QString());
        const QStringList descriptions{
            QStringLiteral("Play an opening against the coach. It follows what players at your "
                           "rating play, stops you when you play a dubious move or leave the known "
                           "lines, and shows better choices."),
            QStringLiteral("Choose the move you will play in each position. Coverage shows how "
                           "much of what your opponents play you have an answer to."),
            QStringLiteral(
                "Flashcards on your repertoire. Each card shows a position. Play your move."),
        };
        const QStringList names{
            QStringLiteral("spar_description"),
            QStringLiteral("repertoire_description"),
            QStringLiteral("drill_description"),
        };
        for (qsizetype i = 0; i < names.size(); ++i) {
            const auto& label = child<QLabel>(window, names.at(i));
            QCOMPARE(label.text(), descriptions.at(i));
            QVERIFY(label.property("muted").toBool()); // the stylesheets' muted colour
        }
    }

    void each_move_sounds_once_by_its_san() const {
        const MainWindow window(data_dir_, QString());
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        QVERIFY(window.sounds().enabled()); // on when the setting is absent
        const NullSoundBackend& sounds = played(window.sounds());
        QVERIFY(!sounds.last().has_value());
        // 1. e4 d5 2. exd5 Qxd5 3. Nc3 Qe5+: each move one sound, by its SAN.
        const std::vector<std::pair<std::string_view, SoundEvent>> moves{
            {"e2e4", SoundEvent::move},    {"d7d5", SoundEvent::move},
            {"e4d5", SoundEvent::capture}, {"d8d5", SoundEvent::capture},
            {"b1c3", SoundEvent::move},    {"d5e5", SoundEvent::check},
        };
        for (const auto& [uci, sound] : moves) {
            click(board, uci.substr(0, 2));
            click(board, uci.substr(2, 2));
            QVERIFY(sounds.last() == sound);
        }
        QCOMPARE(sounds.count(SoundEvent::move), 3);
        QCOMPARE(sounds.count(SoundEvent::capture), 2);
        QCOMPARE(sounds.count(SoundEvent::check), 1);
        // A position set rather than played, as New game sets the start, sounds nothing.
        child<QAction>(window, QStringLiteral("new_game_action")).trigger();
        QCOMPARE(sounds.count(SoundEvent::move), 3);
        click(board, "d2");
        click(board, "d4");
        QCOMPARE(sounds.count(SoundEvent::move), 4);
        QCOMPARE(sounds.count(SoundEvent::capture) + sounds.count(SoundEvent::check) +
                     sounds.count(SoundEvent::wrong) + sounds.count(SoundEvent::interruption),
                 3);
    }

    void the_settings_dialog_turns_the_sounds_off_and_on() const {
        const QString data_dir = root_.filePath(QStringLiteral("sounds-dialog"));
        const MainWindow window(data_dir, QString());
        child<QAction>(window, QStringLiteral("settings_action")).trigger();
        auto& dialog = child<QDialog>(window, QStringLiteral("settings_dialog"));
        auto& box = child<QCheckBox>(dialog, QStringLiteral("sounds_setting"));
        QCOMPARE(box.text(), QStringLiteral("Play sounds"));
        QVERIFY(box.isChecked());
        box.setChecked(false);
        dialog.accept();
        QVERIFY(!window.sounds().enabled());
        QVERIFY(!Settings(data_dir).sounds_enabled());
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        QVERIFY(!played(window.sounds()).last().has_value());

        // A new window on the same data directory starts silent; turned on, a move sounds.
        const MainWindow again(data_dir, QString());
        QVERIFY(!again.sounds().enabled());
        child<QAction>(again, QStringLiteral("settings_action")).trigger();
        auto& reopened = child<QDialog>(again, QStringLiteral("settings_dialog"));
        auto& reopened_box = child<QCheckBox>(reopened, QStringLiteral("sounds_setting"));
        QVERIFY(!reopened_box.isChecked());
        reopened_box.setChecked(true);
        reopened.accept();
        QVERIFY(again.sounds().enabled());
        QVERIFY(Settings(data_dir).sounds_enabled());
        const auto& again_board = child<BoardView>(again, QStringLiteral("board"));
        click(again_board, "e2");
        click(again_board, "e4");
        QVERIFY(played(again.sounds()).last() == SoundEvent::move);
    }

    void the_sound_toggle_and_the_settings_dialog_follow_each_other() const {
        const QString data_dir = root_.filePath(QStringLiteral("sounds-toggle"));
        MainWindow window(data_dir, QString());
        auto& toggle = child<QAction>(window, QStringLiteral("sounds_action"));
        const auto& button = child<QToolButton>(window, QStringLiteral("sounds_button"));
        QVERIFY(toggle.isCheckable());
        QVERIFY(toggle.isChecked());
        QCOMPARE(button.toolButtonStyle(), Qt::ToolButtonIconOnly);
        QCOMPARE(button.toolTip(), QStringLiteral("Sounds on"));
        QCOMPARE(drawn(toggle.icon()),
                 drawn(themed_icon(QStringLiteral("sound-on"), StudyPalette::light())));
        QVERIFY(drawn(toggle.icon()) !=
                drawn(themed_icon(QStringLiteral("sound-off"), StudyPalette::light())));

        // The toggle turns them off at once, saved, with the other icon.
        toggle.trigger();
        QVERIFY(!toggle.isChecked());
        QVERIFY(!window.sounds().enabled());
        QVERIFY(!Settings(data_dir).sounds_enabled());
        QCOMPARE(button.toolTip(), QStringLiteral("Sounds off"));
        QCOMPARE(drawn(toggle.icon()),
                 drawn(themed_icon(QStringLiteral("sound-off"), StudyPalette::light())));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        save_screenshot(window, QStringLiteral("main-window"), QStringLiteral("sounds-off"));

        // The Settings dialog shows it, and turns them on again, which the toggle follows.
        child<QAction>(window, QStringLiteral("settings_action")).trigger();
        auto& dialog = child<QDialog>(window, QStringLiteral("settings_dialog"));
        auto& box = child<QCheckBox>(dialog, QStringLiteral("sounds_setting"));
        QVERIFY(!box.isChecked());
        box.setChecked(true);
        dialog.accept();
        QVERIFY(toggle.isChecked());
        QVERIFY(window.sounds().enabled());
        QVERIFY(Settings(data_dir).sounds_enabled());
        QCOMPARE(button.toolTip(), QStringLiteral("Sounds on"));

        // The dark theme draws the toggle in its colours.
        child<QAction>(window, QStringLiteral("theme_dark")).trigger();
        QCOMPARE(drawn(toggle.icon()),
                 drawn(themed_icon(QStringLiteral("sound-on"), StudyPalette::dark())));
        QVERIFY(drawn(toggle.icon()) !=
                drawn(themed_icon(QStringLiteral("sound-on"), StudyPalette::light())));
    }

    void the_status_bar_shows_no_database_and_the_engine() const {
        const MainWindow window(data_dir_, QString());
        QCOMPARE(child<QLabel>(window, QStringLiteral("database_status")).text(),
                 QStringLiteral("No opening database"));
        QCOMPARE(child<QLabel>(window, QStringLiteral("engine_status")).text(),
                 QStringLiteral("Engine not started"));
        QVERIFY(child<QLabel>(window, QStringLiteral("explore_message"))
                    .text()
                    .startsWith(QStringLiteral("No opening database")));
        QVERIFY(!QFileInfo::exists(data_dir_));
    }
};

} // namespace

QTEST_MAIN(MainWindowTest)
#include "ui_main_window_test.moc"

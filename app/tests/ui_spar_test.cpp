// UI tests of the Spar screen, labelled Practice (app; ui) on the fixture database's tier-4 tree,
// with core's fake engine and its legal script as the coach off book
// (core/tests/support/fake-stockfish.sh): a session as white meeting an interruption on a Dubious
// move and another on an off-book move, the hand-off to the engine, the end at the move limit with
// the summary and the saved session listed, its replay with the interruptions at their plies, each
// live interruption sounding once and the replay's not at all; a session as black, where the coach
// moves first; the coach's engine started with the session, before any move; End the game saving
// a stopped session, and closing the window saving one too; the tier selector disabled only on
// Spar while a session runs; and the Settings dialog's spar entries.
// Screenshots of the interruption, the summary and the replay (ui_harness.hpp, fixture_db.hpp).
#include <chesscoach/position.hpp>
#include <chesscoach/profile_store.hpp>

#include "application.hpp"
#include "board_view.hpp"
#include "eval_bar.hpp"
#include "fixture_db.hpp"
#include "main_window.hpp"
#include "result_delegate.hpp"
#include "settings.hpp"
#include "sounds.hpp"
#include "spar_screen.hpp"
#include "study_palette.hpp"
#include "tables.hpp"
#include "ui_harness.hpp"
#include "wording.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QObject>
#include <QPushButton>
#include <QRadioButton>
#include <QRect>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStringList>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <memory>

using chesscoach::app::BoardView;
using chesscoach::app::EngineSettings;
using chesscoach::app::MainWindow;
using chesscoach::app::Settings;
using chesscoach::app::SoundEvent;
using chesscoach::app::SparScreen;
using chesscoach::app::SparSettings;
using chesscoach::app::SparSummary;
using chesscoach::app::test::child;
using chesscoach::app::test::click;
using chesscoach::app::test::FixtureDb;
using chesscoach::app::test::played;
using chesscoach::app::test::save_screenshot;

namespace {

constexpr int spar_tier = 4;

// The reason the coach gives for 2. h4.
QString off_book_h4() {
    return QStringLiteral(
        "h4 leaves the known lines. Too few games at your rating follow it, so the "
        "engine replies from here.");
}

// A window on the fixture database at tier 4 with the Spar screen current, its settings naming
// the fake engine, a fixed seed and a session four moves long.
struct Session {
    QTemporaryDir root;
    QString data_dir = root.filePath(QStringLiteral("data"));
    std::unique_ptr<MainWindow> window;

    explicit Session(const FixtureDb& db) {
        Settings(data_dir).set_engine(EngineSettings{
            .binary = QStringLiteral(CHESSCOACH_FAKE_STOCKFISH),
            .threads = 1,
            .hash_mb = 16,
            .seconds = 0.1,
            .lines = 1,
        });
        {
            QSettings ini(QDir(data_dir).filePath(QStringLiteral("settings.ini")),
                          QSettings::IniFormat);
            ini.setValue(QStringLiteral("spar/seed"), 7);
            ini.setValue(QStringLiteral("spar/end_move"), 4);
            ini.setValue(QStringLiteral("spar/seconds"), 0.1);
            ini.setValue(QStringLiteral("tier"), spar_tier);
        }
        window = std::make_unique<MainWindow>(data_dir, QString::fromStdString(db.path()));
        window->show();
        child<QTabBar>(*window, QStringLiteral("screen_tabs")).setCurrentIndex(1);
    }
};

QString status(const MainWindow& window) {
    return child<QLabel>(window, QStringLiteral("spar_status")).text();
}

bool interruption_shown(const MainWindow& window) {
    return child<QFrame>(window, QStringLiteral("spar_interruption")).isVisible();
}

QString cell(const MainWindow& window, const QString& table, int row, int column) {
    const auto& widget = child<QTableWidget>(window, table);
    const QTableWidgetItem* item = widget.item(row, column);
    return item != nullptr ? item->text() : QString();
}

int rows(const MainWindow& window, const QString& table) {
    return child<QTableWidget>(window, table).rowCount();
}

int move_rows(const MainWindow& window) {
    return child<QListWidget>(window, QStringLiteral("move_list")).count();
}

QString move_row(const MainWindow& window, int row) {
    return child<QListWidget>(window, QStringLiteral("move_list")).item(row)->text();
}

void press(const MainWindow& window, const QString& name) {
    child<QAbstractButton>(window, name).click();
}

// Whether a pixel of a cell of the table, as it paints, has the colour.
bool painted(const MainWindow& window, const QString& table, int row, int column,
             const QColor& colour) {
    const auto& widget = child<QTableWidget>(window, table);
    const QImage image = widget.viewport()->grab().toImage();
    const QRect cell = widget.visualRect(widget.model()->index(row, column));
    const qreal ratio = image.devicePixelRatio();
    for (int y = qRound(cell.top() * ratio); y < qRound((cell.bottom() + 1) * ratio); ++y) {
        for (int x = qRound(cell.left() * ratio); x < qRound((cell.right() + 1) * ratio); ++x) {
            if (image.pixelColor(x, y).rgb() == colour.rgb()) {
                return true;
            }
        }
    }
    return false;
}

// The session's steps, each a test helper: a failed check returns from the helper, which its
// caller reads from QTest::currentTestFailed().

void meet_a_dubious_move(MainWindow& window, const BoardView& board) {
    // 1. a3, Dubious: interrupted with the better choices; the move stands and black replies.
    click(board, "a2");
    click(board, "a3");
    QTRY_VERIFY(interruption_shown(window));
    QCOMPARE(played(window.sounds()).count(SoundEvent::interruption), 1);
    QCOMPARE(
        child<QLabel>(window, QStringLiteral("spar_reason")).text(),
        QStringLiteral("Players at your rating often choose a3, but it loses ground (Dubious)."));
    QCOMPARE(child<QLabel>(window, QStringLiteral("spar_played")).text(),
             QStringLiteral("The engine says it gives away about 1.1 pawns."));
    QCOMPARE(rows(window, QStringLiteral("spar_better")), 2);
    QCOMPARE(cell(window, QStringLiteral("spar_better"), 0, 0), QStringLiteral("e4"));
    QCOMPARE(cell(window, QStringLiteral("spar_better"), 0, 1), QStringLiteral("Book, Popular"));
    QCOMPARE(cell(window, QStringLiteral("spar_better"), 0, 2), QStringLiteral("60"));
    QCOMPARE(cell(window, QStringLiteral("spar_better"), 1, 0), QStringLiteral("d4"));
    QCOMPARE(cell(window, QStringLiteral("spar_better"), 1, 1), QStringLiteral("Book"));
    // The badges and the results explained where the headers name them.
    const auto& choices = child<QTableWidget>(window, QStringLiteral("spar_better"));
    QCOMPARE(choices.horizontalHeaderItem(1)->toolTip(), chesscoach::app::badges_tooltip());
    QVERIFY(choices.horizontalHeaderItem(1)->toolTip().contains(QStringLiteral("Dubious is")));
    QCOMPARE(choices.horizontalHeaderItem(3)->toolTip(),
             QStringLiteral("White wins · draws · Black wins, as a share of the games"));
    QTRY_COMPARE(move_row(window, 0), QStringLiteral("1. a3 e5"));
    QVERIFY(!board.isEnabled()); // until Continue
    QTRY_VERIFY(!board.animating());
    save_screenshot(window, QStringLiteral("spar"), QStringLiteral("interruption"));
    // The results bar's frame follows the theme.
    using chesscoach::app::StudyPalette;
    const QString better = QStringLiteral("spar_better");
    QVERIFY(painted(window, better, 0, 3, StudyPalette::light().border));
    child<QAction>(window, QStringLiteral("theme_dark")).trigger();
    QVERIFY(painted(window, better, 0, 3, StudyPalette::dark().border));
    QVERIFY(!painted(window, better, 0, 3, StudyPalette::light().border));
    child<QAction>(window, QStringLiteral("theme_light")).trigger();
    // The pills and the Study header, as Explore's table paints them.
    QVERIFY(dynamic_cast<const chesscoach::app::TableHeader*>(choices.horizontalHeader()) !=
            nullptr);
    QCOMPARE(choices.model()->headerData(3, Qt::Horizontal, chesscoach::app::header_note_role),
             QVariant(QStringLiteral("white · draw · black")));
    QVERIFY(painted(window, better, 0, 1, StudyPalette::light().book_bg));
    QVERIFY(painted(window, better, 0, 1, StudyPalette::light().popular_bg));
    QVERIFY(painted(window, better, 1, 1, StudyPalette::light().book_bg));
    QVERIFY(!painted(window, better, 1, 1, StudyPalette::light().popular_bg));
    // Its rows as Explore's: no grid, the rule under each row, the cells painted by the same
    // delegate.
    QVERIFY(!choices.showGrid());
    QVERIFY(painted(window, better, 0, 2, StudyPalette::light().row_line));
    for (int column = 0; column < choices.columnCount(); ++column) {
        QVERIFY(dynamic_cast<const chesscoach::app::StudyCellDelegate*>(
                    choices.itemDelegateForColumn(column)) != nullptr);
    }
    press(window, QStringLiteral("spar_continue"));
    QVERIFY(!interruption_shown(window));
    QVERIFY(board.isEnabled());
}

void leave_the_book(MainWindow& window, const BoardView& board) {
    // 2. h4 leaves the book: interrupted again, then the engine replies.
    click(board, "h2");
    click(board, "h4");
    QTRY_VERIFY(interruption_shown(window));
    QCOMPARE(played(window.sounds()).count(SoundEvent::interruption), 2);
    QCOMPARE(child<QLabel>(window, QStringLiteral("spar_reason")).text(), off_book_h4());
    // Only the loss follows the reason: no badge beside "too few games".
    const QString played = child<QLabel>(window, QStringLiteral("spar_played")).text();
    QVERIFY2(played.startsWith(QStringLiteral("The ")) && !played.contains(QLatin1Char('(')),
             qPrintable(played));
    QCOMPARE(rows(window, QStringLiteral("spar_better")), 1);
    QCOMPARE(cell(window, QStringLiteral("spar_better"), 0, 0), QStringLiteral("Nf3"));
    QTRY_COMPARE(move_rows(window), 2);
    QTRY_VERIFY(move_row(window, 1).count(QLatin1Char(' ')) == 2); // "2. h4 <reply>"
    QCOMPARE(status(window),
             QStringLiteral("Move 3. You have left the known lines, so the engine replies."));
    press(window, QStringLiteral("spar_continue"));
}

void reach_the_move_limit(MainWindow& window, const BoardView& board) {
    // 3. Nf3 and the engine's reply reach move 4, the session's end.
    click(board, "g1");
    click(board, "f3");
    QTRY_VERIFY(child<QFrame>(window, QStringLiteral("spar_summary")).isVisible());
    QCOMPARE(status(window), QStringLiteral("The practice game is over."));
    const QString summary = child<QLabel>(window, QStringLiteral("spar_summary_text")).text();
    QVERIFY2(summary.startsWith(QStringLiteral(
                 "The practice game reached its move limit. The coach stopped you twice. You left "
                 "the known lines, and the engine played the rest.\n1. a3 e5 2. h4")),
             qPrintable(summary));
    QVERIFY(!board.isEnabled());
    QVERIFY(child<QComboBox>(window, QStringLiteral("tier_selector")).isEnabled());
    QTRY_COMPARE(rows(window, QStringLiteral("spar_sessions")), 1);
    QCOMPARE(cell(window, QStringLiteral("spar_sessions"), 0, 1), QStringLiteral("1800–1999"));
    QCOMPARE(cell(window, QStringLiteral("spar_sessions"), 0, 2), QStringLiteral("White"));
    QCOMPARE(cell(window, QStringLiteral("spar_sessions"), 0, 3), QStringLiteral("3"));
    QCOMPARE(cell(window, QStringLiteral("spar_sessions"), 0, 4), QStringLiteral("2"));
    QCOMPARE(cell(window, QStringLiteral("spar_sessions"), 0, 5), QStringLiteral("Move limit"));
    // The day in full, then the time: "4 November 2026, 14:05".
    const QString date = cell(window, QStringLiteral("spar_sessions"), 0, 0);
    QVERIFY2(QRegularExpression(QStringLiteral("^\\d{1,2} [A-Z][a-z]+ \\d{4}, \\d{2}:\\d{2}$"))
                 .match(date)
                 .hasMatch(),
             qPrintable(date));
    QTRY_VERIFY(!board.animating());
    save_screenshot(window, QStringLiteral("spar"), QStringLiteral("summary"));
}

void replay_the_session(MainWindow& window, const BoardView& board) {
    // The replay steps through it, the interruptions at their plies.
    const int moves_before = played(window.sounds()).count(SoundEvent::move);
    press(window, QStringLiteral("spar_replay"));
    QTRY_COMPARE(child<QLabel>(window, QStringLiteral("spar_replay_position")).text(),
                 QStringLiteral("Start of the game"));
    QCOMPARE(board.position().fen(), chesscoach::Position::start().fen());
    press(window, QStringLiteral("spar_next"));
    QVERIFY(interruption_shown(window));
    QCOMPARE(
        child<QLabel>(window, QStringLiteral("spar_reason")).text(),
        QStringLiteral("Players at your rating often choose a3, but it loses ground (Dubious)."));
    QVERIFY(!child<QPushButton>(window, QStringLiteral("spar_continue")).isVisible());
    press(window, QStringLiteral("spar_next"));
    QVERIFY(!interruption_shown(window));
    press(window, QStringLiteral("spar_next"));
    QVERIFY(interruption_shown(window));
    QCOMPARE(child<QLabel>(window, QStringLiteral("spar_reason")).text(), off_book_h4());
    QCOMPARE(move_row(window, 1), QStringLiteral("2. h4"));
    save_screenshot(window, QStringLiteral("spar"), QStringLiteral("replay"));
    press(window, QStringLiteral("spar_previous"));
    QVERIFY(!interruption_shown(window));
    QCOMPARE(child<QLabel>(window, QStringLiteral("spar_replay_position")).text(),
             QStringLiteral("Position 2 of 6"));
    QVERIFY(!board.isEnabled());
    // A replay sets its positions and shows its interruptions without a sound.
    QCOMPARE(played(window.sounds()).count(SoundEvent::interruption), 2);
    QCOMPARE(played(window.sounds()).count(SoundEvent::move), moves_before);
}

class SparTest : public QObject {
    Q_OBJECT

    std::unique_ptr<FixtureDb> db_;

  private slots:
    void initTestCase() {
        chesscoach::app::configure_application();
        db_ = std::make_unique<FixtureDb>();
        qputenv("CHESSCOACH_FAKE_UCI", CHESSCOACH_FAKE_UCI);
        qputenv("CHESSCOACH_FAKE_UCI_SCRIPT", "legal");
        qunsetenv("CHESSCOACH_FAKE_UCI_LOG");
    }

    void cleanupTestCase() { db_.reset(); }

    void a_session_meets_interruptions_ends_and_replays() {
        const Session session(*db_);
        MainWindow& window = *session.window;
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        QCOMPARE(window.tier(), spar_tier);
        QVERIFY(!board.isEnabled()); // no session yet

        press(window, QStringLiteral("spar_start"));
        QTRY_COMPARE(status(window), QStringLiteral("Move 1. You are still in the known lines."));
        QTRY_VERIFY(board.isEnabled());
        QVERIFY(!child<QComboBox>(window, QStringLiteral("tier_selector")).isEnabled());
        // Explore's evaluation: the bar is empty on Spar.
        QVERIFY(!child<chesscoach::app::EvalBar>(window, QStringLiteral("eval_bar")).white_share());
        for (const auto step :
             {meet_a_dubious_move, leave_the_book, reach_the_move_limit, replay_the_session}) {
            step(window, board);
            if (QTest::currentTestFailed()) {
                return;
            }
        }
    }

    void as_black_the_coach_moves_first() {
        const Session session(*db_);
        MainWindow& window = *session.window;
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        child<QRadioButton>(window, QStringLiteral("spar_black")).setChecked(true);
        press(window, QStringLiteral("spar_start"));
        QTRY_COMPARE(move_rows(window), 1);
        QTRY_VERIFY(board.isEnabled());
        QVERIFY(board.flipped());
        QCOMPARE(board.position().side_to_move(), chesscoach::Color::black);
        QCOMPARE(status(window), QStringLiteral("Move 1. You are still in the known lines."));
    }

    void end_session_saves_a_stopped_session() {
        const Session session(*db_);
        MainWindow& window = *session.window;
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        press(window, QStringLiteral("spar_start"));
        QTRY_VERIFY(board.isEnabled());
        click(board, "d2");
        click(board, "d4");
        QTRY_COMPARE(move_rows(window), 1);
        QTRY_VERIFY(move_row(window, 0).count(QLatin1Char(' ')) == 2); // the coach replied
        QTRY_VERIFY(board.isEnabled());
        press(window, QStringLiteral("spar_end"));
        QTRY_VERIFY(child<QFrame>(window, QStringLiteral("spar_summary")).isVisible());
        QVERIFY(child<QLabel>(window, QStringLiteral("spar_summary_text"))
                    .text()
                    .startsWith(QStringLiteral("You ended this practice game early.")));
        QTRY_COMPARE(rows(window, QStringLiteral("spar_sessions")), 1);
        QCOMPARE(cell(window, QStringLiteral("spar_sessions"), 0, 5),
                 QStringLiteral("Ended early"));
        QCOMPARE(cell(window, QStringLiteral("spar_sessions"), 0, 3), QStringLiteral("1"));
    }

    void each_session_after_the_first_starts_a_new_game() {
        const QTemporaryDir logs;
        const QString log = logs.filePath(QStringLiteral("engine.log"));
        qputenv("CHESSCOACH_FAKE_UCI_LOG", log.toUtf8());
        const auto new_games = [&log] {
            QFile file(log);
            if (!file.open(QIODevice::ReadOnly)) {
                return -1;
            }
            return static_cast<int>(QString::fromUtf8(file.readAll())
                                        .split(QLatin1Char('\n'))
                                        .count(QStringLiteral("ucinewgame")));
        };
        const Session session(*db_);
        MainWindow& window = *session.window;
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        // 1. h4 leaves the book, so the coach's engine, started with the session, replies.
        press(window, QStringLiteral("spar_start"));
        QTRY_VERIFY(board.isEnabled());
        click(board, "h2");
        click(board, "h4");
        QTRY_VERIFY(interruption_shown(window));
        QTRY_VERIFY(move_row(window, 0).count(QLatin1Char(' ')) == 2);
        const int first = new_games();
        QVERIFY(first >= 0);
        press(window, QStringLiteral("spar_continue"));
        press(window, QStringLiteral("spar_end"));
        QTRY_VERIFY(child<QFrame>(window, QStringLiteral("spar_summary")).isVisible());
        // The next session tells the running engine a new game begins.
        press(window, QStringLiteral("spar_start"));
        QTRY_COMPARE(new_games(), first + 1);
        qunsetenv("CHESSCOACH_FAKE_UCI_LOG");
    }

    void the_coachs_engine_starts_with_the_session() {
        const QTemporaryDir logs;
        const QString log = logs.filePath(QStringLiteral("engine.log"));
        qputenv("CHESSCOACH_FAKE_UCI_LOG", log.toUtf8());
        // Every engine the window starts logs its lines here, Explore's too: a handshake is a
        // "uci" line, a search a "go" line.
        const auto count = [&log](const QString& command) {
            QFile file(log);
            if (!file.open(QIODevice::ReadOnly)) {
                return 0;
            }
            const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
            return static_cast<int>(std::ranges::count_if(lines, [&command](const QString& line) {
                return line == command || line.startsWith(command + QLatin1Char(' '));
            }));
        };
        const Session session(*db_);
        MainWindow& window = *session.window;
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        // Explore's engine, asked for the start as the window opened, may or may not have started
        // before the window turned to Practice; Explore again makes it start, if it has not, and
        // answer, so that it is the one engine running and its handshake the one logged.
        auto& tabs = child<QTabBar>(window, QStringLiteral("screen_tabs"));
        tabs.setCurrentIndex(0);
        QTRY_VERIFY(child<QListWidget>(window, QStringLiteral("engine_lines")).count() > 0);
        tabs.setCurrentIndex(1);
        QCOMPARE(count(QStringLiteral("uci")), 1);
        const int searches = count(QStringLiteral("go"));
        // The coach's engine starts with the session, before any move.
        press(window, QStringLiteral("spar_start"));
        QTRY_VERIFY(board.isEnabled());
        QTRY_COMPARE(count(QStringLiteral("uci")), 2);
        QCOMPARE(count(QStringLiteral("go")), searches);
        QCOMPARE(move_rows(window), 0);
        press(window, QStringLiteral("spar_end"));
        QTRY_VERIFY(child<QFrame>(window, QStringLiteral("spar_summary")).isVisible());
        // The session's end came after its start on the spar worker's thread: one start, no more.
        QCOMPARE(count(QStringLiteral("uci")), 2);
        qunsetenv("CHESSCOACH_FAKE_UCI_LOG");
    }

    void closing_the_window_saves_a_running_session() {
        Session session(*db_);
        {
            MainWindow& window = *session.window;
            const auto& board = child<BoardView>(window, QStringLiteral("board"));
            press(window, QStringLiteral("spar_start"));
            QTRY_VERIFY(board.isEnabled());
            click(board, "d2");
            click(board, "d4");
            QTRY_VERIFY(move_row(window, 0).count(QLatin1Char(' ')) == 2); // the coach replied
        }
        session.window.reset();
        auto profiles = chesscoach::ProfileStore::open(session.data_dir.toStdString());
        QVERIFY(profiles.has_value());
        const auto saved = profiles->spar_sessions(profiles->profiles().front().id);
        QCOMPARE(saved.size(), std::size_t{1});
        QCOMPARE(saved.front().moves.size(), std::size_t{2});
        QVERIFY(saved.front().phase != chesscoach::Phase::ended);
        QVERIFY(!saved.front().ended_at.has_value());
    }

    void the_settings_dialog_sets_the_spar_settings() {
        const Session session(*db_);
        MainWindow& window = *session.window;
        child<QAction>(window, QStringLiteral("settings_action")).trigger();
        auto& dialog = child<QDialog>(window, QStringLiteral("settings_dialog"));
        QCOMPARE(child<QSpinBox>(dialog, QStringLiteral("spar_end_move")).value(), 4);
        child<QSpinBox>(dialog, QStringLiteral("spar_end_move")).setValue(2);
        child<QDoubleSpinBox>(dialog, QStringLiteral("spar_seconds")).setValue(0.3);
        dialog.accept();
        const SparSettings saved = Settings(session.data_dir).spar();
        QCOMPARE(saved.end_move, 2);
        QCOMPARE(saved.seconds, 0.3);

        // The next session ends at move 2: after 1. d4 and the coach's reply.
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        press(window, QStringLiteral("spar_start"));
        QTRY_VERIFY(board.isEnabled());
        click(board, "d2");
        click(board, "d4");
        QTRY_VERIFY(child<QFrame>(window, QStringLiteral("spar_summary")).isVisible());
        QVERIFY(child<QLabel>(window, QStringLiteral("spar_summary_text"))
                    .text()
                    .startsWith(QStringLiteral("The practice game reached its move limit.")));
    }

    // A slot of the test's object, which Qt Test runs, so it cannot be static.
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void the_summary_says_how_the_game_ended() {
        SparScreen screen;
        const auto summary_of = [&screen](const QString& result, bool ended, int interruptions) {
            SparSummary summary;
            summary.moves_san = QStringList{QStringLiteral("e4"), QStringLiteral("e5")};
            summary.interruptions = interruptions;
            summary.phase_reached = QStringLiteral("You stayed in the known lines.");
            summary.result = result;
            summary.ended = ended;
            screen.show_summary(summary);
            return child<QLabel>(screen, QStringLiteral("spar_summary_text")).text();
        };
        QCOMPARE(summary_of(QStringLiteral("1-0"), true, 0),
                 QStringLiteral("White won the practice game. The coach did not need to stop you. "
                                "You stayed in the known lines.\n1. e4 e5"));
        QVERIFY(summary_of(QStringLiteral("0-1"), true, 1)
                    .startsWith(QStringLiteral(
                        "Black won the practice game. The coach stopped you once.")));
        QVERIFY(summary_of(QStringLiteral("1/2-1/2"), true, 3)
                    .startsWith(QStringLiteral(
                        "The practice game was a draw. The coach stopped you 3 times.")));
        QVERIFY(summary_of(QString(), false, 2)
                    .startsWith(QStringLiteral(
                        "You ended this practice game early. The coach stopped you twice.")));
    }

    void the_spar_screen_leaves_explores_line_alone() {
        const Session session(*db_);
        MainWindow& window = *session.window;
        auto& tabs = child<QTabBar>(window, QStringLiteral("screen_tabs"));
        tabs.setCurrentIndex(0);
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        QCOMPARE(window.line().size(), std::size_t{1});
        tabs.setCurrentIndex(1);
        QCOMPARE(board.position().fen(), chesscoach::Position::start().fen());
        press(window, QStringLiteral("spar_start"));
        QTRY_VERIFY(board.isEnabled());
        click(board, "d2");
        click(board, "d4");
        QTRY_VERIFY(board.isEnabled());
        QCOMPARE(window.line().size(), std::size_t{1}); // Explore's line is untouched
        const auto& tiers = child<QComboBox>(window, QStringLiteral("tier_selector"));
        QVERIFY(!tiers.isEnabled()); // on Spar while the session runs
        tabs.setCurrentIndex(0);
        QVERIFY(tiers.isEnabled()); // Explore's tier is its own
        QCOMPARE(window.line().size(), std::size_t{1});
        QTRY_COMPARE(move_row(window, 0), QStringLiteral("1. e4"));
        QVERIFY(board.isEnabled());

        // Explore keeps its own side of the board; Spar faces the user.
        child<QAction>(window, QStringLiteral("flip_action")).trigger();
        QVERIFY(board.flipped());
        tabs.setCurrentIndex(1);
        QVERIFY(!board.flipped()); // the session's white
        tabs.setCurrentIndex(0);
        QVERIFY(board.flipped());
    }
};

} // namespace

QTEST_MAIN(SparTest)
#include "ui_spar_test.moc"

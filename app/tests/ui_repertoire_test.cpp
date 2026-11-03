// UI tests of the Repertoire screen (app; ui) on the fixture database's tier-4 tree: a choice set
// by a board move and shown in the tree, the coverage equal to core's compute_coverage on the same
// stores, a choice replaced by another move, the gap it leaves listed and Go to taking the board
// there, Choose setting a recommended move, Remove choice clearing it, and a PGN import with its
// counts; the black repertoire; and File > Export and Import profile restoring a repertoire.
// Screenshots of the repertoire and its gaps (ui_harness.hpp, fixture_db.hpp).
#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/profile_store.hpp>
#include <chesscoach/repertoire.hpp>

#include "board_view.hpp"
#include "fixture_db.hpp"
#include "main_window.hpp"
#include "repertoire_screen.hpp"
#include "ui_harness.hpp"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>

using chesscoach::Color;
using chesscoach::Position;
using chesscoach::app::BoardView;
using chesscoach::app::MainWindow;
using chesscoach::app::test::child;
using chesscoach::app::test::click;
using chesscoach::app::test::FixtureDb;
using chesscoach::app::test::save_screenshot;

namespace {

constexpr int tier = 4;

// A window on the fixture database at tier 4 with the Repertoire screen current.
struct Session {
    QTemporaryDir root;
    QString data_dir = root.filePath(QStringLiteral("data"));
    std::unique_ptr<MainWindow> window;

    explicit Session(const FixtureDb& db) {
        QDir().mkpath(data_dir);
        {
            QSettings ini(QDir(data_dir).filePath(QStringLiteral("settings.ini")),
                          QSettings::IniFormat);
            ini.setValue(QStringLiteral("tier"), tier);
        }
        window = std::make_unique<MainWindow>(data_dir, QString::fromStdString(db.path()));
        window->show();
        child<QTabBar>(*window, QStringLiteral("screen_tabs")).setCurrentIndex(2);
    }
};

QString text(const MainWindow& window, const QString& name) {
    return child<QLabel>(window, name).text();
}

QString cell(const MainWindow& window, const QString& table, int row, int column) {
    const QTableWidgetItem* item = child<QTableWidget>(window, table).item(row, column);
    return item != nullptr ? item->text() : QString();
}

int rows(const MainWindow& window, const QString& table) {
    return child<QTableWidget>(window, table).rowCount();
}

void press(const MainWindow& window, const QString& name) {
    child<QAbstractButton>(window, name).click();
}

// The tree's moves depth first, each indented two spaces per level: "1. a3", "  1... e5".
QStringList tree(const MainWindow& window) {
    const QAbstractItemModel& model =
        *child<QTreeView>(window, QStringLiteral("repertoire_tree")).model();
    QStringList out;
    std::vector<std::pair<QModelIndex, qsizetype>> stack;
    for (int row = model.rowCount() - 1; row >= 0; --row) {
        stack.emplace_back(model.index(row, 0), 0);
    }
    while (!stack.empty()) {
        const auto [index, depth] = stack.back();
        stack.pop_back();
        out << QString(depth * 2, QLatin1Char(' ')) + index.data().toString();
        for (int row = model.rowCount(index) - 1; row >= 0; --row) {
            stack.emplace_back(model.index(row, 0, index), depth + 1);
        }
    }
    return out;
}

// The coverage line core's compute_coverage gives for the white repertoire saved in data_dir.
QString expected_coverage(const FixtureDb& db, const QString& data_dir) {
    const auto store = chesscoach::OpeningStore::open(db.path());
    const auto profiles = chesscoach::ProfileStore::open(data_dir.toStdString());
    if (!store || !profiles) {
        return QStringLiteral("no stores");
    }
    const auto repertoire = profiles->repertoire(profiles->profiles().front().id, Color::white);
    const auto coverage = chesscoach::compute_coverage(
        *store, repertoire, Color::white, static_cast<chesscoach::Tier>(tier), {.max_plies = 20});
    return QStringLiteral("Coverage at 1800-1999 to 20 plies: %1%")
        .arg(coverage.covered * 100.0, 0, 'f', 1);
}

Position after(std::initializer_list<const char*> moves) {
    Position position = Position::start();
    for (const char* move : moves) {
        static_cast<void>(position.play_uci(move));
    }
    return position;
}

class RepertoireTest : public QObject {
    Q_OBJECT

    std::unique_ptr<FixtureDb> db_;

    void choose_on_the_board(const Session& session) const {
        const MainWindow& window = *session.window;
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        // An empty repertoire: the start is the one gap.
        QTRY_COMPARE(text(window, QStringLiteral("repertoire_coverage")),
                     QStringLiteral("Coverage at 1800-1999 to 20 plies: 0.0%"));
        QTRY_COMPARE(rows(window, QStringLiteral("repertoire_gaps")), 1);
        QCOMPARE(cell(window, QStringLiteral("repertoire_gaps"), 0, 1),
                 QStringLiteral("the start"));

        // 1. e4 is chosen: e4's node at tier 4 has no replies, so all is covered.
        click(board, "e2");
        click(board, "e4");
        QTRY_COMPARE(tree(window), QStringList{QStringLiteral("1. e4")});
        QTRY_COMPARE(text(window, QStringLiteral("repertoire_coverage")),
                     QStringLiteral("Coverage at 1800-1999 to 20 plies: 100.0%"));
        QCOMPARE(text(window, QStringLiteral("repertoire_coverage")),
                 expected_coverage(*db_, session.data_dir));

        // Back at the start, 1. a3 replaces it, and 1... e5, black's one reply, is a gap.
        child<QAction>(window, QStringLiteral("new_game_action")).trigger();
        QTRY_VERIFY(
            text(window, QStringLiteral("repertoire_position")).contains(QStringLiteral("e4")));
        click(board, "a2");
        click(board, "a3");
        QTRY_COMPARE(tree(window), QStringList{QStringLiteral("1. a3")});
        QTRY_COMPARE(text(window, QStringLiteral("repertoire_coverage")),
                     QStringLiteral("Coverage at 1800-1999 to 20 plies: 0.0%"));
        QCOMPARE(text(window, QStringLiteral("repertoire_coverage")),
                 expected_coverage(*db_, session.data_dir));
        QTRY_COMPARE(cell(window, QStringLiteral("repertoire_gaps"), 0, 1),
                     QStringLiteral("1. a3 e5"));
        QCOMPARE(cell(window, QStringLiteral("repertoire_gaps"), 0, 0), QStringLiteral("100.0%"));
    }

    void fill_the_gap(const Session& session) const {
        MainWindow& window = *session.window;
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        save_screenshot(window, QStringLiteral("repertoire"), QStringLiteral("gaps"));
        press(window, QStringLiteral("repertoire_goto"));
        QCOMPARE(board.position().fen(), after({"a2a3", "e7e5"}).fen());
        QCOMPARE(child<QListWidget>(window, QStringLiteral("move_list")).item(0)->text(),
                 QStringLiteral("1. a3 e5"));
        QVERIFY(text(window, QStringLiteral("repertoire_position"))
                    .startsWith(QStringLiteral("No choice here yet")));

        // The recommendation panel has the position's one move; Choose makes it the choice.
        QTRY_COMPARE(rows(window, QStringLiteral("repertoire_scores")), 1);
        QCOMPARE(cell(window, QStringLiteral("repertoire_scores"), 0, 0), QStringLiteral("Nf3"));
        press(window, QStringLiteral("repertoire_choose"));
        QTRY_COMPARE(tree(window),
                     (QStringList{QStringLiteral("1. a3"), QStringLiteral("  1... e5"),
                                  QStringLiteral("    2. Nf3")}));
        QTRY_COMPARE(text(window, QStringLiteral("repertoire_coverage")),
                     QStringLiteral("Coverage at 1800-1999 to 20 plies: 100.0%"));
        QCOMPARE(text(window, QStringLiteral("repertoire_coverage")),
                 expected_coverage(*db_, session.data_dir));
        QCOMPARE(text(window, QStringLiteral("repertoire_position")),
                 QStringLiteral("Your move here: Nf3"));
        QTRY_VERIFY(!board.animating());
        save_screenshot(window, QStringLiteral("repertoire"), QStringLiteral("tree"));

        // Remove choice takes it back out.
        press(window, QStringLiteral("repertoire_remove"));
        QTRY_COMPARE(tree(window), QStringList{QStringLiteral("1. a3")});
        QTRY_COMPARE(text(window, QStringLiteral("repertoire_coverage")),
                     QStringLiteral("Coverage at 1800-1999 to 20 plies: 0.0%"));
    }

  private slots:
    void initTestCase() {
        db_ = std::make_unique<FixtureDb>();
        // These tests keep the engine out: the binary named here does not exist.
        qputenv("CHESS_COACH_STOCKFISH", "/nonexistent/stockfish-for-the-repertoire-tests");
    }
    void cleanupTestCase() { db_.reset(); }

    void a_repertoire_is_built_its_gaps_filled_and_a_pgn_imported() {
        const Session session(*db_);
        for (const auto step :
             {&RepertoireTest::choose_on_the_board, &RepertoireTest::fill_the_gap}) {
            (this->*step)(session);
            if (QTest::currentTestFailed()) {
                return;
            }
        }
        MainWindow& window = *session.window;
        // 1. e4 is a conflict with 1. a3 and stays out; 2. Nf3 after 1. e4 e5 is added.
        const QString pgn = session.root.filePath(QStringLiteral("lines.pgn"));
        std::ofstream(pgn.toStdString()) << "[Event \"Lines\"]\n\n1. e4 e5 2. Nf3 Nc6 *\n";
        child<chesscoach::app::RepertoireScreen>(window, QStringLiteral("repertoire_screen"))
            .import_pgn(pgn);
        QTRY_COMPARE(
            text(window, QStringLiteral("repertoire_message")),
            QStringLiteral("Imported 1 games and 0 variations: 2 choices, 1 added, 0 already "
                           "made, 1 conflicts, 0 unparsable."));
        QTRY_COMPARE(tree(window), QStringList{QStringLiteral("1. a3")});
    }

    void the_black_repertoire_is_its_own() {
        const Session session(*db_);
        MainWindow& window = *session.window;
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        child<QAbstractButton>(window, QStringLiteral("repertoire_black")).click();
        QTRY_VERIFY(board.flipped());
        // White's 1. a3 only follows the line; black's 1... e5 is then the choice.
        click(board, "a2");
        click(board, "a3");
        QTRY_VERIFY(text(window, QStringLiteral("repertoire_position"))
                        .startsWith(QStringLiteral("No choice here yet")));
        click(board, "e7");
        click(board, "e5");
        QTRY_COMPARE(tree(window),
                     (QStringList{QStringLiteral("1. a3"), QStringLiteral("  1... e5")}));
    }

    void export_and_import_restore_a_repertoire() {
        const Session session(*db_);
        MainWindow& window = *session.window;
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        QTRY_COMPARE(tree(window), QStringList{QStringLiteral("1. e4")});
        const QString exported = session.root.filePath(QStringLiteral("profile.db"));
        window.export_profile(exported);
        QTRY_VERIFY(QFile::exists(exported));
        window.export_profile(exported); // never over a file
        QTRY_VERIFY(window.findChild<QMessageBox*>(QStringLiteral("profile_error")) != nullptr);
        window.findChild<QMessageBox*>(QStringLiteral("profile_error"))->close();
        QTRY_VERIFY(window.findChild<QMessageBox*>(QStringLiteral("profile_error")) == nullptr);

        child<QAction>(window, QStringLiteral("new_game_action")).trigger();
        press(window, QStringLiteral("repertoire_remove"));
        QTRY_VERIFY(tree(window).isEmpty());

        window.import_profile(exported);
        QTRY_COMPARE(tree(window), QStringList{QStringLiteral("1. e4")});
        QVERIFY(
            QFile::exists(QDir(session.data_dir).filePath(QStringLiteral("profiles.db.previous"))));
        QVERIFY(window.findChild<QWidget*>(QStringLiteral("profile_error")) == nullptr);
    }
};

} // namespace

QTEST_MAIN(RepertoireTest)
#include "ui_repertoire_test.moc"

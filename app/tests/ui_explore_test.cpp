// UI tests of the Explore screen on a fixture database (app; ui): opening it and seeing the start
// position's moves with their badges and bars, the header's columns, tooltips and the line under
// the name, the pills, the results bar and the selection as painted, switching tiers, playing
// moves on the board and from the table, jumping back through the move list, an off-book
// position, the evaluation bar, a database that does not open, the settings file, the themes
// repainting the table and the About dialog, with screenshots of Explore in both themes
// (ui_harness.hpp, fixture_db.hpp).
#include <chesscoach/position.hpp>
#include <chesscoach/version.hpp>

#include "application.hpp"
#include "board_view.hpp"
#include "eval_bar.hpp"
#include "explore_model.hpp"
#include "explore_screen.hpp"
#include "fixture_db.hpp"
#include "main_window.hpp"
#include "result_delegate.hpp"
#include "study_palette.hpp"
#include "tables.hpp"
#include "ui_harness.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QHelpEvent>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QModelIndex>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QStringList>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using chesscoach::Badges;
using chesscoach::Position;
using chesscoach::app::BoardView;
using chesscoach::app::EvalBar;
using chesscoach::app::ExploreModel;
using chesscoach::app::ExploreScreen;
using chesscoach::app::MainWindow;
using chesscoach::app::StudyPalette;
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
    return child<QLabel>(window, QStringLiteral("database_status"))
        .text()
        .startsWith(QStringLiteral("Opening database: "));
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

QString label(const MainWindow& window, const QString& name) {
    return child<QLabel>(window, name).text();
}

QVariant header(const MainWindow& window, int column, int role = Qt::DisplayRole) {
    return model(window).headerData(column, Qt::Horizontal, role);
}

// Whether a pixel of the widget's rendering in the rect (the widget's coordinates) has the colour.
bool painted(QWidget& widget, const QRect& rect, const QColor& colour) {
    const QImage image = widget.grab().toImage();
    const qreal ratio = image.devicePixelRatio();
    const QRect pixels(
        QPoint(qRound(rect.left() * ratio), qRound(rect.top() * ratio)),
        QPoint(qRound((rect.right() + 1) * ratio) - 1, qRound((rect.bottom() + 1) * ratio) - 1));
    for (int y = pixels.top(); y <= pixels.bottom(); ++y) {
        for (int x = pixels.left(); x <= pixels.right(); ++x) {
            if (image.pixelColor(x, y).rgb() == colour.rgb()) {
                return true;
            }
        }
    }
    return false;
}

class ExploreTest : public QObject {
    Q_OBJECT

    std::unique_ptr<FixtureDb> db_;

  private slots:
    void initTestCase() {
        chesscoach::app::configure_application();
        db_ = std::make_unique<FixtureDb>();
        // These tests keep the engine out: the binary named here does not exist.
        qputenv("CHESS_COACH_STOCKFISH", "/nonexistent/stockfish-for-the-explore-tests");
    }
    void cleanupTestCase() { db_.reset(); }

    void opening_the_database_shows_the_start_position() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        QCOMPARE(child<QLabel>(window, QStringLiteral("database_status")).text(),
                 QStringLiteral("Opening database: January 2013, data version 0"));
        QVERIFY(child<QLabel>(window, QStringLiteral("database_status"))
                    .toolTip()
                    .endsWith(QStringLiteral("openings.db (--db)")));
        // Tier 2 to start, where the fixture has no games: the start position is off book.
        QCOMPARE(window.tier(), MainWindow::default_tier);
        QTRY_COMPARE(
            message(window),
            QStringLiteral("You have left the known lines. Too few games by players at your rating "
                           "reach this position."));
        QVERIFY(message_shown(window));

        select_tier(window, 3);
        QTRY_COMPARE(model(window).rowCount(), 4);
        QCOMPARE(table_moves(window), (QStringList{QStringLiteral("e4"), QStringLiteral("d4"),
                                                   QStringLiteral("Nf3"), QStringLiteral("f3")}));
        QCOMPARE(cell(window, 0, ExploreModel::badges_column), QStringLiteral("Book, Popular"));
        QCOMPARE(
            model(window).index(0, ExploreModel::badges_column).data(ExploreModel::badges_role),
            QVariant(3U));
        QCOMPARE(cell(window, 0, ExploreModel::games_column), QStringLiteral("600"));
        QCOMPARE(cell(window, 0, ExploreModel::result_column),
                 QStringLiteral("43.3 / 10.0 / 46.7"));
        const auto shares = model(window)
                                .index(0, ExploreModel::result_column)
                                .data(ExploreModel::shares_role)
                                .value<QList<double>>();
        QCOMPARE(shares.size(), 3);
        QCOMPARE(cell(window, 0, ExploreModel::score_column), QStringLiteral("48.3%"));
        QCOMPARE(cell(window, 0, ExploreModel::top_tier_column), QStringLiteral("45%"));
        QCOMPARE(cell(window, 2, ExploreModel::top_tier_column), QStringLiteral("0%"));
        QCOMPARE(cell(window, 3, ExploreModel::badges_column), QString());
        QCOMPARE(
            model(window).index(3, ExploreModel::badges_column).data(ExploreModel::badges_role),
            QVariant(0U));
        QCOMPARE(label(window, QStringLiteral("explore_name")),
                 QStringLiteral("Starting position"));
        QVERIFY(!child<QLabel>(window, QStringLiteral("explore_eco")).isVisible());
        QCOMPARE(label(window, QStringLiteral("explore_summary")),
                 QStringLiteral("White to move · 1,000 games by players rated 1600–1799 · the "
                                "database's evaluation +0.2 for White"));
        QCOMPARE(child<QLabel>(window, QStringLiteral("explore_summary")).toolTip(),
                 QStringLiteral("The database's evaluation comes from a search to depth 40."));
        save_screenshot(window, QStringLiteral("explore"), QStringLiteral("start-tier3"));

        // Tier 4: 1. a3 is Popular and Dubious, losing 109 cp.
        select_tier(window, 4);
        QTRY_COMPARE(table_moves(window), (QStringList{QStringLiteral("e4"), QStringLiteral("a3"),
                                                       QStringLiteral("d4")}));
        QCOMPARE(cell(window, 1, ExploreModel::badges_column), QStringLiteral("Popular, Dubious"));
        QCOMPARE(cell(window, 2, ExploreModel::badges_column), QStringLiteral("Book"));
        // 1. a3: no named position on the line.
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "a2");
        click(board, "a3");
        QTRY_COMPARE(label(window, QStringLiteral("explore_name")),
                     QStringLiteral("No opening name"));
    }

    void the_screen_says_what_it_is_for() {
        const Session session(QString::fromStdString(db_->path()));
        const MainWindow& window = *session.window;
        const auto& description = child<QLabel>(window, QStringLiteral("explore_description"));
        QCOMPARE(description.text(),
                 QStringLiteral("See what players at your rating play here, and what the engine "
                                "thinks. Play moves on the board to go deeper."));
        QVERIFY(description.property("muted").toBool()); // the stylesheets' muted colour
        // A screen told of no database without a reason says so plainly.
        const ExploreScreen screen;
        QCOMPARE(
            child<QLabel>(screen, QStringLiteral("explore_message")).text(),
            QStringLiteral("No opening database is open. Import one with File > Import database."));
    }

    void the_header_names_the_columns_in_plain_words() {
        const Session session(QString::fromStdString(db_->path()));
        const MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        QCOMPARE(model(window).columnCount(), 6);
        QStringList names;
        for (int column = 0; column < model(window).columnCount(); ++column) {
            names << header(window, column).toString();
        }
        QCOMPARE(names,
                 (QStringList{QStringLiteral("Move"), QStringLiteral("Badges"),
                              QStringLiteral("Games"), QStringLiteral("Results"),
                              QStringLiteral("Score for White"), QStringLiteral("Top players")}));
        QCOMPARE(header(window, ExploreModel::result_column, chesscoach::app::header_note_role),
                 QVariant(QStringLiteral("white · draw · black")));
        QCOMPARE(header(window, ExploreModel::badges_column, Qt::ToolTipRole),
                 QVariant(QStringLiteral(
                     "Book: what strong players (2200 and above) choose. Popular: what players at "
                     "your rating often choose. Dubious: popular at your rating, but it loses "
                     "ground.")));
        QCOMPARE(header(window, ExploreModel::games_column, Qt::ToolTipRole),
                 QVariant(QStringLiteral(
                     "How many games by players at this rating played this move here.")));
        QCOMPARE(
            header(window, ExploreModel::result_column, Qt::ToolTipRole),
            QVariant(QStringLiteral("White wins · draws · Black wins, as a share of the games")));
        QCOMPARE(header(window, ExploreModel::score_column, Qt::ToolTipRole),
                 QVariant(QStringLiteral(
                     "The share of the points White scored here: a win counts 1, a draw ½.")));
        QCOMPARE(
            header(window, ExploreModel::top_tier_column, Qt::ToolTipRole),
            QVariant(QStringLiteral("How often players rated 2200 and above choose this move")));
        QVERIFY(!header(window, ExploreModel::move_column, Qt::ToolTipRole).isValid());
        // The numbers read right-aligned, under right-aligned labels.
        for (const int column : {
                 ExploreModel::games_column,
                 ExploreModel::score_column,
                 ExploreModel::top_tier_column,
             }) {
            QVERIFY(header(window, column, Qt::TextAlignmentRole)
                        .value<Qt::Alignment>()
                        .testFlag(Qt::AlignRight));
        }
        QVERIFY(!header(window, ExploreModel::result_column, Qt::TextAlignmentRole)
                     .value<Qt::Alignment>()
                     .testFlag(Qt::AlignRight));
    }

    void the_table_fits_its_rows_and_shares_out_the_width() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        select_tier(window, 3);
        QTRY_COMPARE(model(window).rowCount(), 4);
        const auto& screen = child<ExploreScreen>(window, QStringLiteral("explore_screen"));
        const auto& table = child<QTableView>(window, QStringLiteral("explore_table"));
        const auto& columns = *table.horizontalHeader();
        // QTableView keeps the content's width protected; QAbstractItemView gives it.
        const QAbstractItemView& view = table;
        // The rows fill the table with no empty body under them, and the engine's panel sits
        // right below it.
        QTRY_COMPARE(table.viewport()->height(), model(window).rowCount() * table.rowHeight(0));
        const int table_bottom = table.mapTo(&screen, QPoint(0, table.height())).y();
        const int panel_top = child<QWidget>(window, QStringLiteral("engine_panel")).y();
        QVERIFY(panel_top >= table_bottom && panel_top <= table_bottom + 10);
        // The columns fill the width, none narrower than its content.
        QCOMPARE(columns.length(), table.viewport()->width());
        for (int column = 0; column < model(window).columnCount(); ++column) {
            QVERIFY(columns.sectionSize(column) >=
                    std::max(columns.sectionSizeHint(column), view.sizeHintForColumn(column)));
        }
        save_screenshot(window, QStringLiteral("explore"), QStringLiteral("fitted-tier3"));
        // Every column keeps its width, and the Results bar its place, from one position to the
        // next: tier 4's rows hold other pills, 1. e4's other moves.
        QList<int> widths;
        for (int column = 0; column < model(window).columnCount(); ++column) {
            widths << columns.sectionSize(column);
        }
        const auto same_columns = [&] {
            for (int column = 0; column < model(window).columnCount(); ++column) {
                if (columns.sectionSize(column) != widths.at(column)) {
                    return false;
                }
            }
            return true;
        };
        select_tier(window, 4);
        QTRY_COMPARE(table_moves(window), (QStringList{QStringLiteral("e4"), QStringLiteral("a3"),
                                                       QStringLiteral("d4")}));
        QVERIFY(same_columns());
        select_tier(window, 3);
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        QTRY_COMPARE(model(window).rowCount(), 4);
        click(board, "e2");
        click(board, "e4");
        QTRY_COMPARE(table_moves(window), (QStringList{QStringLiteral("e5"), QStringLiteral("c5"),
                                                       QStringLiteral("e6")}));
        QVERIFY(same_columns());
    }

    void the_table_paints_pills_bars_and_the_selection() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        select_tier(window, 4); // e4 Book and Popular, a3 Popular and Dubious, d4 Book
        QTRY_COMPARE(table_moves(window), (QStringList{QStringLiteral("e4"), QStringLiteral("a3"),
                                                       QStringLiteral("d4")}));
        auto& table = child<QTableView>(window, QStringLiteral("explore_table"));
        table.clearSelection();
        QWidget& rows = *table.viewport();
        const StudyPalette& light = StudyPalette::light();
        const auto rect = [&](int row, int column) {
            return table.visualRect(model(window).index(row, column));
        };
        // The pills in their colours, one per badge.
        QVERIFY(painted(rows, rect(0, ExploreModel::badges_column), light.book_bg));
        QVERIFY(painted(rows, rect(0, ExploreModel::badges_column), light.popular_bg));
        QVERIFY(!painted(rows, rect(0, ExploreModel::badges_column), light.dubious_bg));
        QVERIFY(painted(rows, rect(1, ExploreModel::badges_column), light.popular_bg));
        QVERIFY(painted(rows, rect(1, ExploreModel::badges_column), light.dubious_bg));
        QVERIFY(!painted(rows, rect(1, ExploreModel::badges_column), light.book_bg));
        QVERIFY(painted(rows, rect(2, ExploreModel::badges_column), light.book_bg));
        QVERIFY(!painted(rows, rect(2, ExploreModel::badges_column), light.popular_bg));
        // The results bar: the three segments in a hairline frame, 150 px wide after the cell's
        // padding and nothing of it beyond.
        const QRect results = rect(0, ExploreModel::result_column);
        QVERIFY(results.width() >= 170);
        const QRect bar(results.left() + 10, results.top(), 150, results.height() - 1);
        QVERIFY(painted(rows, bar, light.result_white));
        QVERIFY(painted(rows, bar, light.result_draw));
        QVERIFY(painted(rows, bar, light.result_black));
        QVERIFY(painted(rows, bar, light.border));
        for (const QRect& outside : {
                 QRect(results.left(), results.top(), 10, bar.height()),
                 QRect(bar.right() + 1, results.top(), results.right() - bar.right(), bar.height()),
             }) {
            QVERIFY(!painted(rows, outside, light.result_black));
            QVERIFY(!painted(rows, outside, light.border));
        }
        // Each row's rule but the last's, which the table's frame draws, and the selected row's
        // fill.
        QVERIFY(painted(rows, rect(0, ExploreModel::games_column), light.row_line));
        QVERIFY(!painted(rows, rect(2, ExploreModel::games_column), light.row_line));
        QVERIFY(!painted(rows, rect(1, ExploreModel::games_column), light.selection));
        table.selectRow(1);
        QVERIFY(painted(rows, rect(1, ExploreModel::games_column), light.selection));
        QVERIFY(!painted(rows, rect(0, ExploreModel::games_column), light.selection));
        // The header in the chrome, with its hairline.
        const QHeaderView& columns = *table.horizontalHeader();
        QVERIFY(painted(*columns.viewport(), columns.viewport()->rect(), light.chrome));
        QVERIFY(painted(*columns.viewport(), columns.viewport()->rect(), light.border));
        save_screenshot(window, QStringLiteral("explore"), QStringLiteral("pills-tier4"));
        // Each pill's tooltip says what its badge means.
        const QModelIndex a3 = model(window).index(1, ExploreModel::badges_column);
        const auto pills = chesscoach::app::badge_pills(
            table.visualRect(a3),
            Badges(static_cast<std::uint8_t>(a3.data(ExploreModel::badges_role).toUInt())),
            table.font(), rows);
        QCOMPARE(pills.size(), std::size_t{2});
        for (const auto& [badge, area] : pills) {
            QHelpEvent event(QEvent::ToolTip, area.center(), rows.mapToGlobal(area.center()));
            QApplication::sendEvent(&rows, &event);
            QTRY_COMPARE(QToolTip::text(), chesscoach::app::badge_sentence(badge));
        }
        QCOMPARE(chesscoach::app::badge_sentence(pills.back().badge),
                 QStringLiteral("Dubious: popular at your rating, but it loses ground."));
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
        QCOMPARE(cell(window, 0, ExploreModel::top_tier_column), QStringLiteral("55%"));
        select_tier(window, 0);
        QTRY_COMPARE(table_moves(window),
                     (QStringList{QStringLiteral("e4"), QStringLiteral("f3")}));
        QVERIFY(label(window, QStringLiteral("explore_summary"))
                    .contains(QStringLiteral(" · 20 games by players rated under 1200 · ")));
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
        QCOMPARE(label(window, QStringLiteral("explore_name")), QStringLiteral("King's Pawn Game"));
        QCOMPARE(label(window, QStringLiteral("explore_eco")), QStringLiteral("B00"));
        QVERIFY(child<QLabel>(window, QStringLiteral("explore_eco")).isVisible());
        QCOMPARE(label(window, QStringLiteral("explore_summary")),
                 QStringLiteral("Black to move · 600 games by players rated 1600–1799 · the "
                                "database's evaluation +0.3 for White"));
        // Black to move: the score is still white's, 140 wins and 30 draws in 300 games.
        QCOMPARE(cell(window, 0, ExploreModel::score_column), QStringLiteral("51.7%"));
        QCOMPARE(cell(window, 1, ExploreModel::badges_column), QStringLiteral("Book, Popular"));
        QCOMPARE(cell(window, 2, ExploreModel::badges_column), QString()); // 8%, unevaluated

        click(board, "c7");
        click(board, "c5");
        QTRY_COMPARE(table_moves(window),
                     (QStringList{QStringLiteral("Nf3"), QStringLiteral("Nc3")}));
        QCOMPARE(label(window, QStringLiteral("explore_name")), QStringLiteral("Sicilian Defense"));
        QCOMPARE(label(window, QStringLiteral("explore_eco")), QStringLiteral("B20"));
        // No tier-6 node after 1. e4 c5: no share to show, and the cell says why.
        QCOMPARE(cell(window, 0, ExploreModel::top_tier_column), QStringLiteral("–"));
        QCOMPARE(model(window).index(0, ExploreModel::top_tier_column).data(Qt::ToolTipRole),
                 QVariant(QStringLiteral("No games by players rated 2200 and above here")));
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
        QTRY_COMPARE(label(window, QStringLiteral("explore_eco")), QStringLiteral("C20"));
        QCOMPARE(label(window, QStringLiteral("explore_name")), QStringLiteral("King's Pawn Game"));
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
        QTRY_COMPARE(
            message(window),
            QStringLiteral("You have left the known lines. Too few games by players at your rating "
                           "reach this position."));
        QVERIFY(message_shown(window));
        QVERIFY(!child<QTableView>(window, QStringLiteral("explore_table")).isVisible());
        QCOMPARE(label(window, QStringLiteral("explore_name")), QStringLiteral("King's Pawn Game"));
        QCOMPARE(label(window, QStringLiteral("explore_eco")), QStringLiteral("B00"));
        // The database has no row for 1. e4 e6, so neither its side to move nor an evaluation.
        QCOMPARE(label(window, QStringLiteral("explore_summary")),
                 QStringLiteral("Too few games by players rated 1600–1799 to show"));
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
        QTRY_VERIFY(bar.white_share().has_value());
        const int width = bar.width();
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
        QTRY_COMPARE(label(window, QStringLiteral("explore_summary")),
                     QStringLiteral("Black to move · 10 games by players rated under 1200 · the "
                                    "database's evaluation mated in 1 for White"));
        QCOMPARE(bar.white_share(), std::optional(0.0));
        QVERIFY(bar.isVisible());
        // 1. Nf3 d5 has no evaluation: the bar empties, keeping its width beside the board.
        child<QAction>(window, QStringLiteral("new_game_action")).trigger();
        click(board, "g1");
        click(board, "f3");
        click(board, "d7");
        click(board, "d5");
        QTRY_COMPARE(label(window, QStringLiteral("explore_summary")),
                     QStringLiteral("White to move · too few games by players rated under 1200 "
                                    "to show"));
        QVERIFY(child<QLabel>(window, QStringLiteral("explore_summary")).toolTip().isEmpty());
        QVERIFY(bar.isVisible());
        QCOMPARE(bar.width(), width);
        QCOMPARE(bar.white_share(), std::nullopt);
    }

    void a_database_that_does_not_open_leaves_the_empty_state() const {
        const QTemporaryDir root;
        const QString broken = root.filePath(QStringLiteral("broken.db"));
        QVERIFY(broken != QString::fromStdString(db_->path()));
        {
            QFile file(broken);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("not a database");
        }
        const Session session(broken);
        MainWindow& window = *session.window;
        QTRY_VERIFY(window.findChild<QMessageBox*>(QStringLiteral("database_error")) != nullptr);
        auto& box = child<QMessageBox>(window, QStringLiteral("database_error"));
        QVERIFY(box.text().contains(broken));
        QVERIFY(
            box.text().contains(QStringLiteral("It is not a database file, or it is damaged.")));
        QCOMPARE(child<QLabel>(window, QStringLiteral("database_status")).text(),
                 QStringLiteral("No opening database"));
        // The empty state names the file that did not open, not the places looked in.
        QVERIFY(message(window).startsWith(QStringLiteral("The opening database")));
        QVERIFY(message(window).contains(QDir::toNativeSeparators(broken)));
        box.accept();
        // The board still plays.
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        QCOMPARE(window.line(), (std::vector<std::string>{"e2e4"}));
    }

    void settings_are_written_only_by_a_theme_change() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        QTRY_COMPARE(model(window).rowCount(), 0);
        // Opening a database and playing a move write nothing: the data directory is not there.
        QVERIFY(!QFileInfo::exists(session.data_dir));

        child<QAction>(window, QStringLiteral("theme_dark")).trigger();
        QVERIFY(child<QAction>(window, QStringLiteral("theme_dark")).isChecked());
        QCOMPARE(QDir(session.root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("data")});
        QCOMPARE(QDir(session.data_dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("settings.ini")});

        // A second window on the same data directory starts in the dark theme.
        const MainWindow again(session.data_dir, QString());
        QVERIFY(child<QAction>(again, QStringLiteral("theme_dark")).isChecked());
        child<QAction>(window, QStringLiteral("theme_light")).trigger();
    }

    void the_themes_restyle_explore() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        select_tier(window, 3);
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        QTRY_COMPARE(model(window).rowCount(), 3);
        QTRY_VERIFY(!board.animating());
        const auto& screen = child<ExploreScreen>(window, QStringLiteral("explore_screen"));
        const auto& columns =
            *child<QTableView>(window, QStringLiteral("explore_table")).horizontalHeader();
        child<QAction>(window, QStringLiteral("theme_light")).trigger();
        QCOMPARE(board.board_scene().theme().dark_square,
                 chesscoach::app::BoardTheme::light().dark_square);
        QCOMPARE(screen.study_palette().chrome, StudyPalette::light().chrome);
        QVERIFY(
            painted(*columns.viewport(), columns.viewport()->rect(), StudyPalette::light().chrome));
        save_screenshot(window, QStringLiteral("explore"), QStringLiteral("light"));
        child<QAction>(window, QStringLiteral("theme_dark")).trigger();
        QCOMPARE(board.board_scene().theme().dark_square,
                 chesscoach::app::BoardTheme::dark().dark_square);
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("#1f1b17")));
        // The table repaints in the dark palette: its header, and the pills of 1... c5.
        QCOMPARE(screen.study_palette().chrome, StudyPalette::dark().chrome);
        QVERIFY(
            painted(*columns.viewport(), columns.viewport()->rect(), StudyPalette::dark().chrome));
        QVERIFY(!painted(*columns.viewport(), columns.viewport()->rect(),
                         StudyPalette::light().chrome));
        auto& table = child<QTableView>(window, QStringLiteral("explore_table"));
        table.clearSelection();
        QVERIFY(painted(*table.viewport(),
                        table.visualRect(model(window).index(1, ExploreModel::badges_column)),
                        StudyPalette::dark().book_bg));
        save_screenshot(window, QStringLiteral("explore"), QStringLiteral("dark"));
        child<QAction>(window, QStringLiteral("theme_light")).trigger();
    }

    void the_about_dialog_shows_both_versions() {
        const Session session(QString::fromStdString(db_->path()));
        MainWindow& window = *session.window;
        QTRY_VERIFY(opened(window));
        child<QAction>(window, QStringLiteral("about_action")).trigger();
        auto& dialog = child<QDialog>(window, QStringLiteral("about_dialog"));
        QVERIFY(dialog.isVisible());
        const QString text = child<QLabel>(dialog, QStringLiteral("about_text")).text();
        QVERIFY(text.contains(
            QStringLiteral("chess-coach ") +
            QString::fromStdString(chesscoach::to_string(chesscoach::current_version()))));
        QVERIFY(text.contains(QStringLiteral("data version 0")));
        QVERIFY(text.contains(QStringLiteral("Colin M.L. Burnett")));
        QVERIFY(text.contains(QStringLiteral("LGPL")));
        save_screenshot(dialog, QStringLiteral("about"), QStringLiteral("dialog"));
        dialog.reject();
    }
};

} // namespace

QTEST_MAIN(ExploreTest)
#include "ui_explore_test.moc"

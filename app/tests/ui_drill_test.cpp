// UI tests of the Drill screen (app; ui): a white repertoire of two choices saved through core's
// ProfileStore, its cards taken away so that entering the screen syncs them back; a drill showing
// a card at its position, a correct move recorded as good and the card no longer due, a wrong move
// recorded as again with the card's move played on the board, and the summary when no card is
// due; and Easy changing a correct move's grade. Screenshots of a card and the summary
// (ui_harness.hpp, fixture_db.hpp).
#include <chesscoach/position.hpp>
#include <chesscoach/profile_store.hpp>
#include <chesscoach/repertoire.hpp>
#include <chesscoach/scheduler.hpp>

#include "board_view.hpp"
#include "fixture_db.hpp"
#include "main_window.hpp"
#include "ui_harness.hpp"

#include <QAbstractButton>
#include <QDir>
#include <QLabel>
#include <QObject>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using chesscoach::Color;
using chesscoach::Position;
using chesscoach::ProfileStore;
using chesscoach::app::BoardView;
using chesscoach::app::MainWindow;
using chesscoach::app::test::child;
using chesscoach::app::test::click;
using chesscoach::app::test::FixtureDb;
using chesscoach::app::test::save_screenshot;

namespace {

Position after(std::initializer_list<const char*> moves) {
    Position position = Position::start();
    for (const char* move : moves) {
        static_cast<void>(position.play_uci(move));
    }
    return position;
}

// A data directory whose white repertoire answers the start with 1. e4 and 1. e4 e5 with 2. Nf3,
// without cards, and a window on it with the Drill screen current.
struct Session {
    QTemporaryDir root;
    QString data_dir = root.filePath(QStringLiteral("data"));
    std::unique_ptr<MainWindow> window;

    explicit Session(const FixtureDb& db) {
        {
            auto profiles = ProfileStore::open(data_dir.toStdString());
            QTEST_ASSERT(profiles.has_value());
            const std::int64_t profile = profiles->profiles().front().id;
            const std::vector<std::pair<Position, std::string>> choices{
                {Position::start(), "e2e4"},
                {after({"e2e4", "e7e5"}), "g1f3"},
            };
            for (const auto& [position, move] : choices) {
                const auto set = profiles->set_choice(profile, Color::white,
                                                      chesscoach::RepertoireChoice{
                                                          .key = position.key(),
                                                          .fen = position.fen(),
                                                          .move = move,
                                                      });
                QTEST_ASSERT(set.has_value());
            }
        }
        chesscoach::app::test::drop_cards(
            QDir(data_dir).filePath(QStringLiteral("profiles.db")).toStdString());
        window = std::make_unique<MainWindow>(data_dir, QString::fromStdString(db.path()));
        window->show();
        child<QTabBar>(*window, QStringLiteral("screen_tabs")).setCurrentIndex(3);
    }

    [[nodiscard]] std::vector<chesscoach::Card> cards() const {
        auto profiles = ProfileStore::open(data_dir.toStdString());
        return profiles->cards(profiles->profiles().front().id, Color::white);
    }
};

QString text(const MainWindow& window, const QString& name) {
    return child<QLabel>(window, name).text();
}

void press(const MainWindow& window, const QString& name) {
    child<QAbstractButton>(window, name).click();
}

// The card's answer, and a legal move that is not it, from the board's position.
std::pair<std::string, std::string> answers(const BoardView& board) {
    if (board.position().key() == Position::start().key()) {
        return {"e2e4", "d2d4"};
    }
    return {"g1f3", "b1c3"};
}

void play(const BoardView& board, const std::string& uci) {
    click(board, uci.substr(0, 2));
    click(board, uci.substr(2, 2));
}

class DrillTest : public QObject {
    Q_OBJECT

    std::unique_ptr<FixtureDb> db_;

  private slots:
    void initTestCase() {
        db_ = std::make_unique<FixtureDb>();
        qputenv("CHESS_COACH_STOCKFISH", "/nonexistent/stockfish-for-the-drill-tests");
    }
    void cleanupTestCase() { db_.reset(); }

    void a_drill_grades_a_correct_and_a_wrong_move() {
        const Session session(*db_);
        MainWindow& window = *session.window;
        const auto& board = child<BoardView>(window, QStringLiteral("board"));

        // Entering the screen synced the two cards back, both due.
        QTRY_COMPARE(text(window, QStringLiteral("drill_due")),
                     QStringLiteral("2 due now, out of your 2 cards."));
        QCOMPARE(text(window, QStringLiteral("drill_progress")),
                 QStringLiteral("Your White repertoire has 2 cards, 2 due now. Your Black "
                                "repertoire has no cards."));
        QCOMPARE(session.cards().size(), std::size_t{2});
        QVERIFY(!board.isEnabled());

        press(window, QStringLiteral("drill_start"));
        QTRY_VERIFY(board.isEnabled());
        QVERIFY(
            text(window, QStringLiteral("drill_status")).startsWith(QStringLiteral("Your move ")));
        save_screenshot(window, QStringLiteral("drill"), QStringLiteral("card"));

        // The first card answered right: good, unless Hard or Easy; recorded at Next.
        const Position first = board.position();
        play(board, answers(board).first);
        QTRY_VERIFY(
            text(window, QStringLiteral("drill_verdict")).startsWith(QStringLiteral("Right.")));
        QVERIFY(child<QAbstractButton>(window, QStringLiteral("drill_easy")).isVisible());
        QVERIFY(!board.isEnabled());
        press(window, QStringLiteral("drill_next"));
        QTRY_COMPARE(text(window, QStringLiteral("drill_due")),
                     QStringLiteral("1 due now, out of your 2 cards."));
        for (const chesscoach::Card& card : session.cards()) {
            if (card.key == first.key()) {
                QCOMPARE(card.state.repetitions, 1);
                QCOMPARE(card.state.interval_days, 1); // due again a day after the review
            }
        }

        // The second answered wrong: again at once, and the board plays the card's move.
        QTRY_VERIFY(board.isEnabled());
        const auto [answer, wrong] = answers(board);
        const Position second = board.position();
        play(board, wrong);
        QTRY_VERIFY(
            text(window, QStringLiteral("drill_verdict")).startsWith(QStringLiteral("Not quite.")));
        QVERIFY(!child<QAbstractButton>(window, QStringLiteral("drill_easy")).isVisible());
        Position answered = second;
        static_cast<void>(answered.play_uci(answer));
        QTRY_COMPARE(board.position().fen(), answered.fen());
        QTRY_COMPARE(text(window, QStringLiteral("drill_due")),
                     QStringLiteral("1 due now, out of your 2 cards."));
        press(window, QStringLiteral("drill_next"));

        QTRY_VERIFY(child<QWidget>(window, QStringLiteral("drill_summary")).isVisible());
        QCOMPARE(text(window, QStringLiteral("drill_summary_text")),
                 QStringLiteral("The drill is over. You answered 2 cards, 1 right and 1 wrong. "
                                "The ones you missed come back soon."));
        QTRY_COMPARE(text(window, QStringLiteral("drill_due")),
                     QStringLiteral("None of your 2 cards is due now."));
        for (const chesscoach::Card& card : session.cards()) {
            if (card.key == second.key()) {
                QCOMPARE(card.state.repetitions, 0);
                QCOMPARE(card.state.interval_days, 1);
            }
        }
        QTRY_VERIFY(!board.animating());
        save_screenshot(window, QStringLiteral("drill"), QStringLiteral("summary"));
    }

    void easy_changes_a_correct_moves_grade() {
        const Session session(*db_);
        MainWindow& window = *session.window;
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        QTRY_COMPARE(text(window, QStringLiteral("drill_due")),
                     QStringLiteral("2 due now, out of your 2 cards."));
        press(window, QStringLiteral("drill_start"));
        QTRY_VERIFY(board.isEnabled());
        const Position first = board.position();
        play(board, answers(board).first);
        QTRY_VERIFY(child<QAbstractButton>(window, QStringLiteral("drill_easy")).isVisible());
        press(window, QStringLiteral("drill_easy"));
        QVERIFY(!child<QAbstractButton>(window, QStringLiteral("drill_easy")).isVisible());
        QTRY_VERIFY([&] {
            for (const chesscoach::Card& card : session.cards()) {
                if (card.key == first.key()) {
                    return card.state.ease_milli == chesscoach::initial_ease_milli + 100;
                }
            }
            return false;
        }());
    }
};

} // namespace

QTEST_MAIN(DrillTest)
#include "ui_drill_test.moc"

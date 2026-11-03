// UI tests of the engine worker and Explore's engine lines (app; ui), with core's fake engine set
// as the binary through tests/fake-stockfish.sh: lines after a request, with scores from white's
// side and the evaluation bar following the first line where nothing is stored; a request served
// from the engine cache; a long search cancelled by moving on while the window keeps processing
// events; a missing binary; the Settings dialog and its values surviving a new window; with
// screenshots (ui_harness.hpp, fixture_db.hpp).
#include <chesscoach/engine.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/profile_store.hpp>

#include "board_view.hpp"
#include "engine_worker.hpp"
#include "eval_bar.hpp"
#include "fixture_db.hpp"
#include "main_window.hpp"
#include "settings.hpp"
#include "ui_harness.hpp"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QObject>
#include <QSpinBox>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using chesscoach::Position;
using chesscoach::app::BoardView;
using chesscoach::app::EngineSettings;
using chesscoach::app::EvalBar;
using chesscoach::app::MainWindow;
using chesscoach::app::Settings;
using chesscoach::app::test::child;
using chesscoach::app::test::click;
using chesscoach::app::test::FixtureDb;
using chesscoach::app::test::save_screenshot;

namespace {

// The wait for an answer that includes the engine's start, which a loaded machine slows.
constexpr int first_wait_ms = 30000;

// The fake engine at the given seconds per search and line count.
EngineSettings fake(double seconds, int lines = 3) {
    return EngineSettings{
        .binary = QStringLiteral(CHESSCOACH_FAKE_STOCKFISH),
        .threads = 1,
        .hash_mb = 16,
        .seconds = seconds,
        .lines = lines,
    };
}

// A window on the fixture database with a data directory of its own, whose settings.ini names
// the engine before the window opens.
struct Session {
    QTemporaryDir root;
    QString data_dir = root.filePath(QStringLiteral("data"));
    QString log = root.filePath(QStringLiteral("engine.log"));
    std::unique_ptr<MainWindow> window;

    Session(const FixtureDb& db, const EngineSettings& engine) {
        Settings(data_dir).set_engine(engine);
        qputenv("CHESSCOACH_FAKE_UCI_LOG", log.toUtf8());
        window = std::make_unique<MainWindow>(data_dir, QString::fromStdString(db.path()));
        window->show();
    }

    [[nodiscard]] QStringList received() const {
        QFile file(log);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return {};
        }
        return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
};

QString text_of(const MainWindow& window, const QString& name) {
    return child<QLabel>(window, name).text();
}

int line_count(const MainWindow& window) {
    return child<QListWidget>(window, QStringLiteral("engine_lines")).count();
}

QString line_text(const MainWindow& window, int row) {
    return child<QListWidget>(window, QStringLiteral("engine_lines")).item(row)->text();
}

class EngineTest : public QObject {
    Q_OBJECT

    std::unique_ptr<FixtureDb> db_;

  private slots:
    void initTestCase() {
        db_ = std::make_unique<FixtureDb>();
        qputenv("CHESSCOACH_FAKE_UCI", CHESSCOACH_FAKE_UCI);
        qputenv("CHESSCOACH_FAKE_UCI_SCRIPT", "normal");
    }

    void cleanupTestCase() { db_.reset(); }

    void a_request_shows_the_engine_lines_from_whites_side() {
        const Session session(*db_, fake(0.1));
        MainWindow& window = *session.window;
        child<QComboBox>(window, QStringLiteral("tier_selector")).setCurrentIndex(3);
        // The start position: the fake's three lines, white to move, scores as given. The first
        // wait includes the engine's start, which a loaded machine slows.
        // Qt 6.4's expansion of this macro mixes / and + without parentheses.
        // NOLINTNEXTLINE(readability-math-missing-parentheses)
        QTRY_COMPARE_WITH_TIMEOUT(line_count(window), 3, first_wait_ms);
        QCOMPARE(line_text(window, 0), QStringLiteral("1: depth 3 +0.3 e4 e5"));
        QCOMPARE(line_text(window, 1), QStringLiteral("2: depth 3 +0.3 d4 e5"));
        QCOMPARE(line_text(window, 2), QStringLiteral("3: depth 3 +0.3 Nf3 e5"));
        QCOMPARE(text_of(window, QStringLiteral("engine_label")), QStringLiteral("Engine"));
        QCOMPARE(text_of(window, QStringLiteral("engine_status")),
                 QStringLiteral("Engine: FakeUCI 1.0"));
        // The stored evaluation feeds the bar where there is one.
        const auto& bar = child<EvalBar>(window, QStringLiteral("eval_bar"));
        QVERIFY(std::abs(bar.white_share().value_or(-1) - EvalBar::white_share_of_cp(19)) < 1e-9);
        QTRY_VERIFY(!child<BoardView>(window, QStringLiteral("board")).animating());
        save_screenshot(window, QStringLiteral("explore"), QStringLiteral("engine"));
        child<QAction>(window, QStringLiteral("theme_dark")).trigger();
        save_screenshot(window, QStringLiteral("explore"), QStringLiteral("engine-dark"));
        child<QAction>(window, QStringLiteral("theme_light")).trigger();

        // Black to move: the side-to-move score +0.29 is -0.29 from white's side.
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        QTRY_VERIFY(line_count(window) == 3 &&
                    line_text(window, 0).startsWith(QStringLiteral("1: depth 3 -0.3")));

        // Without a stored evaluation (1. Nf3 d5), the engine's first line feeds the bar.
        child<QAction>(window, QStringLiteral("new_game_action")).trigger();
        click(board, "g1");
        click(board, "f3");
        click(board, "d7");
        click(board, "d5");
        // Qt 6.4's expansion of this macro mixes / and + without parentheses.
        // NOLINTNEXTLINE(readability-math-missing-parentheses)
        QTRY_COMPARE_WITH_TIMEOUT(line_count(window), 3, first_wait_ms);
        QTRY_VERIFY(bar.white_share().has_value());
        QVERIFY(std::abs(bar.white_share().value_or(-1) - EvalBar::white_share_of_cp(29)) < 1e-9);
    }

    void a_cached_search_is_served_without_the_engine() {
        const QTemporaryDir root;
        const QString data_dir = root.filePath(QStringLiteral("data"));
        {
            auto profiles = chesscoach::ProfileStore::open(data_dir.toStdString());
            QVERIFY(profiles.has_value());
            chesscoach::SearchResult result;
            for (int k = 1; k <= 3; ++k) {
                result.lines.push_back({
                    .multipv = k,
                    .depth = 20,
                    .score_cp = 20 + k,
                    .score_mate = std::nullopt,
                    .pv = {"d2d4", "d7d5"},
                });
            }
            result.best_move = "d2d4";
            result.engine_name = "Seeded 1.0";
            QVERIFY(profiles->cache_store(Position::start().key(), 3, result).has_value());
        }
        const QString log = root.filePath(QStringLiteral("engine.log"));
        qputenv("CHESSCOACH_FAKE_UCI_LOG", log.toUtf8());
        Settings(data_dir).set_engine(fake(0.1));
        const MainWindow window(data_dir, QString::fromStdString(db_->path()));
        QTRY_COMPARE(text_of(window, QStringLiteral("engine_label")),
                     QStringLiteral("Engine (cached)"));
        QCOMPARE(line_count(window), 3);
        QCOMPARE(line_text(window, 0), QStringLiteral("1: depth 20 +0.2 d4 d5"));
        // The engine never started.
        QVERIFY(!QFile::exists(log));
        QCOMPARE(text_of(window, QStringLiteral("engine_status")),
                 QStringLiteral("Engine: not started"));
    }

    void a_long_search_is_cancelled_by_moving_on() {
        auto session = std::make_unique<Session>(*db_, fake(30));
        MainWindow& window = *session->window;
        // Qt 6.4's expansion of this macro mixes / and + without parentheses.
        // NOLINTNEXTLINE(readability-math-missing-parentheses)
        QTRY_COMPARE_WITH_TIMEOUT(text_of(window, QStringLiteral("engine_label")),
                                  QStringLiteral("Engine: searching"), first_wait_ms);
        QCOMPARE(text_of(window, QStringLiteral("engine_status")),
                 QStringLiteral("Engine: searching"));

        // The window keeps processing events while the engine thinks.
        int ticks = 0;
        QTimer timer;
        connect(&timer, &QTimer::timeout, this, [&ticks] { ++ticks; });
        timer.start(20);
        // Qt 6.4's expansion of this macro mixes / and + without parentheses (Qt 6.12's does not).
        QTRY_VERIFY_WITH_TIMEOUT(ticks >= 3, 200); // NOLINT(readability-math-missing-parentheses)

        // Moving on stops the start position's search and starts the next one at once, the
        // window ticking all the while: the count starts again here.
        ticks = 0;
        const auto& board = child<BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        QCOMPARE(window.line(), (std::vector<std::string>{"e2e4"}));
        QTRY_VERIFY(session->received().count(QStringLiteral("stop")) >= 1);
        QTRY_COMPARE(session->received().filter(QStringLiteral("go movetime")).size(), 2);
        QCOMPARE(line_count(window), 0); // the stopped search's lines never show
        QTRY_COMPARE(text_of(window, QStringLiteral("engine_label")),
                     QStringLiteral("Engine: searching"));
        QTRY_VERIFY(ticks >= 3); // NOLINT(readability-math-missing-parentheses)

        // Closing the window stops the second search too, without waiting for its 30 s.
        QElapsedTimer closing;
        closing.start();
        session->window.reset();
        QVERIFY(closing.elapsed() < 3000);
        QVERIFY(session->received().count(QStringLiteral("quit")) >= 1);
    }

    void a_missing_binary_shows_not_found() {
        EngineSettings missing = fake(0.1);
        missing.binary = QStringLiteral("/nonexistent/stockfish");
        const Session session(*db_, missing);
        const MainWindow& window = *session.window;
        QTRY_COMPARE(text_of(window, QStringLiteral("engine_status")),
                     QStringLiteral("Engine: not found"));
        QCOMPARE(text_of(window, QStringLiteral("engine_label")),
                 QStringLiteral("Engine: not found"));
        QCOMPARE(line_count(window), 0);
        // Nothing was searched, so nothing was cached: settings.ini is all there is.
        QCOMPARE(QDir(session.data_dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
                 QStringList{QStringLiteral("settings.ini")});
    }

    void the_settings_dialog_saves_and_applies_the_engine_settings() {
        const Session session(*db_, fake(0.1));
        MainWindow& window = *session.window;
        // Qt 6.4's expansion of this macro mixes / and + without parentheses.
        // NOLINTNEXTLINE(readability-math-missing-parentheses)
        QTRY_COMPARE_WITH_TIMEOUT(line_count(window), 3, first_wait_ms);
        child<QAction>(window, QStringLiteral("settings_action")).trigger();
        auto& dialog = child<QDialog>(window, QStringLiteral("settings_dialog"));
        QVERIFY(dialog.isVisible());
        QCOMPARE(child<QLineEdit>(dialog, QStringLiteral("engine_binary")).text(),
                 QStringLiteral(CHESSCOACH_FAKE_STOCKFISH));
        child<QSpinBox>(dialog, QStringLiteral("engine_hash")).setValue(32);
        child<QDoubleSpinBox>(dialog, QStringLiteral("engine_seconds")).setValue(0.2);
        child<QSpinBox>(dialog, QStringLiteral("engine_lines_setting")).setValue(2);
        save_screenshot(dialog, QStringLiteral("settings"), QStringLiteral("dialog"));
        dialog.accept();

        // Applied: the next search has two lines.
        QTRY_COMPARE(line_count(window), 2);
        const EngineSettings saved = Settings(session.data_dir).engine();
        QCOMPARE(saved.hash_mb, 32);
        QCOMPARE(saved.seconds, 0.2);
        QCOMPARE(saved.lines, 2);
        QCOMPARE(saved.binary, QStringLiteral(CHESSCOACH_FAKE_STOCKFISH));

        // A new window on the same data directory starts with them.
        const MainWindow again(session.data_dir, QString());
        child<QAction>(again, QStringLiteral("settings_action")).trigger();
        const auto& reopened = child<QDialog>(again, QStringLiteral("settings_dialog"));
        QCOMPARE(child<QSpinBox>(reopened, QStringLiteral("engine_lines_setting")).value(), 2);
        QCOMPARE(child<QSpinBox>(reopened, QStringLiteral("engine_hash")).value(), 32);
        QCOMPARE(child<QDoubleSpinBox>(reopened, QStringLiteral("engine_seconds")).value(), 0.2);
    }
};

} // namespace

QTEST_MAIN(EngineTest)
#include "ui_engine_test.moc"

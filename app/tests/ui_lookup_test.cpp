// UI tests of where the app finds the opening database and the engine (app; ui): the database
// --db names, else the data directory's openings.db, else the app bundle's, the status bar saying
// which, and the message naming the three places when there is none; and the engine found in the
// bundle's resources when nothing else names one. The bundle is a
// scratch folder named by CHESS_COACH_BUNDLE_RESOURCES, the app's test hook (ui_harness.hpp,
// fixture_db.hpp).
#include "fixture_db.hpp"
#include "main_window.hpp"
#include "settings.hpp"
#include "ui_harness.hpp"

#include <QAction>
#include <QByteArray>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>
#include <filesystem>
#include <memory>

using chesscoach::app::MainWindow;
using chesscoach::app::test::child;
using chesscoach::app::test::save_screenshot;
using chesscoach::app::test::write_fixture_db;

namespace {

// A scratch root with a data directory and a bundle's resources folder, which the test hook
// names.
struct Places {
    QTemporaryDir root;
    QString data_dir = root.filePath(QStringLiteral("data"));
    QString resources = root.filePath(QStringLiteral("bundle"));

    Places() {
        QDir().mkpath(data_dir);
        QDir().mkpath(resources);
        qputenv("CHESS_COACH_BUNDLE_RESOURCES", resources.toUtf8());
    }
    Places(const Places&) = delete;
    Places& operator=(const Places&) = delete;
    Places(Places&&) = delete;
    Places& operator=(Places&&) = delete;
    ~Places() { qunsetenv("CHESS_COACH_BUNDLE_RESOURCES"); }

    [[nodiscard]] QString in_data() const {
        return QDir(data_dir).filePath(QStringLiteral("openings.db"));
    }
    [[nodiscard]] QString in_bundle() const {
        return QDir(resources).filePath(QStringLiteral("openings.db"));
    }
};

void write(const QString& path) { write_fixture_db(std::filesystem::path(path.toStdString())); }

QString text(const MainWindow& window, const QString& name) {
    return child<QLabel>(window, name).text();
}

bool opened(const MainWindow& window) {
    return !child<QLabel>(window, QStringLiteral("data_version_status")).isHidden();
}

class LookupTest : public QObject {
    Q_OBJECT

    QByteArray path_;                // the PATH the engine test takes stockfish off
    std::unique_ptr<Places> places_; // each test's own

  private slots:
    void initTestCase() {
        path_ = qgetenv("PATH");
        // The engine stays out but in the one test that puts it in the bundle.
        qputenv("CHESS_COACH_STOCKFISH", "/nonexistent/stockfish-for-the-lookup-tests");
    }

    void init() { places_ = std::make_unique<Places>(); }
    void cleanup() {
        places_.reset();
        qputenv("PATH", path_);
        qputenv("CHESS_COACH_STOCKFISH", "/nonexistent/stockfish-for-the-lookup-tests");
    }

    void the_database_is_looked_for_in_order() {
        const Places& places = *places_;
        const QString explicit_db = places.root.filePath(QStringLiteral("explicit.db"));
        write(explicit_db);
        write(places.in_data());
        write(places.in_bundle());
        {
            const MainWindow window(places.data_dir, explicit_db);
            QTRY_VERIFY(opened(window));
            QCOMPARE(text(window, QStringLiteral("database_status")),
                     QStringLiteral("explicit.db (--db)"));
        }
        {
            // A --db that names no file is reported, not passed over for the data directory's.
            const QString none = places.root.filePath(QStringLiteral("none.db"));
            const MainWindow window(places.data_dir, none);
            QTRY_VERIFY(window.findChild<QWidget*>(QStringLiteral("database_error")) != nullptr);
            QVERIFY(window.findChild<QWidget*>(QStringLiteral("database_error"))
                        ->property("text")
                        .toString()
                        .contains(none));
            QCOMPARE(text(window, QStringLiteral("database_status")),
                     QStringLiteral("No database"));
        }
        QVERIFY(QFile::remove(places.in_data()));
        {
            const MainWindow window(places.data_dir, QString());
            QTRY_VERIFY(opened(window));
            QCOMPARE(text(window, QStringLiteral("database_status")),
                     QStringLiteral("openings.db (app bundle)"));
        }
        QVERIFY(QFile::remove(places.in_bundle()));
        {
            MainWindow window(places.data_dir, QString());
            window.show();
            QCOMPARE(text(window, QStringLiteral("database_status")),
                     QStringLiteral("No database"));
            const QString message = text(window, QStringLiteral("explore_message"));
            QVERIFY2(message.contains(QStringLiteral("--db")) &&
                         message.contains(QDir::toNativeSeparators(places.data_dir)) &&
                         message.contains(QStringLiteral("app bundle")) &&
                         message.contains(QStringLiteral("File > Import database")),
                     qPrintable(message));
            save_screenshot(window, QStringLiteral("lookup"), QStringLiteral("no-database"));
        }
    }

    void the_engine_is_found_in_the_bundle() {
        const Places& places = *places_;
        QDir(places.resources).mkpath(QStringLiteral("engine"));
        const QString engine = QDir(places.resources).filePath(QStringLiteral("engine/stockfish"));
        QVERIFY(QFile::copy(QStringLiteral(CHESSCOACH_FAKE_STOCKFISH), engine));
        QVERIFY(QFile::setPermissions(engine, QFile::permissions(engine) | QFile::ExeOwner));
        // Nothing else names an engine: no setting, no CHESS_COACH_STOCKFISH, no stockfish on the
        // PATH (cleanup puts both back).
        qunsetenv("CHESS_COACH_STOCKFISH");
        qputenv("PATH", "/usr/bin:/bin");
        qputenv("CHESSCOACH_FAKE_UCI", CHESSCOACH_FAKE_UCI);
        qputenv("CHESSCOACH_FAKE_UCI_SCRIPT", "normal");
        {
            const MainWindow window(places.data_dir, QString());
            QTRY_COMPARE_WITH_TIMEOUT(text(window, QStringLiteral("engine_status")),
                                      QStringLiteral("Engine: FakeUCI 1.0"), 30000);
            child<QAction>(window, QStringLiteral("settings_action")).trigger();
            auto& dialog = child<QDialog>(window, QStringLiteral("settings_dialog"));
            QCOMPARE(child<QLineEdit>(dialog, QStringLiteral("engine_binary")).placeholderText(),
                     QStringLiteral("Default: %1").arg(engine));
            dialog.reject();
        }
    }
};

} // namespace

QTEST_MAIN(LookupTest)
#include "ui_lookup_test.moc"

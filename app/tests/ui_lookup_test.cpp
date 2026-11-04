// UI tests of where the app finds the opening database and the engine (app; ui): the database
// --db names, else the data directory's openings.db, else the app bundle's, the status bar saying
// which, and the message naming the three places when there is none; File > Import database…
// through the file chooser, the data version changing, openings.db.previous left behind and
// Explore reading the new file, and a file that is not a database refused with the old one kept;
// and the engine found in the bundle's resources when nothing else names one. The bundle is a
// scratch folder named by CHESS_COACH_BUNDLE_RESOURCES, the app's test hook (ui_harness.hpp,
// fixture_db.hpp).
#include "board_view.hpp"
#include "explore_model.hpp"
#include "file_chooser.hpp"
#include "fixture_db.hpp"
#include "main_window.hpp"
#include "settings.hpp"
#include "ui_harness.hpp"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QSettings>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <filesystem>
#include <memory>

using chesscoach::app::FileChooser;
using chesscoach::app::MainWindow;
using chesscoach::app::test::child;
using chesscoach::app::test::click;
using chesscoach::app::test::save_screenshot;
using chesscoach::app::test::set_data_version;
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
            QVERIFY(text(window, QStringLiteral("explore_message"))
                        .startsWith(QStringLiteral("There is no opening database at")));
        }
        {
            // An import that fails there reports the failure alone, with no second box for the
            // missing file, which the worker does not try to open again.
            const QString none = places.root.filePath(QStringLiteral("none.db"));
            MainWindow window(places.data_dir, none);
            QTRY_VERIFY(window.findChild<QWidget*>(QStringLiteral("database_error")) != nullptr);
            window.findChild<QWidget*>(QStringLiteral("database_error"))->close();
            QTRY_VERIFY(window.findChild<QWidget*>(QStringLiteral("database_error")) == nullptr);
            const QString junk = places.root.filePath(QStringLiteral("junk.db"));
            {
                QFile file(junk);
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write("not a database");
            }
            window.import_database(junk);
            QTRY_VERIFY(window.findChild<QWidget*>(QStringLiteral("database_error")) != nullptr);
            QTest::qWait(300);
            QCOMPARE(window.findChildren<QWidget*>(QStringLiteral("database_error")).size(), 1);
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
                         message.contains(QStringLiteral("inside the app")) &&
                         message.contains(QStringLiteral("File > Import database")),
                     qPrintable(message));
            save_screenshot(window, QStringLiteral("lookup"), QStringLiteral("no-database"));
        }
    }

    void import_database_puts_a_new_one_in_place() {
        const Places& places = *places_;
        write(places.in_data());
        const QString source = places.root.filePath(QStringLiteral("release.db"));
        write(source);
        set_data_version(source.toStdString(), 7);
        {
            QSettings ini(QDir(places.data_dir).filePath(QStringLiteral("settings.ini")),
                          QSettings::IniFormat);
            ini.setValue(QStringLiteral("tier"), 3);
        }
        MainWindow window(places.data_dir, QString());
        window.show();
        QTRY_COMPARE(text(window, QStringLiteral("data_version_status")),
                     QStringLiteral("Data version 0"));
        const auto& table = child<QTableView>(window, QStringLiteral("explore_table"));
        QTRY_COMPARE(table.model()->rowCount(), 4);
        const auto& board = child<chesscoach::app::BoardView>(window, QStringLiteral("board"));
        click(board, "e2");
        click(board, "e4");
        QTRY_COMPARE(table.model()->rowCount(), 3);

        // File > Import database… opens the chooser, which the timer answers with the file.
        QTimer::singleShot(0, this, [&source] {
            auto* chooser = qobject_cast<FileChooser*>(QApplication::activeModalWidget());
            QTEST_ASSERT(chooser != nullptr);
            chooser->go_to(source);
            child<QPushButton>(*chooser, QStringLiteral("file_chooser_accept")).click();
        });
        child<QAction>(window, QStringLiteral("import_database_action")).trigger();
        // The busy dialog shows until the worker answers, and Escape does not close it.
        auto* busy = window.findChild<QWidget*>(QStringLiteral("database_import_progress"));
        QVERIFY(busy != nullptr && busy->isVisible());
        QTest::keyClick(busy, Qt::Key_Escape);
        QVERIFY(busy->isVisible());
        QTRY_VERIFY(window.findChild<QMessageBox*>(QStringLiteral("database_imported")) != nullptr);
        auto* done = window.findChild<QMessageBox*>(QStringLiteral("database_imported"));
        QCOMPARE(done->text(), QStringLiteral("The new opening database is ready (data version 7). "
                                              "It holds games from January 2013."));
        QTRY_COMPARE(text(window, QStringLiteral("data_version_status")),
                     QStringLiteral("Data version 7"));
        QCOMPARE(text(window, QStringLiteral("database_status")),
                 QStringLiteral("openings.db (data directory)"));
        QVERIFY(QFileInfo::exists(
            QDir(places.data_dir).filePath(QStringLiteral("openings.db.previous"))));
        // The import is over: the busy dialog is gone, and both imports are on again.
        QTRY_VERIFY(window.findChild<QWidget*>(QStringLiteral("database_import_progress")) ==
                    nullptr);
        QVERIFY(child<QAction>(window, QStringLiteral("import_database_action")).isEnabled());
        QVERIFY(child<QAction>(window, QStringLiteral("import_action")).isEnabled());
        // Explore reads the new file at the same position.
        QTRY_COMPARE(table.model()->rowCount(), 3);
        save_screenshot(window, QStringLiteral("lookup"), QStringLiteral("imported"));
        done->close();

        // A file that is not an opening database is refused, and the database stays open.
        const QString junk = places.root.filePath(QStringLiteral("junk.db"));
        {
            QFile file(junk);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("not a database");
        }
        window.import_database(junk);
        QTRY_VERIFY(window.findChild<QMessageBox*>(QStringLiteral("database_error")) != nullptr);
        QVERIFY(window.findChild<QMessageBox*>(QStringLiteral("database_error"))
                    ->text()
                    .startsWith(QStringLiteral("junk.db was not imported")));
        QTRY_COMPARE(text(window, QStringLiteral("data_version_status")),
                     QStringLiteral("Data version 7"));
        QTRY_COMPARE(table.model()->rowCount(), 3);
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
            const auto status = QStringLiteral("engine_status");
            const auto expected = QStringLiteral("Engine: FakeUCI 1.0");
            // NOLINTNEXTLINE(readability-math-missing-parentheses)
            QTRY_COMPARE_WITH_TIMEOUT(text(window, status), expected, 30000);
            child<QAction>(window, QStringLiteral("settings_action")).trigger();
            auto& dialog = child<QDialog>(window, QStringLiteral("settings_dialog"));
            QCOMPARE(child<QLineEdit>(dialog, QStringLiteral("engine_binary")).placeholderText(),
                     QStringLiteral("Leave empty to use %1").arg(engine));
            dialog.reject();
        }
    }
};

} // namespace

QTEST_MAIN(LookupTest)
#include "ui_lookup_test.moc"

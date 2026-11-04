// UI tests of the app's file chooser (app; ui): a file picked through its model in open mode, the
// name filter hiding the other files, a folder refused as a file; save mode refusing a file that
// exists and taking a new name; Return in the path line going to a folder, a file or ~ without
// accepting; save mode reading a full path typed in the path line, taking the name the user
// edited last, and refusing a name with a slash; and Browse in the Settings dialog opening it at
// the folder last used and keeping the new one in settings.ini. The platform's settings folder
// gains no file while the tests run. A screenshot of the chooser (ui_harness.hpp).
#include "file_chooser.hpp"
#include "main_window.hpp"
#include "settings.hpp"
#include "ui_harness.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <memory>

using chesscoach::app::FileChooser;
using chesscoach::app::MainWindow;
using chesscoach::app::Settings;
using chesscoach::app::test::child;
using chesscoach::app::test::save_screenshot;

namespace {

// The entries of the platform's settings folder (~/Library/Preferences on macOS, ~/.config on
// Linux), but the system's own com.apple ones, which macOS adds as it pleases.
QStringList preferences() {
    const QDir folder(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    QStringList names;
    for (const QString& name :
         folder.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
        if (!name.startsWith(QStringLiteral("com.apple."))) {
            names << name;
        }
    }
    return names;
}

void touch(const QString& path) {
    QFile file(path);
    QTEST_ASSERT(file.open(QIODevice::WriteOnly));
}

QString canonical(const QString& path) { return QFileInfo(path).canonicalFilePath(); }

class FileChooserTest : public QObject {
    Q_OBJECT

    QStringList preferences_before_;
    std::unique_ptr<QTemporaryDir> folder_; // each test's own

  private slots:
    void initTestCase() {
        preferences_before_ = preferences();
        // The window test keeps the engine out: the binary named here does not exist.
        qputenv("CHESS_COACH_STOCKFISH", "/nonexistent/stockfish-for-the-chooser-tests");
    }

    void cleanupTestCase() { QCOMPARE(preferences(), preferences_before_); }

    void init() { folder_ = std::make_unique<QTemporaryDir>(); }
    void cleanup() { folder_.reset(); }

    void open_mode_picks_a_file_through_the_model() {
        const QTemporaryDir& folder = *folder_;
        touch(folder.filePath(QStringLiteral("lines.pgn")));
        touch(folder.filePath(QStringLiteral("notes.txt")));
        FileChooser chooser(FileChooser::Mode::open, QStringLiteral("Pick"), folder.path(),
                            {QStringLiteral("*.pgn")});
        chooser.show();
        auto& tree = child<QTreeView>(chooser, QStringLiteral("file_chooser_tree"));
        const auto* model = qobject_cast<const QFileSystemModel*>(tree.model());
        QVERIFY(model != nullptr);
        QCOMPARE(canonical(chooser.directory()), canonical(folder.path()));
        // The filter leaves the PGN file alone in the folder.
        const QModelIndex listing = model->index(folder.path());
        // The model reads the folder on a thread of its own, which a loaded machine slows. Qt 6.4's
        // expansion of this macro mixes / and + without parentheses.
        // NOLINTNEXTLINE(readability-math-missing-parentheses)
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(listing), 1, 30000);
        QCOMPARE(model->fileName(model->index(0, 0, listing)), QStringLiteral("lines.pgn"));

        // A folder is not a file to open.
        child<QPushButton>(chooser, QStringLiteral("file_chooser_accept")).click();
        QVERIFY(chooser.isVisible());
        QCOMPARE(child<QLabel>(chooser, QStringLiteral("file_chooser_message")).text(),
                 QStringLiteral("Choose a file in the list, then press Open."));

        tree.setCurrentIndex(model->index(0, 0, listing));
        save_screenshot(chooser, QStringLiteral("file-chooser"), QStringLiteral("open"));
        QCOMPARE(canonical(child<QLineEdit>(chooser, QStringLiteral("file_chooser_path")).text()),
                 canonical(folder.filePath(QStringLiteral("lines.pgn"))));
        child<QPushButton>(chooser, QStringLiteral("file_chooser_accept")).click();
        QCOMPARE(chooser.result(), int{QDialog::Accepted});
        QCOMPARE(canonical(chooser.file()),
                 canonical(folder.filePath(QStringLiteral("lines.pgn"))));
    }

    void save_mode_refuses_a_file_that_exists() {
        const QTemporaryDir& folder = *folder_;
        touch(folder.filePath(QStringLiteral("profile.db")));
        FileChooser chooser(FileChooser::Mode::save, QStringLiteral("Save"), folder.path(),
                            {QStringLiteral("*.db")}, QStringLiteral("profile.db"));
        chooser.show();
        child<QPushButton>(chooser, QStringLiteral("file_chooser_accept")).click();
        QVERIFY(chooser.isVisible());
        QCOMPARE(child<QLabel>(chooser, QStringLiteral("file_chooser_message")).text(),
                 QStringLiteral("profile.db exists already. Type another name."));
        child<QLineEdit>(chooser, QStringLiteral("file_chooser_name"))
            .setText(QStringLiteral("other.db"));
        child<QPushButton>(chooser, QStringLiteral("file_chooser_accept")).click();
        QCOMPARE(chooser.result(), int{QDialog::Accepted});
        QCOMPARE(QFileInfo(chooser.file()).fileName(), QStringLiteral("other.db"));
        QCOMPARE(canonical(QFileInfo(chooser.file()).absolutePath()), canonical(folder.path()));
        QVERIFY(!QFileInfo::exists(chooser.file())); // choosing writes nothing
    }

    void return_in_the_path_line_goes_there_and_never_accepts() {
        const QTemporaryDir& folder = *folder_;
        QDir(folder.path()).mkpath(QStringLiteral("inner"));
        const QString inner = QDir(folder.path()).filePath(QStringLiteral("inner"));
        FileChooser chooser(FileChooser::Mode::save, QStringLiteral("Save"), folder.path(),
                            {QStringLiteral("*.db")}, QStringLiteral("profile.db"));
        chooser.show();
        auto& path = child<QLineEdit>(chooser, QStringLiteral("file_chooser_path"));
        const auto& name = child<QLineEdit>(chooser, QStringLiteral("file_chooser_name"));
        // A folder typed and Return: the chooser goes there and stays open.
        path.setText(inner);
        QTest::keyClick(&path, Qt::Key_Return);
        QVERIFY(chooser.isVisible());
        QCOMPARE(chooser.result(), int{QDialog::Rejected});
        QCOMPARE(canonical(chooser.directory()), canonical(inner));
        // A file typed and Return: its folder, and its name in the name field.
        path.setText(QDir(folder.path()).filePath(QStringLiteral("mine.db")));
        QTest::keyClick(&path, Qt::Key_Enter);
        QVERIFY(chooser.isVisible());
        QCOMPARE(canonical(chooser.directory()), canonical(folder.path()));
        QCOMPARE(name.text(), QStringLiteral("mine.db"));
        // ~ is the home folder.
        path.setText(QStringLiteral("~"));
        QTest::keyClick(&path, Qt::Key_Return);
        QCOMPARE(canonical(chooser.directory()), canonical(QDir::homePath()));
        QVERIFY(chooser.isVisible());
    }

    void save_mode_reads_a_path_typed_without_return() {
        const QTemporaryDir& folder = *folder_;
        QDir(folder.path()).mkpath(QStringLiteral("inner"));
        FileChooser chooser(FileChooser::Mode::save, QStringLiteral("Save"), folder.path(),
                            {QStringLiteral("*.db")}, QStringLiteral("profile.db"));
        chooser.show();
        auto& path = child<QLineEdit>(chooser, QStringLiteral("file_chooser_path"));
        auto& name = child<QLineEdit>(chooser, QStringLiteral("file_chooser_name"));
        // A name with a slash is refused.
        name.setText(QStringLiteral("inner/mine.db"));
        child<QPushButton>(chooser, QStringLiteral("file_chooser_accept")).click();
        QVERIFY(chooser.isVisible());
        QVERIFY(child<QLabel>(chooser, QStringLiteral("file_chooser_message"))
                    .text()
                    .startsWith(QStringLiteral("A file name cannot contain a slash.")));
        // A full path in the path line gives the folder and the name.
        const QString typed = QDir(folder.path()).filePath(QStringLiteral("inner/mine.db"));
        path.setText(typed);
        child<QPushButton>(chooser, QStringLiteral("file_chooser_accept")).click();
        QCOMPARE(chooser.result(), int{QDialog::Accepted});
        QCOMPARE(QFileInfo(chooser.file()).fileName(), QStringLiteral("mine.db"));
        QCOMPARE(canonical(QFileInfo(chooser.file()).absolutePath()),
                 canonical(QDir(folder.path()).filePath(QStringLiteral("inner"))));
    }

    void save_mode_takes_the_name_edited_last() {
        const QTemporaryDir& folder = *folder_;
        FileChooser chooser(FileChooser::Mode::save, QStringLiteral("Save"), folder.path(),
                            {QStringLiteral("*.db")}, QStringLiteral("profile.db"));
        chooser.show();
        auto& path = child<QLineEdit>(chooser, QStringLiteral("file_chooser_path"));
        auto& name = child<QLineEdit>(chooser, QStringLiteral("file_chooser_name"));
        // A file path typed, then the name field edited: the name field's name, in the typed
        // path's folder.
        path.clear();
        QTest::keyClicks(&path, QDir(folder.path()).filePath(QStringLiteral("typed.db")));
        name.clear();
        QTest::keyClicks(&name, QStringLiteral("named.db"));
        child<QPushButton>(chooser, QStringLiteral("file_chooser_accept")).click();
        QCOMPARE(chooser.result(), int{QDialog::Accepted});
        QCOMPARE(QFileInfo(chooser.file()).fileName(), QStringLiteral("named.db"));
        QCOMPARE(canonical(QFileInfo(chooser.file()).absolutePath()), canonical(folder.path()));
    }

    void browse_starts_at_the_last_folder_and_keeps_the_new_one() {
        const QTemporaryDir root;
        const QString data_dir = root.filePath(QStringLiteral("data"));
        const QString engines = root.filePath(QStringLiteral("engines"));
        QDir().mkpath(engines);
        touch(QDir(engines).filePath(QStringLiteral("stockfish")));
        Settings(data_dir).set_last_dir(engines);
        const MainWindow window(data_dir, QString());
        child<QAction>(window, QStringLiteral("settings_action")).trigger();
        auto& dialog = child<QDialog>(window, QStringLiteral("settings_dialog"));
        // Browse opens the chooser at the folder last used; the timer picks the file in it.
        QString started_in;
        QTimer::singleShot(0, this, [&started_in, &engines] {
            auto* chooser = qobject_cast<FileChooser*>(QApplication::activeModalWidget());
            QTEST_ASSERT(chooser != nullptr);
            started_in = chooser->directory();
            chooser->go_to(QDir(engines).filePath(QStringLiteral("stockfish")));
            child<QPushButton>(*chooser, QStringLiteral("file_chooser_accept")).click();
        });
        child<QPushButton>(dialog, QStringLiteral("engine_browse")).click();
        QCOMPARE(canonical(started_in), canonical(engines));
        QCOMPARE(canonical(child<QLineEdit>(dialog, QStringLiteral("engine_binary")).text()),
                 canonical(QDir(engines).filePath(QStringLiteral("stockfish"))));
        QCOMPARE(canonical(Settings(data_dir).last_dir()), canonical(engines));
        dialog.reject();
    }
};

} // namespace

QTEST_MAIN(FileChooserTest)
#include "ui_file_chooser_test.moc"

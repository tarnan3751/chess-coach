// UI test of the main window under the offscreen platform (app; ui): the title carries the
// version, the central area says no database is loaded, and the status bar names the data
// directory it was given.
#include "main_window.hpp"

#include <chesscoach/version.hpp>

#include <QDir>
#include <QLabel>
#include <QObject>
#include <QStatusBar>
#include <QString>
#include <QTest>

namespace {

class MainWindowTest : public QObject {
    Q_OBJECT

    // The --data-dir each window is given; nothing is created there.
    const QString data_dir_ = QStringLiteral("/scratch/chess-coach-data");

  private slots:
    void title_names_the_version() const;
    void central_label_says_no_database() const;
    void status_bar_names_the_data_directory() const;
};

void MainWindowTest::title_names_the_version() const {
    const chesscoach::MainWindow window(data_dir_, QString());
    QCOMPARE(window.windowTitle(),
             QStringLiteral("chess-coach ") +
                 QString::fromStdString(chesscoach::to_string(chesscoach::current_version())));
}

void MainWindowTest::central_label_says_no_database() const {
    const chesscoach::MainWindow window(data_dir_, QString());
    const auto* label = qobject_cast<const QLabel*>(window.centralWidget());
    QVERIFY(label != nullptr);
    QCOMPARE(label->text(), QStringLiteral("No database loaded"));
}

void MainWindowTest::status_bar_names_the_data_directory() const {
    const chesscoach::MainWindow window(data_dir_, QString());
    QCOMPARE(window.statusBar()->currentMessage(),
             QStringLiteral("Data directory: ") + QDir::toNativeSeparators(data_dir_));
}

} // namespace

QTEST_MAIN(MainWindowTest)
#include "main_window_test.moc"

// The main window (main_window.hpp).
#include "main_window.hpp"

#include <chesscoach/version.hpp>

#include <QDir>
#include <QLabel>
#include <QStatusBar>
#include <Qt>

namespace chesscoach {

MainWindow::MainWindow(const QString& data_dir, const QString& /*db*/, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(tr("chess-coach %1").arg(QString::fromStdString(to_string(current_version()))));

    auto* placeholder = new QLabel(tr("No database loaded"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);

    statusBar()->showMessage(tr("Data directory: %1").arg(QDir::toNativeSeparators(data_dir)));
}

} // namespace chesscoach

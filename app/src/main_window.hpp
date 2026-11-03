// The main window: titled with the version, an empty central area until a database is loaded,
// and the data directory in the status bar. Touched only on the GUI thread; reads and writes
// nothing (CLAUDE.md, **app/ is presentation**).
#pragma once

#include <QMainWindow>
#include <QString>

namespace chesscoach {

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    // data_dir is the directory the profile database will live in (--data-dir, or the
    // platform's application data location); db is the opening database file (--db), empty
    // when none was given. Neither is opened yet.
    MainWindow(const QString& data_dir, const QString& db, QWidget* parent = nullptr);
};

} // namespace chesscoach

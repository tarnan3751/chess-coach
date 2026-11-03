// The main window's frame (docs/reference/app.md, **Shell**): a toolbar with New game, Flip
// board and the tier selector, the board on the left with the move list below it, the screen
// switcher on the right over the four screens (placeholders until each screen lands), and the
// database and the engine in the status bar. Touched only on the GUI thread; reads and writes
// nothing yet (CLAUDE.md, **app/ is presentation**).
#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

class QComboBox;
class QListWidget;

namespace chesscoach::app {

class BoardView;

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    // The tier the selector shows at start.
    static constexpr int default_tier = 2;

    // data_dir is the directory the profile database will live in (--data-dir, or the
    // platform's application data location); db is the opening database file (--db), empty
    // when none was given. Neither is opened yet.
    MainWindow(const QString& data_dir, const QString& db, QWidget* parent = nullptr);

  private:
    void new_game();
    void add_move(const QString& san);

    BoardView* board_;
    QListWidget* move_list_;
    QComboBox* tier_selector_;
    QStringList moves_; // the SAN of every move since the start position
};

} // namespace chesscoach::app

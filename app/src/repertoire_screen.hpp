// The Repertoire screen (docs/reference/app.md, **Repertoire**): the colour whose repertoire is
// shown; Import PGN; the coverage at the selected tier to the settings' depth; the repertoire as a
// tree of lines from the start, a click on a move going there on the board; the current position,
// with its choice and Remove choice; the recommendation score of its moves when the colour is to
// move, with Choose; and the gaps by how often they will be met, with Go to. It shows what the
// window passes on from the profile worker and asks for everything through its signals. GUI
// thread only.
#pragma once

#include "profile_data.hpp"

#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QRadioButton;
class QStandardItemModel;
class QTableWidget;
class QTreeView;

namespace chesscoach::app {

class RepertoireScreen : public QWidget {
    Q_OBJECT

  public:
    explicit RepertoireScreen(QWidget* parent = nullptr);

    // Whether the white repertoire is shown.
    [[nodiscard]] bool user_white() const;

    void show_repertoire(const RepertoireData& repertoire);
    // tier names the tier the coverage was computed at.
    void show_coverage(const CoverageData& coverage, const QString& tier);
    void show_coverage_message(const QString& text);
    // The current position: whether the repertoire's colour is to move there, and its choice in
    // SAN, if any.
    void show_position(bool colour_to_move, const QString& choice_san);
    // The recommendation score of the current position's moves; empty clears it.
    void show_scores(const ScoreData& scores);
    void show_message(const QString& text);
    // Asks for the import of the PGN file at path, once the window's file chooser has given it.
    void import_pgn(const QString& path);

  signals:
    void colour_changed(bool white);
    // Import PGN… was pressed: the window asks for the file.
    void import_clicked();
    void import_requested(const QString& path);
    // The board to the position at the end of the line, UCI from the start.
    void navigate_requested(const QStringList& line);
    void choose_requested(const QString& uci);
    void remove_requested();

  private:
    QRadioButton* white_;
    QRadioButton* black_;
    QPushButton* import_;
    QLabel* message_;
    QLabel* coverage_;
    QTreeView* tree_;
    QStandardItemModel* model_;
    QLabel* position_;
    QPushButton* remove_;
    QTableWidget* scores_;
    QPushButton* choose_;
    QTableWidget* gaps_;
    QPushButton* goto_;
    QVector<QStringList> gap_lines_; // the gaps' lines, by row
};

} // namespace chesscoach::app

// The Drill screen (docs/reference/app.md, **Drill**): the colour whose cards are drilled; the
// cards due of the cards there are, and both repertoires' progress; Sync cards and Start drill;
// during a drill, the card's position named by its line, the verdict on the move played, Hard and
// Easy for a correct one while its grade can still change, and Next; the summary when no card is
// due. It shows what the window passes on and asks for everything through its signals. GUI thread
// only.
#pragma once

#include "profile_data.hpp"

#include <QString>
#include <QWidget>

class QFrame;
class QLabel;
class QPushButton;
class QRadioButton;

namespace chesscoach::app {

class DrillScreen : public QWidget {
    Q_OBJECT

  public:
    explicit DrillScreen(QWidget* parent = nullptr);

    [[nodiscard]] bool user_white() const;

    // The colour's cards and both repertoires' progress.
    void show_cards(const CardsData& cards);
    // A drill runs, or none does: the controls follow.
    void set_drilling(bool drilling);
    // The card to answer, named by its line or its FEN.
    void show_card(const DrillCard& card);
    // A correct move; with grading, Hard and Easy may still change its grade.
    void show_correct(const QString& san, bool grading);
    void show_wrong(const QString& played_san, const QString& answer_san);
    // The grade is recorded: Hard and Easy go.
    void grade_recorded();
    void show_summary(int reviewed, int correct, int again);
    void show_message(const QString& text);

  signals:
    void colour_changed(bool white);
    void sync_requested();
    void start_requested();
    void next_requested();
    // Hard or Easy (core's Grade values) instead of Good for the correct move.
    void grade_chosen(int grade);

  private:
    QRadioButton* white_;
    QRadioButton* black_;
    QPushButton* sync_;
    QPushButton* start_;
    QLabel* due_;
    QLabel* progress_;
    QLabel* status_;
    QLabel* verdict_;
    QPushButton* hard_;
    QPushButton* easy_;
    QPushButton* next_;
    QFrame* summary_;
    QLabel* summary_text_;
};

} // namespace chesscoach::app

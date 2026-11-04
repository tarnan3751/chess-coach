// The Drill screen (docs/reference/app.md, **Drill**): the colour whose cards are drilled; the
// cards due of the cards there are, and both repertoires' progress; Update cards from your
// repertoire and Start the drill; during a drill, the card's position named by its line, the
// verdict on the move played, Hard, About right and Easy to grade a correct one, and Next card
// once the answer is graded or was wrong; the summary when no card is due. It shows what the window
// passes on and asks for everything through its signals. GUI thread only.
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
    // A correct move, which Hard, About right or Easy grades before Next card shows.
    void show_correct(const QString& san);
    void show_wrong(const QString& played_san, const QString& answer_san);
    // The grade is recorded: the grades go and Next card shows.
    void grade_recorded();
    void show_summary(int reviewed, int correct, int again);
    void show_message(const QString& text);

  signals:
    void colour_changed(bool white);
    void sync_requested();
    void start_requested();
    void next_requested();
    // Hard, About right or Easy (core's Grade values hard, good and easy) for the correct move.
    void grade_chosen(int grade);

  private:
    void show_grades(bool shown);

    QRadioButton* white_;
    QRadioButton* black_;
    QPushButton* sync_;
    QPushButton* start_;
    QLabel* due_;
    QLabel* progress_;
    QLabel* status_;
    QLabel* verdict_;
    QPushButton* hard_;
    QPushButton* good_;
    QPushButton* easy_;
    QPushButton* next_;
    QFrame* summary_;
    QLabel* summary_text_;
};

} // namespace chesscoach::app

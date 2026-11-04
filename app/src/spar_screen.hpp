// The Spar screen, labelled Practice in the app (docs/reference/app.md, **Spar**): the colour to
// play, Start a practice game and End the game; a status line with the move number and the
// session's phase; the interruption panel, shown when the coach interrupts a move, with the
// reason, the move's badges and loss, the better choices (move, badges, games, result bar) and
// Keep playing; the summary at the end; and the saved sessions with Replay this game and Previous
// move and Next move to step through one. It shows what the window passes
// on from the spar worker and asks for nothing itself but through its signals. GUI thread only.
#pragma once

#include "spar_data.hpp"

#include <QString>
#include <QVector>
#include <QWidget>
#include <optional>

class QFrame;
class QLabel;
class QPushButton;
class QRadioButton;
class QTableWidget;

namespace chesscoach::app {

class SparScreen : public QWidget {
    Q_OBJECT

  public:
    explicit SparScreen(QWidget* parent = nullptr);

    // Whether the user chose white.
    [[nodiscard]] bool user_white() const;
    [[nodiscard]] bool interruption_shown() const;

    // A session runs, or none does: the controls follow.
    void set_running(bool running);
    void show_state(const SparState& state);
    void show_message(const QString& text);
    // The panel for an interruption; with continue_button false it is shown read-only, as a
    // replay shows it.
    void show_interruption(const SparInterruption& interruption, bool continue_button = true);
    void hide_interruption();
    void show_summary(const SparSummary& summary);
    void show_sessions(const QVector<SparSessionInfo>& sessions);
    // Replay mode at a ply of a session of count plies, with the interruption met there.
    void show_replay(int ply, int count, const std::optional<SparInterruption>& interruption);
    void end_replay();

  signals:
    void start_requested(bool user_white);
    void end_requested();
    void continue_requested();
    void replay_requested(qint64 id);
    void replay_step(int delta);

  private:
    QRadioButton* white_;
    QRadioButton* black_;
    QPushButton* start_;
    QPushButton* end_;
    QLabel* status_;
    QFrame* interruption_;
    QLabel* reason_;
    QLabel* played_;
    QTableWidget* better_;
    QPushButton* continue_;
    QFrame* summary_;
    QLabel* summary_text_;
    QTableWidget* sessions_;
    QPushButton* replay_;
    QPushButton* previous_;
    QPushButton* next_;
    QLabel* replay_position_;
};

} // namespace chesscoach::app

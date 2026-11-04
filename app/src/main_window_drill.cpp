// The main window's Drill screen (main_window.hpp; docs/reference/app.md, **Drill**): the cards
// synced on entering, a drill taking the first due card each time, the board at the card's
// position for the user's move, the grade recorded once through the profile worker (again at
// once for a wrong move, which sounds its tone and whose answer the board then plays; for a
// correct one the grade the user presses, Hard, About right or Easy, or good when the drill ends
// before a press), and the summary when no card is due.
#include <chesscoach/position.hpp>
#include <chesscoach/scheduler.hpp>

#include "board_view.hpp"
#include "drill_screen.hpp"
#include "main_window.hpp"
#include "repertoire_screen.hpp"
#include "sounds.hpp"
#include "wording.hpp"

#include <QStatusBar>
#include <QStringList>

namespace chesscoach::app {

void MainWindow::enter_drill() {
    if (drilling_) {
        return;
    }
    face(drill_->user_white());
    board_->set_position(Position::start());
    rebuild_move_list();
    // The worker answers in order: the sync first, so the count is the synced one.
    emit sync_requested(drill_->user_white());
    emit due_requested(drill_->user_white());
}

void MainWindow::leave_drill() {
    if (!drilling_) {
        return;
    }
    drill_record();
    drilling_ = false;
    card_.reset();
    answered_ = false;
    drill_->set_drilling(false);
    drill_->show_summary(reviewed_, correct_, again_);
}

void MainWindow::drill_start() {
    drilling_ = true;
    reviewed_ = 0;
    correct_ = 0;
    again_ = 0;
    card_.reset();
    answered_ = false;
    recorded_ = false;
    drill_->set_drilling(true);
    drill_->show_message(tr("Finding the cards that are due…"));
    emit due_requested(drill_->user_white());
    update_board_input();
}

void MainWindow::cards_ready(const CardsData& cards) {
    if (cards.white != drill_->user_white()) {
        return;
    }
    drill_->show_cards(cards);
    if (!drilling_ || card_.has_value()) {
        return;
    }
    if (cards.due.isEmpty()) {
        drilling_ = false;
        drill_->set_drilling(false);
        drill_->show_summary(reviewed_, correct_, again_);
        update_board_input();
        return;
    }
    const DrillCard& next = cards.due.front();
    const auto position = Position::from_fen(next.fen.toStdString());
    if (!position) {
        // Cannot happen: the profile store keeps only FENs that parse to their keys.
        drill_->show_message(tr("This card's position could not be read. Press Update cards "
                                "from your repertoire and start again."));
        return;
    }
    card_ = next;
    answered_ = false;
    recorded_ = false;
    grade_ = static_cast<int>(Grade::good);
    face(next.white);
    board_->set_position(*position);
    drill_->show_card(next);
    update_board_input();
}

void MainWindow::drill_move_played(const QString& uci, const QString& san) {
    if (applying_answer_ || !card_.has_value() || answered_) {
        return;
    }
    const DrillCard& card = *card_;
    answered_ = true;
    ++reviewed_;
    if (uci == card.move) {
        ++correct_;
        grade_ = static_cast<int>(Grade::good);
        drill_->show_correct(san);
    } else {
        ++again_;
        grade_ = static_cast<int>(Grade::again);
        drill_record();
        drill_->show_wrong(san, card.move_san);
        sounds_.play(SoundEvent::wrong);
        // The card's move, played on the board from the card's position.
        if (const auto position = Position::from_fen(card.fen.toStdString())) {
            board_->set_position(*position);
            applying_answer_ = true;
            board_->play_move(card.move);
            applying_answer_ = false;
        }
    }
    update_board_input();
}

void MainWindow::drill_record() {
    if (!card_.has_value() || !answered_ || recorded_) {
        return;
    }
    recorded_ = true;
    emit review_requested(card_->id, card_->white, card_->key, grade_);
    drill_->grade_recorded();
}

void MainWindow::drill_grade(int grade) {
    if (!answered_ || recorded_ || grade_ == static_cast<int>(Grade::again)) {
        return;
    }
    grade_ = grade;
    drill_record();
}

void MainWindow::drill_next() {
    drill_record();
    card_.reset();
    answered_ = false;
    update_board_input();
    emit due_requested(drill_->user_white());
}

void MainWindow::cards_synced(bool white, int created, int reset, int deleted) {
    if (white != drill_->user_white() || drilling_ || created + reset + deleted == 0) {
        return;
    }
    QStringList said{tr("Your cards now match your repertoire.")};
    if (created > 0) {
        said << counted(created, tr("%1 new card was made."), tr("%1 new cards were made."));
    }
    if (reset > 0) {
        said << counted(reset, tr("%1 card starts again because its move changed."),
                        tr("%1 cards start again because their moves changed."));
    }
    if (deleted > 0) {
        said << counted(deleted, tr("%1 card was removed with its move."),
                        tr("%1 cards were removed with their moves."));
    }
    drill_->show_message(said.join(QLatin1Char(' ')));
}

void MainWindow::review_recorded(qint64 /*card_id*/, qint64 due_at) {
    statusBar()->showMessage(tr("You will see this card again on %1.").arg(day(due_at)), 5000);
}

void MainWindow::profile_failed(const QString& message) {
    if (screen_ == Screen::drill) {
        drill_->show_message(message);
    } else if (screen_ == Screen::repertoire) {
        repertoire_->show_message(message);
    } else {
        show_profile_error(message);
    }
}

} // namespace chesscoach::app

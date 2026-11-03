// The main window's Repertoire screen (main_window.hpp; docs/reference/app.md, **Repertoire**):
// the line the board is at, a board move setting the choice where the repertoire's colour is to
// move and following a reply where the opponent is; the tree, the coverage, the recommendation
// score and the import, each asked of the profile worker and shown when it answers.
#include <chesscoach/position.hpp>

#include "board_view.hpp"
#include "main_window.hpp"
#include "repertoire_screen.hpp"

#include <QListWidget>

namespace chesscoach::app {

Position MainWindow::repertoire_position() const {
    Position position = Position::start();
    for (const std::string& uci : repertoire_uci_) {
        static_cast<void>(position.play_uci(uci));
    }
    return position;
}

void MainWindow::enter_repertoire() {
    repertoire_white_ = repertoire_->user_white();
    face(repertoire_white_);
    board_->set_position(repertoire_position());
    rebuild_move_list();
    repertoire_refresh();
}

void MainWindow::repertoire_refresh() {
    emit repertoire_requested(repertoire_white_);
    if (db_path_.isEmpty()) {
        repertoire_->show_coverage_message(
            tr("Coverage needs an opening database: open one with --db."));
    } else {
        emit coverage_requested(repertoire_white_, tier(), settings_.repertoire_depth());
    }
    repertoire_update_position();
}

void MainWindow::repertoire_show_choice() {
    const Position position = repertoire_position();
    const bool to_move = (position.side_to_move() == Color::white) == repertoire_white_;
    QString choice_san;
    if (to_move) {
        const auto choice = choices_.constFind(position.key());
        if (choice != choices_.constEnd()) {
            const auto san = position.san_of(choice->toStdString());
            choice_san = san ? QString::fromStdString(*san) : *choice;
        }
    }
    repertoire_->show_position(to_move, choice_san);
}

void MainWindow::repertoire_update_position() {
    repertoire_show_choice();
    const Position position = repertoire_position();
    const bool to_move = (position.side_to_move() == Color::white) == repertoire_white_;
    if (to_move && !db_path_.isEmpty()) {
        repertoire_->show_scores(ScoreData{});
        emit scores_requested(position.key(), tier());
    }
}

void MainWindow::repertoire_move_played(const QString& uci, const QString& san) {
    const Position before = repertoire_position();
    repertoire_uci_.push_back(uci.toStdString());
    repertoire_san_.append(san);
    if ((before.side_to_move() == Color::white) == repertoire_white_) {
        emit choice_requested(repertoire_white_, before.key(), QString::fromStdString(before.fen()),
                              uci);
    }
    rebuild_move_list();
    repertoire_update_position();
}

void MainWindow::repertoire_go(const QStringList& line) {
    Position position = Position::start();
    repertoire_uci_.clear();
    repertoire_san_.clear();
    for (const QString& uci : line) {
        const auto played = position.play_uci(uci.toStdString());
        if (!played) {
            break; // cannot happen: the lines come from moves played through Position
        }
        repertoire_uci_.push_back(played->uci);
        repertoire_san_.append(QString::fromStdString(played->san));
    }
    board_->set_position(position);
    rebuild_move_list();
    repertoire_update_position();
}

void MainWindow::repertoire_jump(qsizetype ply) {
    const auto size = static_cast<std::size_t>(std::max<qsizetype>(ply, 0));
    if (size < repertoire_uci_.size()) {
        repertoire_uci_.resize(size);
        repertoire_san_.resize(static_cast<qsizetype>(size));
    }
    board_->set_position(repertoire_position());
    rebuild_move_list();
    repertoire_update_position();
}

void MainWindow::repertoire_colour_changed(bool white) {
    repertoire_white_ = white;
    choices_.clear();
    face(white);
    repertoire_refresh();
}

void MainWindow::repertoire_choose(const QString& uci) {
    const Position position = repertoire_position();
    if ((position.side_to_move() == Color::white) == repertoire_white_) {
        emit choice_requested(repertoire_white_, position.key(),
                              QString::fromStdString(position.fen()), uci);
    }
}

void MainWindow::repertoire_remove() {
    emit choice_removal_requested(repertoire_white_, repertoire_position().key());
}

void MainWindow::repertoire_changed(bool white, const QString& message) {
    if (!message.isEmpty()) {
        repertoire_->show_message(message);
    }
    if (white == repertoire_white_ && screen_ == Screen::repertoire) {
        repertoire_refresh();
    }
}

void MainWindow::repertoire_ready(const RepertoireData& repertoire) {
    if (repertoire.white != repertoire_white_) {
        return;
    }
    choices_ = repertoire.choices;
    repertoire_->show_repertoire(repertoire);
    repertoire_show_choice();
}

void MainWindow::coverage_ready(const CoverageData& coverage) {
    if (coverage.white != repertoire_white_ || coverage.tier != tier()) {
        return;
    }
    repertoire_->show_coverage(coverage, tier_text(coverage.tier));
}

void MainWindow::scores_ready(const ScoreData& scores) {
    if (screen_ != Screen::repertoire || scores.tier != tier() ||
        scores.key != repertoire_position().key()) {
        return;
    }
    repertoire_->show_scores(scores);
}

void MainWindow::pgn_imported(const PgnImportInfo& import) {
    repertoire_->show_message(
        tr("Imported %1 games and %2 variations: %3 choices, %4 added, %5 already made, %6 "
           "conflicts, %7 unparsable.")
            .arg(import.games)
            .arg(import.variations)
            .arg(import.choices)
            .arg(import.added)
            .arg(import.unchanged)
            .arg(import.conflicts)
            .arg(import.unparsable));
}

} // namespace chesscoach::app

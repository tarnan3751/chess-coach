// The Spar screen (spar_screen.hpp).
#include "spar_screen.hpp"

#include "result_delegate.hpp"
#include "tables.hpp"
#include "wording.hpp"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QRadioButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

namespace chesscoach::app {

namespace {

// A game's result in words: "White won", "Black won" or "Draw"; anything else as it is.
QString result_words(const QString& result) {
    if (result == QStringLiteral("1-0")) {
        return SparScreen::tr("White won");
    }
    if (result == QStringLiteral("0-1")) {
        return SparScreen::tr("Black won");
    }
    if (result == QStringLiteral("1/2-1/2")) {
        return SparScreen::tr("Draw");
    }
    return result;
}

// How the summary says the game ended.
QString ending(const SparSummary& summary) {
    if (!summary.ended) {
        return SparScreen::tr("You ended this practice game early.");
    }
    if (summary.result.isEmpty()) {
        return SparScreen::tr("The practice game reached its move limit.");
    }
    if (summary.result == QStringLiteral("1/2-1/2")) {
        return SparScreen::tr("The practice game was a draw.");
    }
    return SparScreen::tr("%1 the practice game.").arg(result_words(summary.result));
}

// How often the coach stopped the user.
QString stops(int interruptions) {
    switch (interruptions) {
    case 0:
        return SparScreen::tr("The coach did not need to stop you.");
    case 1:
        return SparScreen::tr("The coach stopped you once.");
    case 2:
        return SparScreen::tr("The coach stopped you twice.");
    default:
        return SparScreen::tr("The coach stopped you %1 times.").arg(interruptions);
    }
}

} // namespace

SparScreen::SparScreen(QWidget* parent)
    : QWidget(parent), white_(new QRadioButton(tr("Play White"), this)),
      black_(new QRadioButton(tr("Play Black"), this)),
      start_(new QPushButton(tr("Start a practice game"), this)),
      end_(new QPushButton(tr("End the game"), this)), status_(new QLabel(this)),
      interruption_(new QFrame(this)), reason_(new QLabel(interruption_)),
      played_(new QLabel(interruption_)),
      better_(make_table({tr("Move"), tr("Badges"), tr("Games"), tr("Results")}, interruption_)),
      continue_(new QPushButton(tr("Keep playing"), interruption_)), summary_(new QFrame(this)),
      summary_text_(new QLabel(summary_)),
      sessions_(make_table(
          {tr("Date"), tr("Rating"), tr("You played"), tr("Moves"), tr("Stops"), tr("Result")},
          this)),
      replay_(new QPushButton(tr("Replay this game"), this)),
      previous_(new QPushButton(tr("Previous move"), this)),
      next_(new QPushButton(tr("Next move"), this)), replay_position_(new QLabel(this)) {
    setObjectName(QStringLiteral("spar_screen"));
    white_->setObjectName(QStringLiteral("spar_white"));
    black_->setObjectName(QStringLiteral("spar_black"));
    start_->setObjectName(QStringLiteral("spar_start"));
    end_->setObjectName(QStringLiteral("spar_end"));
    status_->setObjectName(QStringLiteral("spar_status"));
    interruption_->setObjectName(QStringLiteral("spar_interruption"));
    reason_->setObjectName(QStringLiteral("spar_reason"));
    played_->setObjectName(QStringLiteral("spar_played"));
    better_->setObjectName(QStringLiteral("spar_better"));
    continue_->setObjectName(QStringLiteral("spar_continue"));
    summary_->setObjectName(QStringLiteral("spar_summary"));
    summary_text_->setObjectName(QStringLiteral("spar_summary_text"));
    sessions_->setObjectName(QStringLiteral("spar_sessions"));
    replay_->setObjectName(QStringLiteral("spar_replay"));
    previous_->setObjectName(QStringLiteral("spar_previous"));
    next_->setObjectName(QStringLiteral("spar_next"));
    replay_position_->setObjectName(QStringLiteral("spar_replay_position"));
    white_->setChecked(true);
    status_->setWordWrap(true);
    reason_->setWordWrap(true);
    played_->setWordWrap(true);
    summary_text_->setWordWrap(true);
    summary_text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    better_->setItemDelegateForColumn(3, new ResultDelegate(better_, &palette_));
    better_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    better_->horizontalHeaderItem(1)->setToolTip(
        tr("Book is what strong players (2200 and above) choose. Popular is often chosen at your "
           "rating. Dubious is popular at your rating, but it loses ground."));
    better_->horizontalHeaderItem(3)->setToolTip(
        tr("White wins · draws · Black wins, as a share of the games"));
    for (QFrame* frame : {interruption_, summary_}) {
        frame->setFrameShape(QFrame::StyledPanel);
        frame->hide();
    }

    auto* controls = new QHBoxLayout();
    controls->addWidget(white_);
    controls->addWidget(black_);
    controls->addStretch(1);
    controls->addWidget(start_);
    controls->addWidget(end_);

    auto* interruption = new QVBoxLayout(interruption_);
    interruption->addWidget(reason_);
    interruption->addWidget(played_);
    interruption->addWidget(new QLabel(tr("Better choices here"), interruption_));
    interruption->addWidget(better_);
    interruption->addWidget(continue_, 0, Qt::AlignRight);

    auto* summary = new QVBoxLayout(summary_);
    summary->addWidget(summary_text_);

    auto* stepping = new QHBoxLayout();
    stepping->addWidget(replay_);
    stepping->addStretch(1);
    stepping->addWidget(previous_);
    stepping->addWidget(replay_position_);
    stepping->addWidget(next_);

    // What the screen is for and the first step, in the muted colour (the stylesheets'
    // QLabel[muted="true"]).
    auto* description = new QLabel(
        tr("Play an opening against the coach. It follows what players at your rating play, "
           "stops you when you play a dubious move or leave the known lines, and shows better "
           "choices."),
        this);
    description->setObjectName(QStringLiteral("spar_description"));
    description->setWordWrap(true);
    description->setProperty("muted", true);
    auto* column = new QVBoxLayout(this);
    column->addWidget(description);
    column->addLayout(controls);
    column->addWidget(status_);
    column->addWidget(interruption_);
    column->addWidget(summary_);
    column->addWidget(new QLabel(tr("Your saved practice games"), this));
    column->addWidget(sessions_, 1);
    column->addLayout(stepping);

    connect(start_, &QPushButton::clicked, this, [this] { emit start_requested(user_white()); });
    connect(end_, &QPushButton::clicked, this, &SparScreen::end_requested);
    connect(continue_, &QPushButton::clicked, this, [this] {
        hide_interruption();
        emit continue_requested();
    });
    connect(replay_, &QPushButton::clicked, this, [this] {
        const QTableWidgetItem* selected = sessions_->item(sessions_->currentRow(), 0);
        if (selected != nullptr) {
            emit replay_requested(selected->data(Qt::UserRole).toLongLong());
        }
    });
    connect(previous_, &QPushButton::clicked, this, [this] { emit replay_step(-1); });
    connect(next_, &QPushButton::clicked, this, [this] { emit replay_step(1); });
    set_running(false);
    end_replay();
    show_message(tr("Choose your colour and press Start a practice game. The coach plays like "
                    "the players at the rating in the Rating menu."));
}

void SparScreen::set_palette(const StudyPalette& palette) {
    palette_ = palette;
    better_->viewport()->update();
}

bool SparScreen::user_white() const { return white_->isChecked(); }

bool SparScreen::interruption_shown() const { return interruption_->isVisible(); }

void SparScreen::set_running(bool running) {
    white_->setEnabled(!running);
    black_->setEnabled(!running);
    start_->setEnabled(!running);
    end_->setEnabled(running);
    replay_->setEnabled(!running);
    if (running) {
        summary_->hide();
        end_replay();
    }
}

void SparScreen::show_state(const SparState& state) {
    status_->setText(state.ended ? state.phase
                                 : tr("Move %1. %2").arg(state.move_number).arg(state.phase));
}

void SparScreen::show_message(const QString& text) { status_->setText(text); }

void SparScreen::show_interruption(const SparInterruption& interruption, bool continue_button) {
    reason_->setText(interruption.reason);
    // A Dubious move's reason has said its badges; another move's are news.
    QStringList said;
    if (!interruption.dubious && !interruption.badges.isEmpty()) {
        said << interruption.badges;
    }
    said << (interruption.loss ? loss_sentence(*interruption.loss)
                               : tr("The database has no engine evaluation for it."));
    played_->setText(said.join(QLatin1Char(' ')));
    better_->setRowCount(0);
    for (const SparChoice& choice : interruption.better) {
        const int row = better_->rowCount();
        better_->insertRow(row);
        better_->setItem(row, 0, table_item(choice.san));
        better_->setItem(row, 1, table_item(choice.badges));
        better_->setItem(row, 2, table_item(number(choice.total)));
        auto* result = table_item(QString());
        const auto total = static_cast<double>(choice.total > 0 ? choice.total : 1);
        result->setData(shares_role, QVariant::fromValue(QList<double>{
                                         static_cast<double>(choice.white) / total,
                                         static_cast<double>(choice.draws) / total,
                                         static_cast<double>(choice.black) / total,
                                     }));
        better_->setItem(row, 3, result);
    }
    continue_->setVisible(continue_button);
    interruption_->show();
}

void SparScreen::hide_interruption() { interruption_->hide(); }

void SparScreen::show_summary(const SparSummary& summary) {
    QStringList numbered;
    for (qsizetype ply = 0; ply < summary.moves_san.size(); ply += 2) {
        QString pair = QStringLiteral("%1. %2").arg((ply / 2) + 1).arg(summary.moves_san.at(ply));
        if (ply + 1 < summary.moves_san.size()) {
            pair += QLatin1Char(' ') + summary.moves_san.at(ply + 1);
        }
        numbered << pair;
    }
    summary_text_->setText(
        QStringList{ending(summary), stops(summary.interruptions), summary.phase_reached}.join(
            QLatin1Char(' ')) +
        QLatin1Char('\n') + numbered.join(QLatin1Char(' ')));
    summary_->show();
}

void SparScreen::show_sessions(const QVector<SparSessionInfo>& sessions) {
    sessions_->setRowCount(0);
    for (const SparSessionInfo& session : sessions) {
        const int row = sessions_->rowCount();
        sessions_->insertRow(row);
        auto* date = table_item(tr("%1, %2").arg(
            day(session.started_at),
            QDateTime::fromSecsSinceEpoch(session.started_at).toString(QStringLiteral("HH:mm"))));
        date->setData(Qt::UserRole, QVariant::fromValue(session.id));
        sessions_->setItem(row, 0, date);
        sessions_->setItem(row, 1, table_item(rating_band(session.tier)));
        sessions_->setItem(row, 2, table_item(session.user_white ? tr("White") : tr("Black")));
        // The moves as a game counts them, white's and black's together: six plies are three.
        sessions_->setItem(row, 3, table_item(number((session.moves + 1) / 2)));
        sessions_->setItem(row, 4, table_item(number(session.interruptions)));
        sessions_->setItem(
            row, 5,
            table_item(session.result.isEmpty() ? tr("Move limit") : result_words(session.result)));
    }
    if (sessions_->rowCount() > 0) {
        sessions_->selectRow(0);
    }
}

void SparScreen::show_replay(int ply, int count,
                             const std::optional<SparInterruption>& interruption) {
    summary_->hide();
    status_->setText(tr("Replaying a saved practice game."));
    previous_->setVisible(true);
    next_->setVisible(true);
    replay_position_->setVisible(true);
    previous_->setEnabled(ply > 0);
    next_->setEnabled(ply < count);
    replay_position_->setText(tr("Position %1 of %2").arg(ply).arg(count));
    if (interruption) {
        show_interruption(*interruption, false);
    } else {
        hide_interruption();
    }
}

void SparScreen::end_replay() {
    previous_->setVisible(false);
    next_->setVisible(false);
    replay_position_->setVisible(false);
}

} // namespace chesscoach::app

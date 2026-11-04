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
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

namespace chesscoach::app {

SparScreen::SparScreen(QWidget* parent)
    : QWidget(parent), white_(new QRadioButton(tr("White"), this)),
      black_(new QRadioButton(tr("Black"), this)), start_(new QPushButton(tr("Start"), this)),
      end_(new QPushButton(tr("End session"), this)), status_(new QLabel(this)),
      interruption_(new QFrame(this)), reason_(new QLabel(interruption_)),
      played_(new QLabel(interruption_)),
      better_(make_table({tr("Move"), tr("Badges"), tr("Games"), tr("Result")}, interruption_)),
      continue_(new QPushButton(tr("Continue"), interruption_)), summary_(new QFrame(this)),
      summary_text_(new QLabel(summary_)),
      sessions_(make_table(
          {tr("Date"), tr("Tier"), tr("Colour"), tr("Moves"), tr("Interruptions"), tr("Result")},
          this)),
      replay_(new QPushButton(tr("Replay"), this)),
      previous_(new QPushButton(tr("Previous"), this)), next_(new QPushButton(tr("Next"), this)),
      replay_position_(new QLabel(this)) {
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
    summary_text_->setWordWrap(true);
    summary_text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    better_->setItemDelegateForColumn(3, new ResultDelegate(better_));
    better_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
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
    interruption->addWidget(new QLabel(tr("Better choices:"), interruption_));
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

    auto* column = new QVBoxLayout(this);
    column->addLayout(controls);
    column->addWidget(status_);
    column->addWidget(interruption_);
    column->addWidget(summary_);
    column->addWidget(new QLabel(tr("Saved sessions"), this));
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
    show_message(tr("Choose a colour and start a session at the selected tier."));
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
                                 : tr("%1 · move %2").arg(state.phase).arg(state.move_number));
}

void SparScreen::show_message(const QString& text) { status_->setText(text); }

void SparScreen::show_interruption(const SparInterruption& interruption, bool continue_button) {
    reason_->setText(tr("%1: %2").arg(interruption.reason, interruption.played_san));
    const QString badges = interruption.badges.isEmpty() ? tr("no badges") : interruption.badges;
    const QString loss =
        interruption.loss ? tr("loses %1 cp").arg(*interruption.loss) : tr("loss unknown");
    played_->setText(tr("%1 played: %2, %3").arg(interruption.played_san, badges, loss));
    better_->setRowCount(0);
    for (const SparChoice& choice : interruption.better) {
        const int row = better_->rowCount();
        better_->insertRow(row);
        better_->setItem(row, 0, table_item(choice.san));
        better_->setItem(row, 1, table_item(choice.badges));
        better_->setItem(row, 2, table_item(QString::number(choice.total)));
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
    QString result = summary.result;
    if (!summary.ended) {
        result = tr("stopped");
    } else if (result.isEmpty()) {
        result = tr("ended at its move limit");
    }
    summary_text_->setText(tr("Session over: %1.\nSeed %2. Interruptions: %3. Reached: %4.\n%5")
                               .arg(result)
                               .arg(summary.seed)
                               .arg(summary.interruptions)
                               .arg(summary.phase_reached, numbered.join(QLatin1Char(' '))));
    summary_->show();
}

void SparScreen::show_sessions(const QVector<SparSessionInfo>& sessions) {
    sessions_->setRowCount(0);
    for (const SparSessionInfo& session : sessions) {
        const int row = sessions_->rowCount();
        sessions_->insertRow(row);
        auto* date = table_item(QDateTime::fromSecsSinceEpoch(session.started_at)
                                    .toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        date->setData(Qt::UserRole, QVariant::fromValue(session.id));
        sessions_->setItem(row, 0, date);
        sessions_->setItem(row, 1, table_item(rating_band(session.tier)));
        sessions_->setItem(row, 2, table_item(session.user_white ? tr("White") : tr("Black")));
        sessions_->setItem(row, 3, table_item(QString::number(session.moves)));
        sessions_->setItem(row, 4, table_item(QString::number(session.interruptions)));
        sessions_->setItem(
            row, 5, table_item(session.result.isEmpty() ? tr("move limit") : session.result));
    }
    if (sessions_->rowCount() > 0) {
        sessions_->selectRow(0);
    }
}

void SparScreen::show_replay(int ply, int count,
                             const std::optional<SparInterruption>& interruption) {
    summary_->hide();
    status_->setText(tr("Replaying a saved session"));
    previous_->setVisible(true);
    next_->setVisible(true);
    replay_position_->setVisible(true);
    previous_->setEnabled(ply > 0);
    next_->setEnabled(ply < count);
    replay_position_->setText(tr("ply %1 of %2").arg(ply).arg(count));
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

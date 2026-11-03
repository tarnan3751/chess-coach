// The Drill screen (drill_screen.hpp).
#include "drill_screen.hpp"

#include <chesscoach/scheduler.hpp>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace chesscoach::app {

DrillScreen::DrillScreen(QWidget* parent)
    : QWidget(parent), white_(new QRadioButton(tr("White"), this)),
      black_(new QRadioButton(tr("Black"), this)), sync_(new QPushButton(tr("Sync cards"), this)),
      start_(new QPushButton(tr("Start drill"), this)), due_(new QLabel(this)),
      progress_(new QLabel(this)), status_(new QLabel(this)), verdict_(new QLabel(this)),
      hard_(new QPushButton(tr("Hard"), this)), easy_(new QPushButton(tr("Easy"), this)),
      next_(new QPushButton(tr("Next"), this)), summary_(new QFrame(this)),
      summary_text_(new QLabel(summary_)) {
    setObjectName(QStringLiteral("drill_screen"));
    white_->setObjectName(QStringLiteral("drill_white"));
    black_->setObjectName(QStringLiteral("drill_black"));
    sync_->setObjectName(QStringLiteral("drill_sync"));
    start_->setObjectName(QStringLiteral("drill_start"));
    due_->setObjectName(QStringLiteral("drill_due"));
    progress_->setObjectName(QStringLiteral("drill_progress"));
    status_->setObjectName(QStringLiteral("drill_status"));
    verdict_->setObjectName(QStringLiteral("drill_verdict"));
    hard_->setObjectName(QStringLiteral("drill_hard"));
    easy_->setObjectName(QStringLiteral("drill_easy"));
    next_->setObjectName(QStringLiteral("drill_next"));
    summary_->setObjectName(QStringLiteral("drill_summary"));
    summary_text_->setObjectName(QStringLiteral("drill_summary_text"));
    white_->setChecked(true);
    status_->setWordWrap(true);
    verdict_->setWordWrap(true);
    summary_text_->setWordWrap(true);
    summary_->setFrameShape(QFrame::StyledPanel);
    summary_->hide();

    auto* controls = new QHBoxLayout();
    controls->addWidget(white_);
    controls->addWidget(black_);
    controls->addStretch(1);
    controls->addWidget(sync_);
    controls->addWidget(start_);
    auto* grading = new QHBoxLayout();
    grading->addWidget(hard_);
    grading->addWidget(easy_);
    grading->addStretch(1);
    grading->addWidget(next_);
    auto* summary = new QVBoxLayout(summary_);
    summary->addWidget(summary_text_);

    auto* column = new QVBoxLayout(this);
    column->addLayout(controls);
    column->addWidget(due_);
    column->addWidget(progress_);
    column->addWidget(status_);
    column->addWidget(verdict_);
    column->addLayout(grading);
    column->addWidget(summary_);
    column->addStretch(1);

    connect(white_, &QRadioButton::toggled, this,
            [this](bool checked) { emit colour_changed(checked); });
    connect(sync_, &QPushButton::clicked, this, &DrillScreen::sync_requested);
    connect(start_, &QPushButton::clicked, this, &DrillScreen::start_requested);
    connect(next_, &QPushButton::clicked, this, &DrillScreen::next_requested);
    connect(hard_, &QPushButton::clicked, this,
            [this] { emit grade_chosen(static_cast<int>(Grade::hard)); });
    connect(easy_, &QPushButton::clicked, this,
            [this] { emit grade_chosen(static_cast<int>(Grade::easy)); });
    set_drilling(false);
    show_message(tr("Start a drill to answer the cards that are due."));
}

bool DrillScreen::user_white() const { return white_->isChecked(); }

void DrillScreen::show_cards(const CardsData& cards) {
    due_->setText(tr("Due: %1 of %2 cards").arg(cards.due.size()).arg(cards.total));
    progress_->setText(tr("White: %1 due / %2 total · Black: %3 due / %4 total")
                           .arg(cards.white_due)
                           .arg(cards.white_total)
                           .arg(cards.black_due)
                           .arg(cards.black_total));
}

void DrillScreen::set_drilling(bool drilling) {
    white_->setEnabled(!drilling);
    black_->setEnabled(!drilling);
    sync_->setEnabled(!drilling);
    start_->setEnabled(!drilling);
    next_->setVisible(drilling);
    next_->setEnabled(false);
    hard_->hide();
    easy_->hide();
    if (drilling) {
        summary_->hide();
    }
}

void DrillScreen::show_card(const DrillCard& card) {
    status_->setText(tr("Your move after %1").arg(card.name));
    verdict_->clear();
    next_->setEnabled(false);
    hard_->hide();
    easy_->hide();
}

void DrillScreen::show_correct(const QString& san, bool grading) {
    verdict_->setText(grading ? tr("Correct: %1. Good, unless it was hard or easy.").arg(san)
                              : tr("Correct: %1.").arg(san));
    hard_->setVisible(grading);
    easy_->setVisible(grading);
    next_->setEnabled(true);
}

void DrillScreen::show_wrong(const QString& played_san, const QString& answer_san) {
    verdict_->setText(tr("%1 is not the move: it is %2.").arg(played_san, answer_san));
    hard_->hide();
    easy_->hide();
    next_->setEnabled(true);
}

void DrillScreen::grade_recorded() {
    hard_->hide();
    easy_->hide();
}

void DrillScreen::show_summary(int reviewed, int correct, int again) {
    summary_text_->setText(
        tr("Drill over: %1 reviewed, %2 correct, %3 again.").arg(reviewed).arg(correct).arg(again));
    summary_->show();
    status_->setText(tr("No card is due."));
    verdict_->clear();
}

void DrillScreen::show_message(const QString& text) { status_->setText(text); }

} // namespace chesscoach::app

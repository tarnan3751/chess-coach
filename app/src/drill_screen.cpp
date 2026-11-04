// The Drill screen (drill_screen.hpp).
#include "drill_screen.hpp"

#include <chesscoach/scheduler.hpp>

#include "wording.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace chesscoach::app {

DrillScreen::DrillScreen(QWidget* parent)
    : QWidget(parent), white_(new QRadioButton(tr("White repertoire"), this)),
      black_(new QRadioButton(tr("Black repertoire"), this)),
      sync_(new QPushButton(tr("Update cards from your repertoire"), this)),
      start_(new QPushButton(tr("Start the drill"), this)), due_(new QLabel(this)),
      progress_(new QLabel(this)), status_(new QLabel(this)), verdict_(new QLabel(this)),
      hard_(new QPushButton(tr("Hard"), this)), good_(new QPushButton(tr("About right"), this)),
      easy_(new QPushButton(tr("Easy"), this)), next_(new QPushButton(tr("Next card"), this)),
      summary_(new QFrame(this)), summary_text_(new QLabel(summary_)) {
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
    good_->setObjectName(QStringLiteral("drill_good"));
    easy_->setObjectName(QStringLiteral("drill_easy"));
    next_->setObjectName(QStringLiteral("drill_next"));
    summary_->setObjectName(QStringLiteral("drill_summary"));
    summary_text_->setObjectName(QStringLiteral("drill_summary_text"));
    white_->setChecked(true);
    due_->setWordWrap(true);
    progress_->setWordWrap(true);
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
    grading->addWidget(good_);
    grading->addWidget(easy_);
    grading->addStretch(1);
    grading->addWidget(next_);
    auto* summary = new QVBoxLayout(summary_);
    summary->addWidget(summary_text_);

    // What the screen is for and the first step, in the muted colour (the stylesheets'
    // QLabel[muted="true"]).
    auto* description = new QLabel(
        tr("Flashcards on your repertoire. Each card shows a position. Play your move."), this);
    description->setObjectName(QStringLiteral("drill_description"));
    description->setWordWrap(true);
    description->setProperty("muted", true);
    auto* column = new QVBoxLayout(this);
    column->addWidget(description);
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
    connect(good_, &QPushButton::clicked, this,
            [this] { emit grade_chosen(static_cast<int>(Grade::good)); });
    connect(easy_, &QPushButton::clicked, this,
            [this] { emit grade_chosen(static_cast<int>(Grade::easy)); });
    set_drilling(false);
    show_message(tr("Press Start the drill to answer the cards that are due now."));
}

bool DrillScreen::user_white() const { return white_->isChecked(); }

namespace {

// "2 cards" or "1 card".
QString cards_of(qint64 n) {
    return counted(n, DrillScreen::tr("%1 card"), DrillScreen::tr("%1 cards"));
}

// One repertoire's cards in a sentence.
QString progress(const QString& colour, int due, int total) {
    if (total == 0) {
        return DrillScreen::tr("Your %1 repertoire has no cards.").arg(colour);
    }
    return DrillScreen::tr("Your %1 repertoire has %2, %3 due now.")
        .arg(colour, cards_of(total), number(due));
}

} // namespace

void DrillScreen::show_cards(const CardsData& cards) {
    const auto due = static_cast<qint64>(cards.due.size());
    if (cards.total == 0) {
        due_->setText(tr("You have no cards for this repertoire yet. Save moves on the Repertoire "
                         "tab to make them."));
    } else if (due == 0) {
        due_->setText(counted(cards.total, tr("Your %1 card is not due now."),
                              tr("None of your %1 cards is due now.")));
    } else {
        due_->setText(tr("%1 due now, out of your %2.").arg(number(due), cards_of(cards.total)));
    }
    progress_->setText(progress(tr("White"), cards.white_due, cards.white_total) +
                       QLatin1Char(' ') +
                       progress(tr("Black"), cards.black_due, cards.black_total));
}

void DrillScreen::set_drilling(bool drilling) {
    white_->setEnabled(!drilling);
    black_->setEnabled(!drilling);
    sync_->setEnabled(!drilling);
    start_->setEnabled(!drilling);
    show_grades(false);
    next_->hide();
    if (drilling) {
        summary_->hide();
    }
}

void DrillScreen::show_card(const DrillCard& card) {
    status_->setText(tr("Your move %1.").arg(card.name));
    verdict_->clear();
    show_grades(false);
    next_->hide();
}

void DrillScreen::show_correct(const QString& san) {
    // The user grades the answer; Next card comes once a grade is recorded.
    verdict_->setText(tr("Right. %1 is your move here. How hard was it to find?").arg(san));
    show_grades(true);
    next_->hide();
}

void DrillScreen::show_wrong(const QString& played_san, const QString& answer_san) {
    verdict_->setText(tr("Not quite. Your move here is %2, not %1. The board shows it now.")
                          .arg(played_san, answer_san));
    show_grades(false);
    next_->show();
}

void DrillScreen::grade_recorded() {
    show_grades(false);
    next_->show();
}

void DrillScreen::show_grades(bool shown) {
    for (QPushButton* button : {hard_, good_, easy_}) {
        button->setVisible(shown);
    }
}

void DrillScreen::show_summary(int reviewed, int correct, int again) {
    if (reviewed == 0) {
        summary_text_->setText(tr("The drill is over. You did not answer any cards."));
    } else {
        QString text = tr("The drill is over. You answered %1, %2 right and %3 wrong.")
                           .arg(cards_of(reviewed), number(correct), number(again));
        if (again > 0) {
            text += QLatin1Char(' ') + tr("The ones you missed come back tomorrow.");
        }
        summary_text_->setText(text);
    }
    summary_->show();
    status_->setText(tr("No more cards are due now. Come back later."));
    verdict_->clear();
}

void DrillScreen::show_message(const QString& text) { status_->setText(text); }

} // namespace chesscoach::app

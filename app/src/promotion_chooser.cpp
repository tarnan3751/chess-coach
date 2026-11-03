// The promotion popup (promotion_chooser.hpp).
#include "promotion_chooser.hpp"

#include "piece_set.hpp"

#include <QAction>
#include <QIcon>
#include <QSize>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <array>
#include <cstddef>

namespace chesscoach::app {

namespace {

struct Choice {
    char letter; // the UCI promotion letter
    const char* name;
};

constexpr std::array choices{
    Choice{.letter = 'q', .name = QT_TRANSLATE_NOOP("chesscoach::app::PromotionChooser", "Queen")},
    Choice{.letter = 'r', .name = QT_TRANSLATE_NOOP("chesscoach::app::PromotionChooser", "Rook")},
    Choice{.letter = 'b', .name = QT_TRANSLATE_NOOP("chesscoach::app::PromotionChooser", "Bishop")},
    Choice{.letter = 'n', .name = QT_TRANSLATE_NOOP("chesscoach::app::PromotionChooser", "Knight")},
};

} // namespace

PromotionChooser::PromotionChooser(QWidget* parent)
    : QFrame(parent), column_(new QVBoxLayout(this)) {
    setObjectName(QStringLiteral("promotion_chooser"));
    setFrameShape(QFrame::Box);
    setAutoFillBackground(true);
    column_->setContentsMargins(0, 0, 0, 0);
    column_->setSpacing(0);
    for (std::size_t i = 0; i < choices.size(); ++i) {
        const Choice& choice = choices.at(i);
        auto* action = new QAction(tr(choice.name), this);
        action->setObjectName(QStringLiteral("promote_") + QChar(choice.letter));
        const char letter = choice.letter;
        connect(action, &QAction::triggered, this, [this, letter] {
            hide();
            emit chosen(letter);
        });
        auto* button = new QToolButton(this);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        column_->addWidget(button);
        buttons_.at(i) = button;
    }
    hide();
}

void PromotionChooser::show_for(bool white, const PieceSet& pieces, QPoint top_left, int square,
                                bool downwards) {
    // The queen nearest the promotion square: first going down, last going up.
    for (QToolButton* button : buttons_) {
        column_->removeWidget(button);
    }
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        QToolButton* button = buttons_.at(downwards ? i : buttons_.size() - 1 - i);
        column_->addWidget(button);
        const char letter = choices.at(i).letter;
        const char piece = white ? static_cast<char>(letter - 'a' + 'A') : letter;
        buttons_.at(i)->defaultAction()->setIcon(QIcon(pieces.pixmap(piece, square)));
        buttons_.at(i)->setIconSize(QSize(square, square) * 0.85);
    }
    const QPoint origin = downwards ? top_left : top_left - QPoint(0, 3 * square);
    setGeometry(origin.x(), origin.y(), square, 4 * square);
    show();
    raise();
}

} // namespace chesscoach::app

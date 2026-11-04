// The evaluation bar and the board side by side (board_row.hpp).
#include "board_row.hpp"

#include <QResizeEvent>
#include <QSizePolicy>
#include <algorithm>

namespace chesscoach::app {

BoardRow::BoardRow(QWidget* bar, QWidget* board, QWidget* parent)
    : QWidget(parent), bar_(bar), board_(board) {
    bar_->setParent(this);
    board_->setParent(this);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void BoardRow::set_side(int side) {
    if (side != side_) {
        side_ = side;
        updateGeometry();
    }
}

int BoardRow::heightForWidth(int width) const { return std::max(0, width - bar_width - gap); }

QSize BoardRow::sizeHint() const { return {side_ + bar_width + gap, side_}; }

QSize BoardRow::minimumSizeHint() const { return {min_side + bar_width + gap, min_side}; }

void BoardRow::resizeEvent(QResizeEvent* /*event*/) {
    const int side = std::max(0, std::min(height(), width() - bar_width - gap));
    bar_->setGeometry(0, 0, bar_width, side);
    board_->setGeometry(bar_width + gap, 0, side, side);
}

} // namespace chesscoach::app

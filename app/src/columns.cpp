// The main window's content (columns.hpp).
#include "columns.hpp"

#include "board_row.hpp"

#include <QHBoxLayout>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <algorithm>

namespace chesscoach::app {

Columns::Columns(BoardRow* row, QWidget* move_list, QWidget* tabs, QWidget* screens,
                 QWidget* parent)
    : QWidget(parent), row_(row), move_list_(move_list), right_(new QVBoxLayout()) {
    auto* left = new QVBoxLayout();
    left->setSpacing(spacing);
    left->addWidget(row);
    left->addWidget(move_list, 1);
    screens->setMinimumWidth(screen_min_width);
    right_->setSpacing(spacing);
    right_->addWidget(tabs);
    right_->addWidget(screens, 1);
    auto* columns = new QHBoxLayout(this);
    columns->setContentsMargins(margin, margin, margin, margin);
    columns->setSpacing(margin);
    columns->addLayout(left);
    columns->addLayout(right_, 1);
}

void Columns::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const int by_height = height() - (2 * margin) - spacing - move_list_->minimumHeight();
    const int by_width = width() - (3 * margin) - right_->minimumSize().width() -
                         BoardRow::bar_width - BoardRow::gap;
    row_->set_side(std::max(BoardRow::min_side, std::min(by_height, by_width)));
}

} // namespace chesscoach::app

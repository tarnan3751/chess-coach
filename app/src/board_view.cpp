// The board (board_view.hpp).
#include "board_view.hpp"

#include "board_scene.hpp"
#include "promotion_chooser.hpp"

#include <QAbstractAnimation>
#include <QApplication>
#include <QEasingCurve>
#include <QGraphicsSvgItem>
#include <QMouseEvent>
#include <QPainter>
#include <QRectF>
#include <QResizeEvent>
#include <QVariantAnimation>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <utility>

namespace chesscoach::app {

BoardView::BoardView(QWidget* parent, const BoardTheme& theme)
    : QGraphicsView(parent), scene_(new BoardScene(theme, this)),
      chooser_(new PromotionChooser(viewport())), position_(Position::start()) {
    setObjectName(QStringLiteral("board"));
    setScene(scene_);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setMinimumSize(240, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(chooser_, &PromotionChooser::chosen, this, [this](char promotion) {
        if (!promotion_) {
            return;
        }
        const PendingPromotion pending = *promotion_;
        promotion_.reset();
        play(square_name(pending.from) + square_name(pending.to) + promotion, pending.animate);
    });
    refresh();
}

void BoardView::set_position(const Position& position) {
    stop_animations();
    cancel_promotion();
    position_ = position;
    selected_.reset();
    drag_.reset();
    last_move_.reset();
    refresh();
}

bool BoardView::play_move(const QString& uci) {
    const std::string text = uci.toStdString();
    const auto legal = position_.legal_moves_uci();
    if (std::ranges::find(legal, text) == legal.end()) {
        return false;
    }
    cancel_promotion();
    play(text, true);
    return true;
}

void BoardView::set_theme(const BoardTheme& theme) { scene_->set_theme(theme); }

void BoardView::flip() {
    stop_animations();
    cancel_promotion();
    scene_->set_flipped(!scene_->flipped());
}

bool BoardView::flipped() const { return scene_->flipped(); }

bool BoardView::animating() const {
    return std::ranges::any_of(animations_, [](const QPointer<QVariantAnimation>& animation) {
        return !animation.isNull() && animation->state() == QAbstractAnimation::Running;
    });
}

std::vector<std::string> BoardView::legal_moves_from(int square) const {
    const std::string from = square_name(square);
    std::vector<std::string> moves;
    for (std::string& move : position_.legal_moves_uci()) {
        if (move.starts_with(from)) {
            moves.push_back(std::move(move));
        }
    }
    return moves;
}

bool BoardView::is_target(int from, int to) const {
    const std::string to_name = square_name(to);
    return std::ranges::any_of(legal_moves_from(from), [&](const std::string& move) {
        return move.compare(2, 2, to_name) == 0;
    });
}

bool BoardView::own_piece_on(int square) const {
    const char piece = placement_of(position_).at(static_cast<std::size_t>(square));
    if (piece == 0) {
        return false;
    }
    const bool white = piece >= 'A' && piece <= 'Z';
    return white == (position_.side_to_move() == Color::white);
}

void BoardView::select(std::optional<int> square) {
    selected_ = square;
    std::vector<int> quiet;
    std::vector<int> captures;
    if (square) {
        const Placement placement = placement_of(position_);
        const char mover = placement.at(static_cast<std::size_t>(*square));
        for (const std::string& move : legal_moves_from(*square)) {
            const auto parsed = parse_uci(move);
            if (!parsed || std::ranges::find(quiet, parsed->to) != quiet.end() ||
                std::ranges::find(captures, parsed->to) != captures.end()) {
                continue; // the four promotions share their target
            }
            // A capture lands on a piece, or is a pawn's diagonal step onto the en passant
            // square, which is empty.
            const bool lands_on_piece = placement.at(static_cast<std::size_t>(parsed->to)) != 0;
            const bool en_passant =
                (mover == 'P' || mover == 'p') && file_of(parsed->from) != file_of(parsed->to);
            (lands_on_piece || en_passant ? captures : quiet).push_back(parsed->to);
        }
    }
    scene_->set_selection(square, quiet, captures);
}

void BoardView::try_move(int from, int to, bool animate) {
    const std::string prefix = square_name(from) + square_name(to);
    const std::vector<std::string> moves = legal_moves_from(from);
    const bool promotes = std::ranges::any_of(moves, [&](const std::string& move) {
        return move.size() == 5 && move.starts_with(prefix);
    });
    if (!promotes) {
        play(prefix, animate);
        return;
    }
    promotion_ = PendingPromotion{.from = from, .to = to, .animate = animate};
    select(std::nullopt);
    const QRectF target = mapFromScene(scene_->square_rect(to)).boundingRect();
    const int side = static_cast<int>(target.width());
    const bool downwards = target.top() < viewport()->height() / 2.0;
    chooser_->show_for(position_.side_to_move() == Color::white, scene_->pieces(),
                       target.topLeft().toPoint(), side, downwards);
}

void BoardView::play(const std::string& uci, bool animate) {
    const auto move = parse_uci(uci);
    if (!move) {
        return;
    }
    const Placement before = placement_of(position_);
    auto played = position_.play_uci(uci);
    if (!played) {
        refresh();
        return;
    }
    stop_animations();
    selected_.reset();
    last_move_ = std::pair{move->from, move->to};
    refresh();
    if (animate) {
        animate_piece(move->from, move->to);
        // Castling moves the rook too: the king's two-square step takes it from its corner to
        // the square the king crossed.
        const char mover = before.at(static_cast<std::size_t>(move->from));
        if ((mover == 'K' || mover == 'k') &&
            std::abs(file_of(move->to) - file_of(move->from)) == 2) {
            const int rank = rank_of(move->from);
            const bool king_side = file_of(move->to) > file_of(move->from);
            animate_piece(square_of(king_side ? 7 : 0, rank), square_of(king_side ? 5 : 3, rank));
        }
    }
    const QString san = QString::fromStdString(played->san);
    emit move_shown(san);
    emit move_played(QString::fromStdString(played->uci), san);
}

void BoardView::animate_piece(int from, int to) {
    QGraphicsSvgItem* item = scene_->piece_item(to);
    if (item == nullptr) {
        return;
    }
    auto* animation = new QVariantAnimation(this);
    animation->setDuration(animation_ms);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->setStartValue(scene_->square_rect(from).topLeft());
    animation->setEndValue(scene_->square_rect(to).topLeft());
    connect(animation, &QVariantAnimation::valueChanged, item,
            [item](const QVariant& value) { item->setPos(value.toPointF()); });
    item->setPos(scene_->square_rect(from).topLeft());
    animations_.emplace_back(animation);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void BoardView::stop_animations() {
    for (const QPointer<QVariantAnimation>& animation : animations_) {
        if (!animation.isNull()) {
            animation->disconnect();
            animation->stop();
        }
    }
    animations_.clear();
}

void BoardView::refresh() {
    const Placement placement = placement_of(position_);
    scene_->set_placement(placement);
    scene_->set_last_move(last_move_);
    scene_->set_check(position_.in_check() ? king_square(placement, position_.side_to_move())
                                           : std::nullopt);
    select(selected_);
}

void BoardView::cancel_promotion() {
    if (promotion_) {
        promotion_.reset();
        chooser_->hide();
        refresh();
    }
}

void BoardView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (promotion_) {
        cancel_promotion();
        return;
    }
    const QPointF point = mapToScene(event->position().toPoint());
    const auto square = scene_->square_at(point);
    if (!square) {
        select(std::nullopt);
        return;
    }
    if (selected_ && *selected_ != *square && is_target(*selected_, *square)) {
        try_move(*selected_, *square, true);
        return;
    }
    if (own_piece_on(*square)) {
        stop_animations();
        refresh();
        select(square);
        drag_ = Drag{.from = *square, .press = point, .moved = false};
        return;
    }
    select(std::nullopt);
}

void BoardView::mouseDoubleClickEvent(QMouseEvent* event) {
    // A quick second click is a click like any other.
    mousePressEvent(event);
}

void BoardView::mouseMoveEvent(QMouseEvent* event) {
    if (!drag_ || !(event->buttons() & Qt::LeftButton)) {
        return;
    }
    QGraphicsSvgItem* item = scene_->piece_item(drag_->from);
    if (item == nullptr) {
        return;
    }
    const QPointF point = mapToScene(event->position().toPoint());
    const QPoint travelled = mapFromScene(point) - mapFromScene(drag_->press);
    if (!drag_->moved && travelled.manhattanLength() < QApplication::startDragDistance()) {
        return;
    }
    drag_->moved = true;
    const qreal half = BoardScene::square_size / 2;
    item->setZValue(BoardScene::dragged_z);
    item->setPos(point - QPointF(half, half));
}

void BoardView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !drag_) {
        return;
    }
    const Drag drag = *drag_;
    drag_.reset();
    if (!drag.moved) {
        return; // a click: the piece stays picked for the second click
    }
    const auto square = scene_->square_at(mapToScene(event->position().toPoint()));
    if (square && *square != drag.from && is_target(drag.from, *square)) {
        try_move(drag.from, *square, false);
        return;
    }
    // Anywhere else the piece goes back where it came from.
    if (QGraphicsSvgItem* item = scene_->piece_item(drag.from)) {
        item->setZValue(BoardScene::piece_z);
        item->setPos(scene_->square_rect(drag.from).topLeft());
    }
    if (square != drag.from) {
        select(std::nullopt);
    }
}

void BoardView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}

} // namespace chesscoach::app

// The board's scene (board_scene.hpp).
#include "board_scene.hpp"

#include "piece_set.hpp"

#include <QBrush>
#include <QFont>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsSvgItem>
#include <QPen>
#include <QRadialGradient>
#include <QString>
#include <QSvgRenderer>
#include <QVariant>
#include <cmath>
#include <cstddef>

namespace chesscoach::app {

namespace {

// The data key under which a piece item keeps its FEN letter.
constexpr int piece_data_key = 0;

constexpr qreal board_size = 8 * BoardScene::square_size;

bool is_light(int square) { return (file_of(square) + rank_of(square)) % 2 == 1; }

std::size_t index(int square) { return static_cast<std::size_t>(square); }

} // namespace

BoardScene::BoardScene(const BoardTheme& theme, QObject* parent)
    : QGraphicsScene(parent), theme_(theme),
      pieces_(new PieceSet(QStringLiteral("cburnett"), this)), last_from_(new QGraphicsRectItem()),
      last_to_(new QGraphicsRectItem()), selected_item_(new QGraphicsRectItem()),
      check_item_(new QGraphicsRectItem()) {
    setSceneRect(0, 0, board_size, board_size);
    for (int square = 0; square < square_count; ++square) {
        auto* item = new QGraphicsRectItem();
        item->setPen(Qt::NoPen);
        item->setZValue(square_z);
        addItem(item);
        squares_.at(index(square)) = item;
    }
    for (QGraphicsRectItem* item : {last_from_, last_to_, selected_item_}) {
        item->setPen(Qt::NoPen);
        item->setZValue(highlight_z);
        item->setVisible(false);
        addItem(item);
    }
    check_item_->setPen(Qt::NoPen);
    check_item_->setZValue(highlight_z);
    check_item_->setVisible(false);
    addItem(check_item_);

    QFont font;
    font.setPixelSize(static_cast<int>(square_size * 0.18));
    font.setBold(true);
    for (int i = 0; i < 8; ++i) {
        auto* file = new QGraphicsSimpleTextItem(QString(QChar('a' + i)));
        auto* rank = new QGraphicsSimpleTextItem(QString(QChar('1' + i)));
        for (QGraphicsSimpleTextItem* label : {file, rank}) {
            label->setFont(font);
            label->setZValue(coordinate_z);
            addItem(label);
        }
        file_labels_.at(index(i)) = file;
        rank_labels_.at(index(i)) = rank;
    }
    set_theme(theme_);
}

void BoardScene::set_theme(const BoardTheme& theme) {
    theme_ = theme;
    for (int square = 0; square < square_count; ++square) {
        squares_.at(index(square))
            ->setBrush(is_light(square) ? theme_.light_square : theme_.dark_square);
    }
    last_from_->setBrush(theme_.last_move);
    last_to_->setBrush(theme_.last_move);
    selected_item_->setBrush(theme_.selected);
    QRadialGradient glow(QPointF(0.5, 0.5), 0.5);
    glow.setCoordinateMode(QGradient::ObjectBoundingMode);
    glow.setColorAt(0.0, theme_.check);
    glow.setColorAt(1.0, Qt::transparent);
    check_item_->setBrush(glow);
    layout(); // the coordinates and the hints take their colours there
}

void BoardScene::set_flipped(bool flipped) {
    flipped_ = flipped;
    layout();
}

QRectF BoardScene::square_rect(int square) const {
    const int column = flipped_ ? 7 - file_of(square) : file_of(square);
    const int row = flipped_ ? rank_of(square) : 7 - rank_of(square);
    return {column * square_size, row * square_size, square_size, square_size};
}

std::optional<int> BoardScene::square_at(QPointF point) const {
    if (point.x() < 0 || point.y() < 0 || point.x() >= board_size || point.y() >= board_size) {
        return std::nullopt;
    }
    const int column = static_cast<int>(std::floor(point.x() / square_size));
    const int row = static_cast<int>(std::floor(point.y() / square_size));
    return flipped_ ? square_of(7 - column, row) : square_of(column, 7 - row);
}

void BoardScene::set_placement(const Placement& placement) {
    for (QGraphicsSvgItem*& item : piece_items_) {
        delete item; // removes it from the scene
        item = nullptr;
    }
    for (int square = 0; square < square_count; ++square) {
        const char piece = placement.at(index(square));
        QSvgRenderer* renderer = pieces_->renderer(piece);
        if (renderer == nullptr) {
            continue;
        }
        auto* item = new QGraphicsSvgItem();
        item->setSharedRenderer(renderer);
        item->setScale(square_size / renderer->defaultSize().width());
        item->setData(piece_data_key, QVariant::fromValue(static_cast<int>(piece)));
        item->setZValue(piece_z);
        item->setPos(square_rect(square).topLeft());
        addItem(item);
        piece_items_.at(index(square)) = item;
    }
}

QGraphicsSvgItem* BoardScene::piece_item(int square) const {
    return piece_items_.at(index(square));
}

std::vector<QGraphicsSvgItem*> BoardScene::piece_items() const {
    std::vector<QGraphicsSvgItem*> items;
    for (QGraphicsSvgItem* item : piece_items_) {
        if (item != nullptr) {
            items.push_back(item);
        }
    }
    return items;
}

char BoardScene::piece_of(const QGraphicsSvgItem* item) {
    return static_cast<char>(item->data(piece_data_key).toInt());
}

void BoardScene::set_last_move(std::optional<std::pair<int, int>> move) {
    last_move_ = move;
    place_highlight(last_from_, move ? std::optional(move->first) : std::nullopt);
    place_highlight(last_to_, move ? std::optional(move->second) : std::nullopt);
}

void BoardScene::set_check(std::optional<int> king) {
    check_ = king;
    place_highlight(check_item_, king);
}

void BoardScene::set_selection(std::optional<int> square, const std::vector<int>& quiet_targets,
                               const std::vector<int>& capture_targets) {
    selected_ = square;
    place_highlight(selected_item_, square);
    for (const QGraphicsEllipseItem* item : hint_items_) {
        delete item;
    }
    hint_items_.clear();
    hints_.clear();
    hint_is_capture_.clear();
    for (const int target : quiet_targets) {
        hints_.push_back(target);
        hint_is_capture_.push_back(false);
    }
    for (const int target : capture_targets) {
        hints_.push_back(target);
        hint_is_capture_.push_back(true);
    }
    for (std::size_t i = 0; i < hints_.size(); ++i) {
        auto* item = new QGraphicsEllipseItem();
        item->setZValue(hint_z);
        addItem(item);
        hint_items_.push_back(item);
    }
    layout();
}

std::vector<int> BoardScene::hint_squares() const { return hints_; }

const QGraphicsSimpleTextItem* BoardScene::file_label(int file) const {
    return file_labels_.at(index(file));
}

const QGraphicsSimpleTextItem* BoardScene::rank_label(int rank) const {
    return rank_labels_.at(index(rank));
}

void BoardScene::place_highlight(QGraphicsRectItem* item, std::optional<int> square) const {
    item->setVisible(square.has_value());
    if (square) {
        item->setRect(square_rect(*square));
    }
}

void BoardScene::layout() {
    for (int square = 0; square < square_count; ++square) {
        squares_.at(index(square))->setRect(square_rect(square));
        if (QGraphicsSvgItem* item = piece_items_.at(index(square))) {
            item->setPos(square_rect(square).topLeft());
        }
    }
    place_highlight(last_from_, last_move_ ? std::optional(last_move_->first) : std::nullopt);
    place_highlight(last_to_, last_move_ ? std::optional(last_move_->second) : std::nullopt);
    place_highlight(selected_item_, selected_);
    place_highlight(check_item_, check_);

    // Files along the bottom row, in each square's bottom-right corner; ranks down the left
    // column, in each square's top-left corner; each in the colour of the other square shade.
    constexpr qreal margin = square_size * 0.04;
    const int bottom_rank = flipped_ ? 7 : 0;
    const int left_file = flipped_ ? 7 : 0;
    for (int i = 0; i < 8; ++i) {
        const int file_square = square_of(i, bottom_rank);
        QGraphicsSimpleTextItem* file = file_labels_.at(index(i));
        const QRectF file_rect = square_rect(file_square);
        file->setBrush(is_light(file_square) ? theme_.coordinate_on_light
                                             : theme_.coordinate_on_dark);
        file->setPos(file_rect.right() - file->boundingRect().width() - margin,
                     file_rect.bottom() - file->boundingRect().height());

        const int rank_square = square_of(left_file, i);
        QGraphicsSimpleTextItem* rank = rank_labels_.at(index(i));
        const QRectF rank_rect = square_rect(rank_square);
        rank->setBrush(is_light(rank_square) ? theme_.coordinate_on_light
                                             : theme_.coordinate_on_dark);
        rank->setPos(rank_rect.left() + margin, rank_rect.top());
    }

    // A dot a third of the square across on an empty target; a ring inside the square's edge
    // on a capture.
    for (std::size_t i = 0; i < hint_items_.size(); ++i) {
        QGraphicsEllipseItem* item = hint_items_.at(i);
        const QRectF rect = square_rect(hints_.at(i));
        if (hint_is_capture_.at(i)) {
            const qreal width = square_size * 0.08;
            item->setRect(rect.adjusted(width / 2, width / 2, -width / 2, -width / 2));
            item->setBrush(Qt::NoBrush);
            item->setPen(QPen(theme_.hint, width));
        } else {
            const qreal radius = square_size / 6;
            item->setRect(QRectF(rect.center() - QPointF(radius, radius),
                                 rect.center() + QPointF(radius, radius)));
            item->setBrush(theme_.hint);
            item->setPen(Qt::NoPen);
        }
    }
}

} // namespace chesscoach::app

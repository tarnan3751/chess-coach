// A piece set (piece_set.hpp).
#include "piece_set.hpp"

#include <QPainter>
#include <QSvgRenderer>
#include <algorithm>
#include <cstddef>
#include <string_view>

namespace chesscoach::app {

namespace {

// The FEN letters in the order of PieceSet's renderers.
constexpr std::string_view piece_letters = "KQRBNPkqrbnp";

} // namespace

PieceSet::PieceSet(const QString& name, QObject* parent) : QObject(parent) {
    for (std::size_t i = 0; i < piece_letters.size(); ++i) {
        const char piece = piece_letters.at(i);
        const bool white = piece >= 'A' && piece <= 'Z';
        const QChar type(white ? piece : static_cast<char>(piece - 'a' + 'A'));
        const QString file =
            QStringLiteral(":/pieces/%1/%2%3.svg")
                .arg(name, white ? QStringLiteral("w") : QStringLiteral("b"), QString(type));
        renderers_.at(i) = new QSvgRenderer(file, this);
    }
}

QSvgRenderer* PieceSet::renderer(char piece) const {
    const std::size_t index = piece_letters.find(piece);
    return index == std::string_view::npos ? nullptr : renderers_.at(index);
}

QPixmap PieceSet::pixmap(char piece, int size) const {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    if (QSvgRenderer* svg = renderer(piece)) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        svg->render(&painter);
    }
    return pixmap;
}

bool PieceSet::valid() const {
    return std::ranges::all_of(
        renderers_, [](const QSvgRenderer* svg) { return svg != nullptr && svg->isValid(); });
}

} // namespace chesscoach::app

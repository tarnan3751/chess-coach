// The themed icon loader (icons.hpp).
#include "icons.hpp"

#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QIODevice>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <Qt>
#include <utility>

namespace chesscoach::app {

QIcon tinted_icon(const QString& name, const QColor& normal, const QColor& disabled) {
    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray svg = file.readAll();
    QIcon icon;
    for (const auto& [mode, colour] :
         {std::pair{QIcon::Normal, normal}, std::pair{QIcon::Disabled, disabled}}) {
        QByteArray coloured = svg;
        coloured.replace("#000000", colour.name(QColor::HexRgb).toLatin1());
        QSvgRenderer renderer(coloured);
        for (const int size : {16, 32}) {
            QPixmap pixmap(size, size);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            renderer.render(&painter);
            painter.end();
            icon.addPixmap(pixmap, mode);
        }
    }
    return icon;
}

QIcon themed_icon(const QString& name, const StudyPalette& palette) {
    return tinted_icon(name, palette.text, palette.muted);
}

} // namespace chesscoach::app

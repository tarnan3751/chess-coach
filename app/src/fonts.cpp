// The bundled fonts (fonts.hpp).
#include "fonts.hpp"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStringList>

namespace chesscoach::app {

void install_fonts() {
    static const bool registered = [] {
        bool all = true;
        for (const QString& file : {
                 QStringLiteral(":/fonts/SourceSerif4-Semibold.ttf"),
                 QStringLiteral(":/fonts/SourceSans3-Regular.ttf"),
                 QStringLiteral(":/fonts/SourceSans3-Semibold.ttf"),
                 QStringLiteral(":/fonts/SourceSans3-Bold.ttf"),
             }) {
            all = QFontDatabase::addApplicationFont(file) >= 0 && all;
        }
        return all;
    }();
    static_cast<void>(registered); // a face that does not register falls back to the system's
    QFont font(sans_family);
    font.setPixelSize(body_pixel_size);
    QGuiApplication::setFont(font);
}

} // namespace chesscoach::app

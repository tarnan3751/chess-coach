// The application's look (application.hpp).
#include "application.hpp"

#include "fonts.hpp"

#include <QApplication>
#include <QString>

namespace chesscoach::app {

void configure_application() {
    // Fusion draws what the stylesheets cannot reach (radio buttons, spin boxes, scroll bars, the
    // tab strip's alignment) in the theme's palette, where the macOS style draws them in the
    // system's colours.
    QApplication::setStyle(QStringLiteral("Fusion"));
    install_fonts();
}

} // namespace chesscoach::app

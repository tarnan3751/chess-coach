// Unit tests of the Study look's resources (app; unit): the bundled fonts register from the Qt
// resource and become the application font, on an offscreen QGuiApplication, so no display and no
// system font is needed.
#include "fonts.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QtGlobal>
#include <array>

using chesscoach::app::install_fonts;

namespace {

// The process's application, offscreen, made once and never destroyed: Qt allows one per
// process, and one destroyed after main returns, when Qt's own statics are gone, crashes.
QGuiApplication& application() {
    static int argc = 1;
    static std::array<char, 15> name{"resources_test"};
    static std::array<char*, 2> argv{name.data(), nullptr};
    static QGuiApplication* const app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        return new QGuiApplication(argc, argv.data());
    }();
    return *app;
}

} // namespace

TEST_CASE("The bundled fonts register and Source Sans 3 is the application font", "[fonts]") {
    static_cast<void>(application());
    install_fonts();
    CHECK(QFontDatabase::hasFamily(chesscoach::app::serif_family));
    CHECK(QFontDatabase::hasFamily(chesscoach::app::sans_family));
    const QFont font = QGuiApplication::font();
    CHECK(font.family() == chesscoach::app::sans_family);
    CHECK(font.pixelSize() == chesscoach::app::body_pixel_size);
    // The faces asked for resolve to the bundled families, not a fallback.
    QFont serif(chesscoach::app::serif_family);
    serif.setWeight(QFont::DemiBold);
    CHECK(QFontInfo(serif).family() == chesscoach::app::serif_family);
    QFont bold(chesscoach::app::sans_family);
    bold.setWeight(QFont::Bold);
    CHECK(QFontInfo(bold).family() == chesscoach::app::sans_family);
}

// Unit tests of the Study look's resources (app; unit): the bundled fonts register from the Qt
// resource and become the application font, each stylesheet carries its palette's colours, and a
// themed icon is drawn in the palette's colours, on an offscreen QGuiApplication, so no display
// and no system font is needed.
#include "fonts.hpp"
#include "icons.hpp"
#include "study_palette.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QIODevice>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QtGlobal>
#include <array>
#include <utility>

using chesscoach::app::install_fonts;
using chesscoach::app::StudyPalette;
using chesscoach::app::themed_icon;

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

// Whether the image holds a pixel of exactly this colour, opaque.
bool holds(const QImage& image, const QColor& colour) {
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) == colour) {
                return true;
            }
        }
    }
    return false;
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

TEST_CASE("Each stylesheet carries its palette's colours", "[themes]") {
    for (const auto& [file, palette] : {
             std::pair{QStringLiteral(":/themes/light.qss"), &StudyPalette::light()},
             std::pair{QStringLiteral(":/themes/dark.qss"), &StudyPalette::dark()},
         }) {
        QFile stylesheet(file);
        REQUIRE(stylesheet.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(stylesheet.readAll());
        for (const QColor& colour : {
                 palette->window,
                 palette->chrome,
                 palette->panel,
                 palette->panel_alt,
                 palette->border,
                 palette->row_line,
                 palette->text,
                 palette->muted,
                 palette->accent,
                 palette->selection,
             }) {
            CHECK(text.contains(colour.name(QColor::HexRgb)));
        }
        CHECK(text.contains(QStringLiteral("rgba(%1, %1, %1, %2)")
                                .arg(palette->hover.red())
                                .arg(palette->hover.alpha())));
    }
}

TEST_CASE("A themed icon is drawn in the palette's text, and muted when disabled", "[icons]") {
    static_cast<void>(application());
    for (const StudyPalette* palette : {&StudyPalette::light(), &StudyPalette::dark()}) {
        const QIcon icon = themed_icon(QStringLiteral("plus"), *palette);
        REQUIRE(!icon.isNull());
        // At 32 px the plus's bars are 3.2 px wide, so pixels inside them take the stroke's colour
        // exactly.
        const QImage normal = icon.pixmap(QSize(32, 32), 1.0, QIcon::Normal).toImage();
        CHECK(normal.size() == QSize(32, 32));
        CHECK(holds(normal, palette->text));
        CHECK(!holds(normal, QColor(0, 0, 0)));
        const QImage disabled = icon.pixmap(QSize(32, 32), 1.0, QIcon::Disabled).toImage();
        CHECK(holds(disabled, palette->muted));
        CHECK(!holds(disabled, palette->text));
    }
    CHECK(themed_icon(QStringLiteral("no-such-icon"), StudyPalette::light()).isNull());
}

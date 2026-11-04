// chess-coach: the Study look's two palettes (study_palette.hpp). The values are the visual spec's
// tokens; the stylesheets in resources/ carry the same ones, by hand.
#include "study_palette.hpp"

namespace chesscoach::app {

namespace {

StudyPalette make_light() {
    StudyPalette p;
    p.window = QColor(0xf4, 0xee, 0xe2);
    p.chrome = QColor(0xeb, 0xe3, 0xd3);
    p.panel = QColor(0xfb, 0xf7, 0xee);
    p.panel_alt = QColor(0xf1, 0xea, 0xdb);
    p.border = QColor(0xd3, 0xc7, 0xb0);
    p.row_line = QColor(0xe6, 0xdc, 0xc8);
    p.text = QColor(0x2a, 0x25, 0x21);
    p.muted = QColor(0x6f, 0x66, 0x5a);
    p.hover = QColor(0x00, 0x00, 0x00, 0x0d);
    p.accent = QColor(0x2f, 0x6b, 0x4f);
    p.accent_text = QColor(0xff, 0xff, 0xff);
    p.selection = QColor(0xdf, 0xe8, 0xd9);
    p.book_bg = QColor(0xdf, 0xe8, 0xd9);
    p.book_fg = QColor(0x1f, 0x4a, 0x35);
    p.popular_bg = QColor(0xec, 0xe4, 0xd2);
    p.popular_fg = QColor(0x4a, 0x40, 0x36);
    p.dubious_bg = QColor(0xf1, 0xd9, 0xcf);
    p.dubious_fg = QColor(0x7a, 0x2e, 0x1a);
    p.result_white = QColor(0xf7, 0xf7, 0xf7);
    p.result_draw = QColor(0x9a, 0x9a, 0x9a);
    p.result_black = QColor(0x3b, 0x3b, 0x3b);
    p.result_white_text = QColor(0x33, 0x33, 0x33);
    p.result_black_text = QColor(0xee, 0xee, 0xee);
    return p;
}

StudyPalette make_dark() {
    StudyPalette p;
    p.window = QColor(0x1f, 0x1b, 0x17);
    p.chrome = QColor(0x2a, 0x25, 0x21);
    p.panel = QColor(0x27, 0x22, 0x1e);
    p.panel_alt = QColor(0x33, 0x2c, 0x26);
    p.border = QColor(0x41, 0x39, 0x30);
    p.row_line = QColor(0x36, 0x30, 0x2a);
    p.text = QColor(0xe8, 0xe1, 0xd4);
    p.muted = QColor(0xa8, 0x9e, 0x8e);
    p.hover = QColor(0xff, 0xff, 0xff, 0x0f);
    p.accent = QColor(0x7f, 0xb8, 0x9a);
    p.accent_text = QColor(0x16, 0x20, 0x1b);
    p.selection = QColor(0x2f, 0x4a, 0x3c);
    p.book_bg = QColor(0x27, 0x46, 0x3a);
    p.book_fg = QColor(0xb9, 0xe0, 0xc9);
    p.popular_bg = QColor(0x3a, 0x34, 0x2c);
    p.popular_fg = QColor(0xd9, 0xd0, 0xc0);
    p.dubious_bg = QColor(0x4a, 0x2a, 0x22);
    p.dubious_fg = QColor(0xf0, 0xb3, 0x9a);
    p.result_white = QColor(0xf7, 0xf7, 0xf7);
    p.result_draw = QColor(0x9a, 0x9a, 0x9a);
    p.result_black = QColor(0x3b, 0x3b, 0x3b);
    p.result_white_text = QColor(0x33, 0x33, 0x33);
    p.result_black_text = QColor(0xee, 0xee, 0xee);
    return p;
}

} // namespace

const StudyPalette& StudyPalette::light() {
    static const StudyPalette palette = make_light();
    return palette;
}

const StudyPalette& StudyPalette::dark() {
    static const StudyPalette palette = make_dark();
    return palette;
}

const StudyPalette& StudyPalette::named(const QString& theme) {
    return theme == QStringLiteral("dark") ? dark() : light();
}

} // namespace chesscoach::app

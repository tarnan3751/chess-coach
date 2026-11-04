// The board's colour presets (board_theme.hpp).
#include "board_theme.hpp"

namespace chesscoach::app {

BoardTheme BoardTheme::light() {
    // Walnut on paper: the last move, the pick and the hints in the accent's green.
    return BoardTheme{
        .light_square = QColor(0xec, 0xd6, 0xb1),
        .dark_square = QColor(0xa9, 0x77, 0x4f),
        .last_move = QColor(0x2f, 0x6b, 0x4f, 0x59),
        .selected = QColor(0x2f, 0x6b, 0x4f, 0x80),
        .check = QColor(0xff, 0x00, 0x00, 0xc0),
        .hint = QColor(0x1f, 0x4a, 0x35, 0x8c),
        .coordinate_on_light = QColor(0xa9, 0x77, 0x4f),
        .coordinate_on_dark = QColor(0xec, 0xd6, 0xb1),
    };
}

BoardTheme BoardTheme::dark() {
    // A darker walnut for the night, the greens the dark accent's.
    return BoardTheme{
        .light_square = QColor(0xd9, 0xc3, 0x9e),
        .dark_square = QColor(0x8f, 0x62, 0x40),
        .last_move = QColor(0x7f, 0xb8, 0x9a, 0x59),
        .selected = QColor(0x7f, 0xb8, 0x9a, 0x80),
        .check = QColor(0xff, 0x40, 0x40, 0xc0),
        .hint = QColor(0x1f, 0x4a, 0x35, 0x8c),
        .coordinate_on_light = QColor(0x8f, 0x62, 0x40),
        .coordinate_on_dark = QColor(0xd9, 0xc3, 0x9e),
    };
}

} // namespace chesscoach::app

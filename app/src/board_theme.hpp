// The board's colours: squares, highlights, hints and coordinates, with a light and a dark
// preset, the Study look's walnut board (docs/reference/app.md, **Themes**). The board reads one
// of these; switching between them at run time comes with the settings (docs/reference/app.md,
// **Board**).
#pragma once

#include <QColor>

namespace chesscoach::app {

struct BoardTheme {
    QColor light_square;
    QColor dark_square;
    QColor last_move; // laid over the last move's from and to squares
    QColor selected;  // laid over the square of the picked piece
    QColor check;     // the centre of the glow on the king in check
    QColor hint;      // the dots on empty targets and the rings on captures
    QColor coordinate_on_light;
    QColor coordinate_on_dark;

    [[nodiscard]] static BoardTheme light();
    [[nodiscard]] static BoardTheme dark();
};

} // namespace chesscoach::app

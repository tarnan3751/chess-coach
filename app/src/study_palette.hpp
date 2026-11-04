// chess-coach: the Study look's colours, one set per theme (docs/reference/app.md, **Themes**).
// Widgets take their colours from the theme's stylesheet; delegates and painted widgets, which
// a stylesheet cannot reach, read the same colours here, so the two never drift. The board's own
// colours are BoardTheme's. Presentation only: no I/O, no Qt outside app.
#pragma once

#include <QColor>
#include <QString>

namespace chesscoach::app {

struct StudyPalette {
    QColor window;    // the window's background: paper by day, brown-black by night
    QColor chrome;    // the toolbar, the status bar, a table header
    QColor panel;     // a panel, a table body, an input
    QColor panel_alt; // a chip, an alternate row
    QColor border;    // every hairline
    QColor row_line;  // the line between table rows, fainter than a hairline
    QColor text;
    QColor muted;                  // secondary text: labels, move numbers, the status bar
    QColor hover;                  // laid over a toolbar button under the pointer
    QColor accent;                 // the deep green: the active tab's rule, an evaluation, a switch
    QColor accent_text;            // text on the accent
    QColor selection;              // the selected row, the current move
    QColor book_bg, book_fg;       // the Book pill
    QColor popular_bg, popular_fg; // the Popular pill
    QColor dubious_bg, dubious_fg; // the Dubious pill
    QColor result_white, result_draw, result_black; // the results bar's segments
    QColor result_white_text, result_black_text;    // the percentages inside them

    [[nodiscard]] static const StudyPalette& light();
    [[nodiscard]] static const StudyPalette& dark();
    // The palette of a theme name as Settings stores it: "dark" is dark, anything else light.
    [[nodiscard]] static const StudyPalette& named(const QString& theme);
};

} // namespace chesscoach::app

// The toolbar's icons (docs/reference/app.md, **Themes**): our own, 16 by 16, a 1.6 px stroke with
// round caps and joins and no fills, drawn in #000000, one file per icon under :/icons/<name>.svg
// (plus, back, flip, chevron-down, gear, sound-on, sound-off, explore, spar, repertoire, drill).
// A theme recolours them as it loads them, so one file serves both themes. GUI thread only.
#pragma once

#include "study_palette.hpp"

#include <QColor>
#include <QIcon>
#include <QString>

namespace chesscoach::app {

// The icon :/icons/<name>.svg with its #000000 drawn in the normal colour, and in the disabled
// one when disabled, rendered at 16 and 32 px; a null icon when there is no such file.
[[nodiscard]] QIcon tinted_icon(const QString& name, const QColor& normal, const QColor& disabled);
// The icon in the palette's text colour, and in its muted colour when disabled.
[[nodiscard]] QIcon themed_icon(const QString& name, const StudyPalette& palette);

} // namespace chesscoach::app

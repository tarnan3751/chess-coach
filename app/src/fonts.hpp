// The Study look's type (docs/reference/app.md, **Themes**): Source Serif 4 SemiBold for the
// headings and the screen names, Source Sans 3 Regular, SemiBold and Bold for everything else,
// static TTFs compiled in under :/fonts/ (Adobe's, SIL Open Font License 1.1; NOTICE), so the
// app needs no font on the system. GUI thread only.
#pragma once

#include <QLatin1String>

namespace chesscoach::app {

// The two families as the bundled files name them.
inline constexpr QLatin1String serif_family{"Source Serif 4"};
inline constexpr QLatin1String sans_family{"Source Sans 3"};
// The body size, in pixels.
inline constexpr int body_pixel_size = 13;

// Registers the four bundled faces with QFontDatabase, once per process, and makes Source Sans 3
// at body_pixel_size the application font. Needs a QGuiApplication.
void install_fonts();

} // namespace chesscoach::app

// The application's look, set once before any window or dialog opens (docs/reference/app.md,
// **Themes**): the Fusion style on every platform and the bundled fonts, Source Sans 3 at 13 px
// the application font. main.cpp calls it, and every UI test's initTestCase, so that what a test
// opens looks as the app does. The theme's palette and stylesheet are the window's (apply_theme).
// GUI thread only, with a QApplication.
#pragma once

namespace chesscoach::app {

void configure_application();

} // namespace chesscoach::app

// The app's unit tests' QApplication (app; unit): one per test process, on the offscreen platform,
// so a unit test needs no display, made at the first call and never destroyed, as one destroyed
// after main returns, when Qt's own statics are gone, crashes. Catch2 owns main, so the tests ask
// for it rather than QTEST_MAIN making it.
#pragma once

#include <QApplication>
#include <QtGlobal>
#include <array>

namespace chesscoach::app::test {

inline QApplication& offscreen_application() {
    static int argc = 1;
    static std::array<char, 9> name{"app_test"};
    static std::array<char*, 2> argv{name.data(), nullptr};
    static QApplication* const app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        return new QApplication(argc, argv.data());
    }();
    return *app;
}

} // namespace chesscoach::app::test

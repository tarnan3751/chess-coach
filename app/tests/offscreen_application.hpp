// The app's unit tests' QApplication (app; unit): one per test process, on the offscreen platform,
// so a unit test needs no display, made at the first call and destroyed when Catch2's run ends,
// before main returns, since one destroyed after main returns, when Qt's own statics are gone,
// crashes, and one never destroyed leaves the font database and its fontconfig allocations to
// LeakSanitizer on Linux. Catch2 owns main, so the tests ask for it rather than QTEST_MAIN making
// it; the listener below is registered once per test executable, which includes this header from
// one translation unit.
#pragma once

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QtGlobal>
#include <array>

namespace chesscoach::app::test {

namespace detail {

inline QApplication*& application_slot() {
    static QApplication* app = nullptr;
    return app;
}

} // namespace detail

inline QApplication& offscreen_application() {
    static int argc = 1;
    static std::array<char, 9> name{"app_test"};
    static std::array<char*, 2> argv{name.data(), nullptr};
    QApplication*& app = detail::application_slot();
    if (app == nullptr) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        app = new QApplication(argc, argv.data());
    }
    return *app;
}

// Destroys the application when the whole run is over, so Qt tears its font database down while
// its statics still live and the leak check at exit finds nothing of Qt's.
class OffscreenApplicationListener : public Catch::EventListenerBase {
  public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunEnded(const Catch::TestRunStats& /*stats*/) override {
        QApplication*& app = detail::application_slot();
        delete app;
        app = nullptr;
    }
};

} // namespace chesscoach::app::test

CATCH_REGISTER_LISTENER(chesscoach::app::test::OffscreenApplicationListener)

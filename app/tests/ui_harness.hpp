// The UI harness's helpers for the Qt Test classes under app/tests/ (docs/reference/app.md,
// **UI harness**): screenshots saved as <test>-<state>.png in the build's ui-screenshots
// directory (CHESSCOACH_UI_SCREENSHOT_DIR, set by app/CMakeLists.txt), and the viewport point at
// the centre of a board square, where a test clicks. Including it makes the tests silent: it sets
// CHESS_COACH_SILENT before main, so every window's sounds go to the null backend, which a test
// reads for what the window played; unless the includer is built with CHESSCOACH_HARNESS_SOUNDS
// defined, which leaves the choice to its own main (app_measure's --sounds).
#pragma once

#include "board_scene.hpp"
#include "board_view.hpp"
#include "sounds.hpp"
#include "squares.hpp"

#include <QDir>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QVariant>
#include <QWidget>
#include <QtGlobal>
#include <string_view>
#include <utility>
#include <vector>

namespace chesscoach::app::test {

#ifndef CHESSCOACH_HARNESS_SOUNDS
// Set at static initialisation, before any test makes a window (sounds.hpp); qputenv fails only
// for want of memory, before main, which ends the run as it should.
// NOLINTNEXTLINE(bugprone-throwing-static-initialization,cert-err58-cpp)
inline const bool silent = qputenv("CHESS_COACH_SILENT", "1");
#endif

// The descendant of parent with the object name. A missing one is a mistake in the test or the
// window rather than a failed check, so it stops the run (QTEST_ASSERT), and the result is never
// null.
template <typename T> T& child(const QObject& parent, const QString& name) {
    T* found = parent.findChild<T*>(name);
    QTEST_ASSERT(found != nullptr);
    return *found;
}

// What the sounds played: their null backend, which CHESS_COACH_SILENT chose. A window's are
// MainWindow::sounds().
inline const NullSoundBackend& played(const Sounds& sounds) {
    const NullSoundBackend* null = sounds.null_backend();
    QTEST_ASSERT(null != nullptr);
    return *null;
}

// Saves the widget's rendering as <test>-<state>.png; fails the test when it cannot.
inline void save_screenshot(QWidget& widget, const QString& test, const QString& state) {
    const QDir dir(QStringLiteral(CHESSCOACH_UI_SCREENSHOT_DIR));
    QVERIFY(dir.mkpath(QStringLiteral(".")));
    const QString file = dir.filePath(test + QLatin1Char('-') + state + QStringLiteral(".png"));
    QVERIFY2(widget.grab().save(file), qPrintable(file));
}

// The square of a name such as "e4"; the tests spell their squares correctly.
inline int square(std::string_view name) { return square_from_name(name).value_or(0); }

// The centre of a square in the board's viewport coordinates.
inline QPoint square_point(const BoardView& view, std::string_view name) {
    return view.mapFromScene(view.board_scene().square_rect(square(name)).center());
}

// Clicks a square of the board.
inline void click(const BoardView& view, std::string_view name) {
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::KeyboardModifiers(),
                      square_point(view, name));
}

// The moves a spy on BoardView::move_played recorded, as (UCI, SAN) pairs, taken from it.
using Moves = std::vector<std::pair<QString, QString>>;
inline Moves taken(QSignalSpy& spy) {
    Moves moves;
    while (!spy.isEmpty()) {
        const QList<QVariant> arguments = spy.takeFirst();
        moves.emplace_back(arguments.at(0).toString(), arguments.at(1).toString());
    }
    return moves;
}

} // namespace chesscoach::app::test

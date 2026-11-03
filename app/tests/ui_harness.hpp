// The UI harness's helpers for the Qt Test classes under app/tests/ (docs/reference/app.md,
// **UI harness**): screenshots saved as <test>-<state>.png in the build's ui-screenshots
// directory (CHESSCOACH_UI_SCREENSHOT_DIR, set by app/CMakeLists.txt), and the viewport point at
// the centre of a board square, where a test clicks.
#pragma once

#include "board_scene.hpp"
#include "board_view.hpp"
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
#include <string_view>
#include <utility>
#include <vector>

namespace chesscoach::app::test {

// The descendant of parent with the object name. A missing one is a mistake in the test or the
// window rather than a failed check, so it stops the run (QTEST_ASSERT), and the result is never
// null.
template <typename T> T& child(const QObject& parent, const QString& name) {
    T* found = parent.findChild<T*>(name);
    QTEST_ASSERT(found != nullptr);
    return *found;
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

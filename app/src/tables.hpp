// The read-only tables the screens list things in (docs/reference/app.md, **Spar**,
// **Repertoire**): headers, no row numbers, whole rows selected one at a time, columns fitted to
// their contents, and cells that cannot be edited. GUI thread only.
#pragma once

#include <QStringList>

class QString;
class QTableWidget;
class QTableWidgetItem;
class QWidget;

namespace chesscoach::app {

[[nodiscard]] QTableWidget* make_table(const QStringList& headers, QWidget* parent);
// A cell the user may select but not edit; the table takes it.
[[nodiscard]] QTableWidgetItem* table_item(const QString& text);

} // namespace chesscoach::app

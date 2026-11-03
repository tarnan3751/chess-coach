// The screens' read-only tables (tables.hpp).
#include "tables.hpp"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>

namespace chesscoach::app {

QTableWidget* make_table(const QStringList& headers, QWidget* parent) {
    auto* widget = new QTableWidget(0, static_cast<int>(headers.size()), parent);
    widget->setHorizontalHeaderLabels(headers);
    widget->verticalHeader()->hide();
    widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    widget->setSelectionBehavior(QAbstractItemView::SelectRows);
    widget->setSelectionMode(QAbstractItemView::SingleSelection);
    widget->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    return widget;
}

QTableWidgetItem* table_item(const QString& text) {
    auto* cell = new QTableWidgetItem(text);
    cell->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return cell;
}

} // namespace chesscoach::app

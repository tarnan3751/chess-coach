// The screens' read-only tables and the Study table look (tables.hpp).
#include "tables.hpp"

#include <QLatin1Char>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QFontMetrics>
#include <QHeaderView>
#include <QPaintDevice>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVariant>

namespace chesscoach::app {

namespace {

// A header label's padding above and below.
constexpr int header_padding_vertical = 6;

} // namespace

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

QFont sized_font(QFont font, qreal pixels, QFont::Weight weight, const QPaintDevice& device) {
    font.setPointSizeF(pixels * 72.0 / static_cast<qreal>(device.logicalDpiY()));
    font.setWeight(weight);
    return font;
}

QFont tabular(const QFont& font) {
    QFont result = font;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    result.setFeature(QFont::Tag("tnum"), 1);
#endif
    return result;
}

void paint_row(QPainter& painter, const QStyleOptionViewItem& option, const StudyPalette& palette) {
    if (option.state.testFlag(QStyle::State_Selected)) {
        painter.fillRect(option.rect, palette.selection);
    }
    const auto* view = qobject_cast<const QAbstractItemView*>(option.widget);
    const bool at_frame = view != nullptr && option.rect.bottom() + 1 >= view->viewport()->height();
    if (!at_frame) {
        painter.setPen(palette.row_line);
        painter.drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
    }
}

TableHeader::TableHeader(const StudyPalette* palette, QWidget* parent)
    : QHeaderView(Qt::Horizontal, parent), palette_(palette) {}

QFont TableHeader::label_font() const { return sized_font(font(), 11.5, QFont::DemiBold, *this); }

QFont TableHeader::note_font() const { return sized_font(font(), 11.5, QFont::Normal, *this); }

void TableHeader::paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const {
    if (!rect.isValid() || model() == nullptr) {
        return;
    }
    const QString label = model()->headerData(logicalIndex, orientation()).toString();
    const QString note =
        model()->headerData(logicalIndex, orientation(), header_note_role).toString();
    const bool right = model()
                           ->headerData(logicalIndex, orientation(), Qt::TextAlignmentRole)
                           .value<Qt::Alignment>()
                           .testFlag(Qt::AlignRight);
    const QFont bold = label_font();
    const QFont regular = note_font();
    const QString spaced_note = note.isEmpty() ? QString() : QLatin1Char(' ') + note;
    const int label_width = QFontMetrics(bold).horizontalAdvance(label);
    const int width = label_width + QFontMetrics(regular).horizontalAdvance(spaced_note);
    const QRect text = rect.adjusted(cell_padding, 0, -cell_padding, -1);
    const int left = right ? text.right() + 1 - width : text.left();

    painter->save();
    painter->setClipRect(rect, Qt::IntersectClip);
    painter->fillRect(rect, palette_->chrome);
    painter->setPen(palette_->border);
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());
    painter->setPen(palette_->muted);
    painter->setFont(bold);
    painter->drawText(QRect(left, text.top(), label_width, text.height()),
                      text_flags(Qt::AlignLeft | Qt::AlignVCenter), label);
    if (!spaced_note.isEmpty()) {
        painter->setFont(regular);
        painter->drawText(QRect(left + label_width, text.top(), width - label_width, text.height()),
                          text_flags(Qt::AlignLeft | Qt::AlignVCenter), spaced_note);
    }
    painter->restore();
}

QSize TableHeader::sectionSizeFromContents(int logicalIndex) const {
    if (model() == nullptr) {
        return QHeaderView::sectionSizeFromContents(logicalIndex);
    }
    const QString label = model()->headerData(logicalIndex, orientation()).toString();
    const QString note =
        model()->headerData(logicalIndex, orientation(), header_note_role).toString();
    const QFontMetrics bold(label_font());
    int width = bold.horizontalAdvance(label) + (2 * cell_padding);
    if (!note.isEmpty()) {
        width += QFontMetrics(note_font()).horizontalAdvance(QLatin1Char(' ') + note);
    }
    // The hairline under the label is the section's last pixel row.
    return {width, bold.height() + (2 * header_padding_vertical) + 1};
}

} // namespace chesscoach::app

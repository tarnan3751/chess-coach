// The read-only tables the screens list things in (docs/reference/app.md, **Spar**,
// **Repertoire**, **Tables**): headers, no row numbers, whole rows selected one at a time,
// columns fitted to their contents, and cells that cannot be edited. With them, the Study look's
// parts for a table that paints itself, as Explore's does: the header, a row's selection and
// rule, and type at the spec's sizes in pixels, in the colours of a StudyPalette its owner keeps.
// GUI thread only.
#pragma once

#include "study_palette.hpp"

#include <QFont>
#include <QHeaderView>
#include <QSize>
#include <QStringList>
#include <QtGlobal>

class QPaintDevice;
class QPainter;
class QRect;
class QString;
class QStyleOptionViewItem;
class QTableWidget;
class QTableWidgetItem;
class QWidget;

namespace chesscoach::app {

[[nodiscard]] QTableWidget* make_table(const QStringList& headers, QWidget* parent);
// A cell the user may select but not edit; the table takes it.
[[nodiscard]] QTableWidgetItem* table_item(const QString& text);

// A Study table cell's padding: left and right, and above and below.
inline constexpr int cell_padding = 10;
inline constexpr int cell_padding_vertical = 5;

// The font at a size in pixels on the device, a fraction of a pixel included (QFont takes whole
// pixels, so the size goes through points at the device's logical DPI), at a weight.
[[nodiscard]] QFont sized_font(QFont font, qreal pixels, QFont::Weight weight,
                               const QPaintDevice& device);
// The font with tabular figures, so that numbers in a column line up. Qt asks the font for them
// from 6.7 (QFont::setFeature); before that the font's own figures stay.
[[nodiscard]] QFont tabular(QFont font);

// An alignment as QPainter::drawText's flags.
[[nodiscard]] inline int text_flags(Qt::Alignment alignment) {
    return static_cast<int>(alignment.toInt());
}

// Paints a cell's part of its row: the selection's fill when the row is selected, and the rule
// under the row, but not where the row meets the table's frame, which is a line already. The
// painter's pen changes.
void paint_row(QPainter& painter, const QStyleOptionViewItem& option, const StudyPalette& palette);

// A header section's note, which TableHeader paints after the label in the regular weight.
inline constexpr int header_note_role = Qt::UserRole + 3;

// A horizontal header painting its sections in the Study look: the chrome fill with a hairline
// under it, the label in 11.5 px semibold muted, left or right as the model's TextAlignmentRole
// for the section says, and its note (header_note_role) after it. The sections' tooltips are the
// model's Qt::ToolTipRole, which QHeaderView shows itself.
class TableHeader : public QHeaderView {
  public:
    // palette is the owner's, which outlives the header.
    TableHeader(const StudyPalette* palette, QWidget* parent);

  protected:
    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override;
    [[nodiscard]] QSize sectionSizeFromContents(int logicalIndex) const override;

  private:
    [[nodiscard]] QFont label_font() const;
    [[nodiscard]] QFont note_font() const;

    const StudyPalette* palette_;
};

} // namespace chesscoach::app

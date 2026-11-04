// The Study look's cells of the move tables (docs/reference/app.md, **Tables**). The results bar:
// white wins, draws and black wins side by side in a hairline frame, 150 × 16 px, the white and
// black percentages inside their segments and the draws' only in a segment 18 px wide or more,
// from the cell's shares_role data (a QList<double> of three fractions summing to 1). The badge
// pills: Book, Popular and Dubious by name in their colours, or a muted dash for none, from the
// cell's badges_role data (the bits, as Badges::raw holds them), each pill's tooltip the sentence
// saying what its badge means (wording.hpp's badge_sentence). StudyCellDelegate paints a cell of
// either kind, a move or a number, with its row's selection and rule: Explore's move table and
// Practice's better choices paint every cell through it. GUI thread only.
#pragma once

#include <chesscoach/badges.hpp>

#include "study_palette.hpp"

#include <QFont>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStyledItemDelegate>
#include <QtGlobal>
#include <cstdint>
#include <vector>

class QAbstractItemView;
class QHelpEvent;
class QModelIndex;
class QPaintDevice;
class QPainter;
class QStyleOptionViewItem;

namespace chesscoach::app {

// The roles under which a cell holds its three shares and its badges.
inline constexpr int shares_role = Qt::UserRole + 1;
inline constexpr int badges_role = Qt::UserRole + 2;

// The results bar's size, its frame included.
inline constexpr QSize results_bar_size{150, 16};

// The word a badge's pill reads.
[[nodiscard]] QString badge_word(Badge badge);
// The Results header's note: "white · draw · black".
[[nodiscard]] QString results_note();

// A badge's pill and where it lies.
struct Pill {
    Badge badge = Badge::book;
    QRect rect;
};
// The pills of the badges in a cell, in order, after its left padding and centred in its height
// above the row's rule, in the font (the table's) on the device.
[[nodiscard]] std::vector<Pill> badge_pills(const QRect& cell, Badges badges, const QFont& font,
                                            const QPaintDevice& device);
// A tooltip event over a badges cell: the sentence of the pill under the pointer, or none.
bool show_pill_tooltip(const QHelpEvent& event, const QAbstractItemView& view,
                       const QStyleOptionViewItem& option, const QModelIndex& index);

// Paints the results bar in the cell, after its left padding and centred in its height above the
// row's rule; nothing unless there are three shares.
void paint_results_bar(QPainter& painter, const QStyleOptionViewItem& option,
                       const QList<double>& shares, const StudyPalette& palette);
// Paints the badges' pills in the cell, after its left padding, 4 px apart, centred in its height
// above the row's rule; a muted "–" when there is none.
void paint_badge_pills(QPainter& painter, const QStyleOptionViewItem& option, Badges badges,
                       const StudyPalette& palette);
// The width the pills take, the cell's padding included, and a pill's height, in the font (the
// table's) on the device.
[[nodiscard]] int badge_pills_width(Badges badges, const QFont& font, const QPaintDevice& device);
[[nodiscard]] int badge_pill_height(const QFont& font, const QPaintDevice& device);

// A Study table row's height in the font (the table's) on the device: the tallest of a bold line,
// a pill and the results bar, with the cell's padding and the rule under it.
[[nodiscard]] int study_row_height(const QFont& font, const QPaintDevice& device);

// A Study table's cell (docs/reference/app.md, **Tables**): its row's selection and rule, then its
// kind's content: a move in bold, a number right-aligned in tabular figures, the badges' pills,
// whose tooltips say what they mean, or the results bar.
class StudyCellDelegate : public QStyledItemDelegate {
  public:
    enum class Kind : std::uint8_t { move, number, badges, results };

    // palette is the owner's, which outlives the delegate.
    StudyCellDelegate(Kind kind, const StudyPalette* palette, QObject* parent);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;
    // Over a pill, its badge's sentence; elsewhere the cell's tooltip, if any.
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                   const QModelIndex& index) override;

  private:
    [[nodiscard]] QFont font_of(const QStyleOptionViewItem& option) const;

    Kind kind_;
    const StudyPalette* palette_;
};

} // namespace chesscoach::app

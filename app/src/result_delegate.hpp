// The Study look's cells of the move tables (docs/reference/app.md, **Tables**). The results bar:
// white wins, draws and black wins side by side in a hairline frame, 150 × 16 px, the white and
// black percentages inside their segments and the draws' only in a segment 18 px wide or more,
// from the cell's shares_role data (a QList<double> of three fractions summing to 1). The badge
// pills: Book, Popular and Dubious by name in their colours, or a muted dash for none, from the
// cell's badges_role data (the bits, as Badges::raw holds them). ResultDelegate is the bar as a
// delegate of its own, as Spar's better choices use it; Explore's table paints both through its
// own delegate. GUI thread only.
#pragma once

#include <chesscoach/badges.hpp>

#include "study_palette.hpp"

#include <QFont>
#include <QList>
#include <QSize>
#include <QString>
#include <QStyledItemDelegate>
#include <QtGlobal>

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

class ResultDelegate : public QStyledItemDelegate {
  public:
    // palette is the owner's, which outlives the delegate; nullptr for the light theme's.
    explicit ResultDelegate(QObject* parent, const StudyPalette* palette = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

  private:
    const StudyPalette* palette_;
};

} // namespace chesscoach::app

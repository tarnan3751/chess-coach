// The results bar and the badge pills (result_delegate.hpp).
#include "result_delegate.hpp"

#include "tables.hpp"

#include <QAbstractItemView>
#include <QColor>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QModelIndex>
#include <QPaintDevice>
#include <QPainter>
#include <QRect>
#include <QRectF>
#include <QStringList>
#include <QStyleOptionViewItem>
#include <QToolTip>
#include <QVariant>
#include <array>
#include <cstddef>

namespace chesscoach::app {

namespace {

constexpr std::array badge_order{Badge::book, Badge::popular, Badge::dubious};
// A pill: its padding left and right, and above and below; the gap between two; its corners.
constexpr int pill_padding = 7;
constexpr int pill_padding_vertical = 1;
constexpr int pill_gap = 4;
constexpr qreal corner_radius = 2;
// The type sizes in pixels: the pills', and the bar's percentages.
constexpr qreal pill_pixels = 11.5;
constexpr qreal bar_label_pixels = 10;
// The narrowest draws segment that holds its percentage.
constexpr int draw_label_width = 18;

struct PillColours {
    QColor background;
    QColor text;
};

PillColours colours_of(Badge badge, const StudyPalette& palette) {
    switch (badge) {
    case Badge::book:
        return {.background = palette.book_bg, .text = palette.book_fg};
    case Badge::popular:
        return {.background = palette.popular_bg, .text = palette.popular_fg};
    case Badge::dubious:
        return {.background = palette.dubious_bg, .text = palette.dubious_fg};
    }
    return {.background = palette.panel_alt, .text = palette.text};
}

QFont pill_font(const QFont& font, const QPaintDevice& device) {
    return sized_font(font, pill_pixels, QFont::DemiBold, device);
}

// A cell's content: the padding off left and right, the row's rule off the bottom.
QRect content_of(const QRect& cell) { return cell.adjusted(cell_padding, 0, -cell_padding, -1); }

QString none_mark() { return QStringLiteral("–"); }

QString translate(const char* text) {
    return QCoreApplication::translate("chesscoach::app::Badges", text);
}

Badges badges_of(const QModelIndex& index) {
    return Badges(static_cast<std::uint8_t>(index.data(badges_role).toUInt()));
}

} // namespace

QString badge_word(Badge badge) {
    switch (badge) {
    case Badge::book:
        return translate("Book");
    case Badge::popular:
        return translate("Popular");
    case Badge::dubious:
        return translate("Dubious");
    }
    return {};
}

QString badge_sentence(Badge badge) {
    switch (badge) {
    case Badge::book:
        return translate("Book: what strong players (2200 and above) choose.");
    case Badge::popular:
        return translate("Popular: what players at your rating often choose.");
    case Badge::dubious:
        return translate("Dubious: popular at your rating, but it loses ground.");
    }
    return {};
}

QString badges_explained() {
    QStringList sentences;
    for (const Badge badge : badge_order) {
        sentences << badge_sentence(badge);
    }
    return sentences.join(QLatin1Char(' '));
}

QString results_note() { return translate("white · draw · black"); }

std::vector<Pill> badge_pills(const QRect& cell, Badges badges, const QFont& font,
                              const QPaintDevice& device) {
    const QRect content = content_of(cell);
    const QFontMetrics metrics(pill_font(font, device));
    const int height = metrics.height() + (2 * pill_padding_vertical);
    const int top = content.top() + ((content.height() - height) / 2);
    std::vector<Pill> pills;
    int left = content.left();
    for (const Badge badge : badge_order) {
        if (badges.has(badge)) {
            const QRect rect(left, top,
                             metrics.horizontalAdvance(badge_word(badge)) + (2 * pill_padding),
                             height);
            pills.push_back({.badge = badge, .rect = rect});
            left += rect.width() + pill_gap;
        }
    }
    return pills;
}

bool show_pill_tooltip(const QHelpEvent& event, const QAbstractItemView& view,
                       const QStyleOptionViewItem& option, const QModelIndex& index) {
    for (const Pill& pill :
         badge_pills(option.rect, badges_of(index), option.font, *view.viewport())) {
        if (pill.rect.contains(event.pos())) {
            QToolTip::showText(event.globalPos(), badge_sentence(pill.badge), view.viewport(),
                               pill.rect);
            return true;
        }
    }
    QToolTip::hideText();
    return true;
}

void paint_results_bar(QPainter& painter, const QStyleOptionViewItem& option,
                       const QList<double>& shares, const StudyPalette& palette) {
    if (shares.size() != 3) {
        return;
    }
    const QRect content = content_of(option.rect);
    const QRect bar(content.left(),
                    content.top() + ((content.height() - results_bar_size.height()) / 2),
                    results_bar_size.width(), results_bar_size.height());
    const QRect inside = bar.adjusted(1, 1, -1, -1);
    // The segments' edges in whole pixels, so that they meet without a gap.
    const double first = shares.at(0);
    const double second = first + shares.at(1);
    const std::array edges{
        inside.left(),
        inside.left() + qRound(inside.width() * first),
        inside.left() + qRound(inside.width() * second),
        inside.left() + inside.width(),
    };
    const std::array fills{palette.result_white, palette.result_draw, palette.result_black};
    // The draws' percentage reads in the white segment's colour, dark on the grey.
    const std::array texts{
        palette.result_white_text,
        palette.result_white_text,
        palette.result_black_text,
    };
    const std::array narrowest{0, draw_label_width, 0};
    painter.setFont(sized_font(option.font, bar_label_pixels, QFont::Bold, *painter.device()));
    const QFontMetrics metrics(painter.font());
    for (std::size_t i = 0; i < fills.size(); ++i) {
        const QRect part(edges.at(i), inside.top(), edges.at(i + 1) - edges.at(i), inside.height());
        painter.fillRect(part, fills.at(i));
        const QString label = QString::number(qRound(100 * shares.at(static_cast<qsizetype>(i))));
        if (part.width() >= narrowest.at(i) &&
            metrics.horizontalAdvance(label) + 4 <= part.width()) {
            painter.setPen(texts.at(i));
            painter.drawText(part, Qt::AlignCenter, label);
        }
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(palette.border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(bar).adjusted(0.5, 0.5, -0.5, -0.5), corner_radius,
                            corner_radius);
}

int badge_pill_height(const QFont& font, const QPaintDevice& device) {
    return QFontMetrics(pill_font(font, device)).height() + (2 * pill_padding_vertical);
}

int badge_pills_width(Badges badges, const QFont& font, const QPaintDevice& device) {
    const QFontMetrics metrics(pill_font(font, device));
    int width = 0;
    for (const Badge badge : badge_order) {
        if (badges.has(badge)) {
            width += (width > 0 ? pill_gap : 0) + metrics.horizontalAdvance(badge_word(badge)) +
                     (2 * pill_padding);
        }
    }
    if (width == 0) {
        width = QFontMetrics(font).horizontalAdvance(none_mark());
    }
    return width + (2 * cell_padding);
}

void paint_badge_pills(QPainter& painter, const QStyleOptionViewItem& option, Badges badges,
                       const StudyPalette& palette) {
    const std::vector<Pill> pills =
        badge_pills(option.rect, badges, option.font, *painter.device());
    if (pills.empty()) {
        painter.setFont(option.font);
        painter.setPen(palette.muted);
        painter.drawText(content_of(option.rect), text_flags(Qt::AlignLeft | Qt::AlignVCenter),
                         none_mark());
        return;
    }
    painter.setFont(pill_font(option.font, *painter.device()));
    painter.setRenderHint(QPainter::Antialiasing);
    for (const Pill& pill : pills) {
        const PillColours colours = colours_of(pill.badge, palette);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colours.background);
        painter.drawRoundedRect(QRectF(pill.rect), corner_radius, corner_radius);
        painter.setPen(colours.text);
        painter.drawText(pill.rect, Qt::AlignCenter, badge_word(pill.badge));
    }
}

ResultDelegate::ResultDelegate(QObject* parent, const StudyPalette* palette)
    : QStyledItemDelegate(parent), palette_(palette != nullptr ? palette : &StudyPalette::light()) {
}

void ResultDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                           const QModelIndex& index) const {
    // The cell as the style paints it (its selection, no text), then the bar over it.
    QStyledItemDelegate::paint(painter, option, index);
    painter->save();
    paint_results_bar(*painter, option, index.data(shares_role).value<QList<double>>(), *palette_);
    painter->restore();
}

QSize ResultDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const QSize text = QStyledItemDelegate::sizeHint(option, index);
    return {results_bar_size.width() + (2 * cell_padding),
            qMax(text.height(), results_bar_size.height() + (2 * cell_padding_vertical))};
}

} // namespace chesscoach::app

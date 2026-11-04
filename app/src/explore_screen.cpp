// The Explore screen (explore_screen.hpp).
#include "explore_screen.hpp"

#include <chesscoach/position.hpp>

#include "explore_model.hpp"
#include "result_delegate.hpp"
#include "tables.hpp"
#include "wording.hpp"

#include <QLatin1Char>

#include <QAbstractItemView>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QListWidgetItem>
#include <QModelIndex>
#include <QPaintDevice>
#include <QPainter>
#include <QRect>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStringList>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace chesscoach::app {

namespace {

// The type sizes in pixels: the opening's name and the ECO chip.
constexpr qreal name_pixels = 17;
constexpr qreal chip_pixels = 11.5;
// The space between the screen's parts, between the chip and the name, and between the name and
// the line under it.
constexpr int part_spacing = 10;
constexpr int chip_spacing = 8;
constexpr int head_spacing = 3;
// The engine's panel: its padding, left and right then above and below; the space under its
// head; the evaluation's column and the gap after it; a line's padding above and below; and the
// lines it keeps room for, at least and at most (the Settings dialog's range).
constexpr int panel_padding = 10;
constexpr int panel_padding_vertical = 8;
constexpr int panel_head_spacing = 4;
constexpr int engine_score_width = 44;
constexpr int engine_gap = 10;
constexpr int engine_line_padding = 2;
constexpr int engine_rows_least = 3;
constexpr int engine_rows_most = 10;
// The roles under which an engine line's item holds its score and its moves.
constexpr int engine_score_role = Qt::UserRole;
constexpr int engine_moves_role = Qt::UserRole + 1;

QFont weighted(QFont font, QFont::Weight weight) {
    font.setWeight(weight);
    return font;
}

// A row of the move table: the tallest of a bold line of text, a pill and the results bar, its
// padding, and its rule.
int row_height(const QFont& font, const QPaintDevice& device) {
    const int content = std::max({
        QFontMetrics(weighted(font, QFont::Bold)).height(),
        badge_pill_height(font, device),
        results_bar_size.height(),
    });
    return content + (2 * cell_padding_vertical) + 1;
}

// A line of the engine's panel.
int engine_line_height(const QFont& font) {
    return QFontMetrics(weighted(font, QFont::Bold)).height() + (2 * engine_line_padding);
}

// Paints every cell of the move table in the screen's palette (docs/reference/app.md,
// **Tables**): the row's selection and rule, then the move in bold, the badges' pills, the
// results bar, or a number right-aligned in tabular figures.
class MoveDelegate : public QStyledItemDelegate {
  public:
    MoveDelegate(const StudyPalette* palette, QObject* parent)
        : QStyledItemDelegate(parent), palette_(palette) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->setClipRect(option.rect, Qt::IntersectClip);
        paint_row(*painter, option, *palette_);
        switch (index.column()) {
        case ExploreModel::badges_column:
            paint_badge_pills(*painter, option, badges_of(index), *palette_);
            break;
        case ExploreModel::result_column:
            paint_results_bar(*painter, option,
                              index.data(ExploreModel::shares_role).value<QList<double>>(),
                              *palette_);
            break;
        default: {
            const QFont font = font_of(option, index.column());
            const QRect content = option.rect.adjusted(cell_padding, 0, -cell_padding, -1);
            painter->setFont(font);
            painter->setPen(palette_->text);
            painter->drawText(content, text_flags(alignment_of(index)),
                              QFontMetrics(font).elidedText(index.data().toString(), Qt::ElideRight,
                                                            content.width()));
            break;
        }
        }
        painter->restore();
    }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override {
        if (option.widget == nullptr) {
            return QStyledItemDelegate::sizeHint(option, index);
        }
        const QPaintDevice& device = *option.widget;
        int width = 0;
        switch (index.column()) {
        case ExploreModel::badges_column:
            width = badge_pills_width(badges_of(index), option.font, device);
            break;
        case ExploreModel::result_column:
            width = results_bar_size.width() + (2 * cell_padding);
            break;
        default:
            width = QFontMetrics(font_of(option, index.column()))
                        .horizontalAdvance(index.data().toString()) +
                    (2 * cell_padding);
            break;
        }
        return {width, row_height(option.font, device)};
    }

  private:
    [[nodiscard]] static Badges badges_of(const QModelIndex& index) {
        return Badges(static_cast<std::uint8_t>(index.data(ExploreModel::badges_role).toUInt()));
    }

    [[nodiscard]] static QFont font_of(const QStyleOptionViewItem& option, int column) {
        return column == ExploreModel::move_column ? weighted(option.font, QFont::Bold)
                                                   : tabular(option.font);
    }

    [[nodiscard]] static Qt::Alignment alignment_of(const QModelIndex& index) {
        const QVariant alignment = index.data(Qt::TextAlignmentRole);
        return alignment.isValid() ? alignment.value<Qt::Alignment>()
                                   : Qt::Alignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

    const StudyPalette* palette_;
};

// Paints a line of the engine's panel: its score bold in the accent, in a column of its own,
// then its moves.
class EngineLineDelegate : public QStyledItemDelegate {
  public:
    EngineLineDelegate(const StudyPalette* palette, QObject* parent)
        : QStyledItemDelegate(parent), palette_(palette) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const QRect line = option.rect.adjusted(0, engine_line_padding, 0, -engine_line_padding);
        const QRect score(line.left(), line.top(), engine_score_width, line.height());
        const QRect moves = line.adjusted(engine_score_width + engine_gap, 0, 0, 0);
        painter->save();
        painter->setFont(weighted(tabular(option.font), QFont::Bold));
        painter->setPen(palette_->accent);
        painter->drawText(score, text_flags(Qt::AlignLeft | Qt::AlignVCenter),
                          index.data(engine_score_role).toString());
        painter->setFont(option.font);
        painter->setPen(palette_->text);
        painter->drawText(moves, text_flags(Qt::AlignLeft | Qt::AlignVCenter),
                          option.fontMetrics.elidedText(index.data(engine_moves_role).toString(),
                                                        Qt::ElideRight, moves.width()));
        painter->restore();
    }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override {
        return {engine_score_width + engine_gap +
                    option.fontMetrics.horizontalAdvance(index.data(engine_moves_role).toString()),
                engine_line_height(option.font)};
    }

  private:
    const StudyPalette* palette_;
};

// The screen's own rules, in the palette's colours: the chip, the muted lines, and the frames of
// the table and the engine's panel. They name the screen's widgets only, so the application's
// stylesheet keeps the rest.
QString screen_style(const StudyPalette& palette) {
    return QStringLiteral("#explore_eco { background: %1; border: 1px solid %2; border-radius: 2px;"
                          " color: %3; padding: 1px 5px; }\n"
                          "#explore_summary, #engine_detail { color: %3; }\n"
                          "#explore_table, #engine_panel { background: %4; border: 1px solid %2;"
                          " border-radius: 2px; }\n"
                          "#engine_lines { background: transparent; border: none; }\n")
        .arg(palette.panel_alt.name(), palette.border.name(), palette.muted.name(),
             palette.panel.name());
}

} // namespace

// The move table (explore_screen.hpp): Return and Enter activate the current row on every
// platform (Qt's own handling does not activate it on macOS), like a double-click. It asks for
// its rows' height, so that the engine's panel sits right under them, and at least the width of
// every column at its widest likely content (its header's label or a long sample), so that the
// window makes room for the columns whatever the rows. Each column takes its content's width,
// and the spare width goes to Badges, Games, Score for White and Top players in proportion to
// theirs, so that no column holds a wide gap.
class MoveTable : public QTableView {
  public:
    using QTableView::QTableView;

    [[nodiscard]] QSize sizeHint() const override {
        return {least_width(), horizontalHeader()->sizeHint().height() +
                                   verticalHeader()->length() + (2 * frameWidth())};
    }
    [[nodiscard]] QSize minimumSizeHint() const override {
        return {least_width(), QTableView::minimumSizeHint().height()};
    }

    // Fits the columns and the height to the rows the model holds now.
    void fit() {
        fit_columns();
        updateGeometry();
    }

  protected:
    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            currentIndex().isValid()) {
            emit activated(currentIndex());
            event->accept();
            return;
        }
        QTableView::keyPressEvent(event);
    }

    // A new width for the rows, the window's or a scroll bar's doing, shares itself out again.
    bool viewportEvent(QEvent* event) override {
        if (event->type() == QEvent::Resize) {
            fit_columns();
        }
        return QTableView::viewportEvent(event);
    }

  private:
    // The columns that share the spare width.
    [[nodiscard]] static bool sharing(int column) {
        return column == ExploreModel::badges_column || column == ExploreModel::games_column ||
               column == ExploreModel::score_column || column == ExploreModel::top_tier_column;
    }

    void fit_columns() {
        QHeaderView& columns = *horizontalHeader();
        std::array<int, ExploreModel::column_count> widths{};
        int used = 0;
        int shared = 0;
        for (int column = 0; column < ExploreModel::column_count; ++column) {
            const int width = std::max(columns.sectionSizeHint(column), sizeHintForColumn(column));
            widths.at(static_cast<std::size_t>(column)) = width;
            used += width;
            shared += sharing(column) ? width : 0;
        }
        const int spare = viewport()->width() - used;
        if (spare > 0 && shared > 0) {
            int given = 0;
            for (int column = 0; column < ExploreModel::column_count; ++column) {
                if (sharing(column)) {
                    const int extra = spare * widths.at(static_cast<std::size_t>(column)) / shared;
                    widths.at(static_cast<std::size_t>(column)) += extra;
                    given += extra;
                }
            }
            // The last sharing column takes what the division left over.
            widths.at(ExploreModel::top_tier_column) += spare - given;
        }
        for (int column = 0; column < ExploreModel::column_count; ++column) {
            columns.resizeSection(column, widths.at(static_cast<std::size_t>(column)));
        }
    }

    [[nodiscard]] int least_width() const {
        int width = 2 * frameWidth();
        for (int column = 0; column < ExploreModel::column_count; ++column) {
            width += std::max(horizontalHeader()->sectionSizeHint(column), widest(column));
        }
        return width;
    }

    // A column's widest likely content, as the move delegate paints it.
    [[nodiscard]] int widest(int column) const {
        const auto text = [](const QFont& font, const QString& sample) {
            return QFontMetrics(font).horizontalAdvance(sample) + (2 * cell_padding);
        };
        switch (column) {
        case ExploreModel::move_column:
            return text(weighted(font(), QFont::Bold), QStringLiteral("Qxe4+"));
        case ExploreModel::badges_column: {
            Badges both;
            both.set(Badge::popular);
            both.set(Badge::dubious);
            return badge_pills_width(both, font(), *this);
        }
        case ExploreModel::games_column:
            return text(tabular(font()), QStringLiteral("999,999"));
        case ExploreModel::result_column:
            return results_bar_size.width() + (2 * cell_padding);
        default:
            return text(tabular(font()), QStringLiteral("100.0%"));
        }
    }
};

QString score_text(std::optional<qint64> cp, std::optional<qint64> mate) {
    if (mate) {
        const qint64 moves = *mate;
        return (moves < 0 ? QStringLiteral("-M") : QStringLiteral("M")) +
               QString::number(moves < 0 ? -moves : moves);
    }
    const double pawns = static_cast<double>(cp.value_or(0)) / 100.0;
    const QString text = QString::number(pawns, 'f', 1);
    return pawns > 0 && text != QStringLiteral("0.0") ? QLatin1Char('+') + text : text;
}

QString position_summary(const ExploreData& explore, const QString& band) {
    QStringList parts;
    if (!explore.fen.isEmpty()) {
        if (const auto position = Position::from_fen(explore.fen.toStdString())) {
            parts << (position->side_to_move() == Color::white
                          ? ExploreScreen::tr("White to move")
                          : ExploreScreen::tr("Black to move"));
        }
    }
    const qint64 games = explore.node ? explore.node->games : 0;
    parts << counted(games, ExploreScreen::tr("%1 game by players rated %2"),
                     ExploreScreen::tr("%1 games by players rated %2"))
                 .arg(band);
    if (explore.position && explore.position->eval_depth) {
        parts << ExploreScreen::tr("the database's evaluation %1 at depth %2")
                     .arg(score_text(explore.position->eval_cp, explore.position->eval_mate),
                          QString::number(*explore.position->eval_depth));
    }
    return parts.join(QStringLiteral(" · "));
}

QString engine_score_text(const EngineLine& line) {
    if (line.mated) {
        return *line.mated == Color::white ? QStringLiteral("-M0") : QStringLiteral("M0");
    }
    return score_text(line.cp ? std::optional<qint64>(*line.cp) : std::nullopt,
                      line.mate ? std::optional<qint64>(*line.mate) : std::nullopt);
}

QString engine_line_text(const EngineLine& line) {
    const QString score = engine_score_text(line);
    return line.pv_san.isEmpty() ? score : score + QLatin1Char(' ') + line.pv_san;
}

ExploreScreen::ExploreScreen(QWidget* parent)
    : QWidget(parent), palette_(StudyPalette::light()), model_(new ExploreModel(this)),
      eco_(new QLabel(this)), name_(new QLabel(this)), summary_(new QLabel(this)),
      content_(new QStackedWidget(this)), table_(new MoveTable(this)), message_(new QLabel(this)),
      engine_panel_(new QFrame(this)), engine_label_(new QLabel(engine_panel_)),
      engine_detail_(new QLabel(engine_panel_)), engine_lines_(new QListWidget(engine_panel_)) {
    setObjectName(QStringLiteral("explore_screen"));
    eco_->setObjectName(QStringLiteral("explore_eco"));
    name_->setObjectName(QStringLiteral("explore_name"));
    summary_->setObjectName(QStringLiteral("explore_summary"));
    table_->setObjectName(QStringLiteral("explore_table"));
    message_->setObjectName(QStringLiteral("explore_message"));
    engine_panel_->setObjectName(QStringLiteral("engine_panel"));
    engine_label_->setObjectName(QStringLiteral("engine_label"));
    engine_detail_->setObjectName(QStringLiteral("engine_detail"));
    engine_lines_->setObjectName(QStringLiteral("engine_lines"));

    // The type: the application's font, the name in Source Serif 4 where it is installed.
    const QString serif = QStringLiteral("Source Serif 4");
    name_->setFont(sized_font(QFontDatabase::hasFamily(serif) ? QFont(serif) : font(), name_pixels,
                              QFont::DemiBold, *this));
    eco_->setFont(sized_font(font(), chip_pixels, QFont::DemiBold, *this));
    engine_label_->setFont(weighted(font(), QFont::DemiBold));
    summary_->setWordWrap(true);
    engine_detail_->setWordWrap(true);
    engine_detail_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);

    auto* header = new TableHeader(&palette_, table_);
    table_->setHorizontalHeader(header);
    table_->setModel(model_);
    table_->setItemDelegate(new MoveDelegate(&palette_, table_));
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(false);
    table_->setWordWrap(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(row_height(table_->font(), *table_));
    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::Fixed); // MoveTable::fit sizes the columns
    connect(table_, &QTableView::activated, this, [this](const QModelIndex& index) {
        emit move_requested(index.data(ExploreModel::uci_role).toString());
    });

    engine_lines_->setItemDelegate(new EngineLineDelegate(&palette_, engine_lines_));
    engine_lines_->setSelectionMode(QAbstractItemView::NoSelection);
    engine_lines_->setFocusPolicy(Qt::NoFocus);
    engine_lines_->setFrameShape(QFrame::NoFrame);
    engine_lines_->setUniformItemSizes(true);
    engine_lines_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* engine_head = new QHBoxLayout();
    engine_head->addWidget(engine_label_);
    engine_head->addWidget(engine_detail_, 1);
    auto* engine = new QVBoxLayout(engine_panel_);
    engine->setContentsMargins(panel_padding, panel_padding_vertical, panel_padding,
                               panel_padding_vertical);
    engine->setSpacing(panel_head_spacing);
    engine->addLayout(engine_head);
    engine->addWidget(engine_lines_);

    content_->addWidget(table_);
    content_->addWidget(message_);
    // The table and the message take the height they ask for, the engine's panel sits under
    // them, and the space left is below the panel.
    content_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto* title = new QHBoxLayout();
    title->setSpacing(chip_spacing);
    title->addWidget(eco_, 0, Qt::AlignVCenter);
    title->addWidget(name_, 1);
    auto* head = new QVBoxLayout();
    head->setSpacing(head_spacing);
    head->addLayout(title);
    head->addWidget(summary_);
    // What the screen is for and the first step, in the muted colour (the stylesheets'
    // QLabel[muted="true"]).
    auto* description = new QLabel(tr("See what players at your rating play here, and what the "
                                      "engine thinks. Play moves on the board to go deeper."),
                                   this);
    description->setObjectName(QStringLiteral("explore_description"));
    description->setWordWrap(true);
    description->setProperty("muted", true);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(part_spacing);
    column->addWidget(description);
    column->addLayout(head);
    column->addWidget(content_);
    column->addWidget(engine_panel_);
    column->addStretch(1);
    set_palette(StudyPalette::light());
    show_no_database();
    show_engine_state(tr("not started"));
}

void ExploreScreen::set_palette(const StudyPalette& palette) {
    palette_ = palette;
    setStyleSheet(screen_style(palette_));
    fit_engine_lines();
    table_->viewport()->update();
    table_->horizontalHeader()->viewport()->update();
    engine_lines_->viewport()->update();
}

void ExploreScreen::show_data(const ExploreData& explore, const QString& band) {
    // The worker writes the ECO code, a space and the name; the code may be missing.
    const qsizetype space = explore.name.indexOf(QLatin1Char(' '));
    const QString eco = space > 0 ? explore.name.left(space) : QString();
    const QString name = space >= 0 ? explore.name.mid(space + 1) : explore.name;
    eco_->setText(eco);
    eco_->setVisible(!eco.isEmpty());
    name_->setText(name.isEmpty() ? tr("Unnamed") : name);
    summary_->setText(position_summary(explore, band));
    if (!explore.node) {
        model_->set_moves({});
        table_->fit();
        message_->setText(
            tr("No games at this rating after this move. The engine is your guide from here."));
        content_->setCurrentWidget(message_);
        return;
    }
    model_->set_moves(explore.moves);
    table_->fit();
    content_->setCurrentWidget(table_);
}

void ExploreScreen::show_no_database(const QString& message) {
    eco_->clear();
    eco_->hide();
    name_->clear();
    summary_->clear();
    model_->set_moves({});
    table_->fit();
    message_->setText(message.isEmpty() ? tr("No opening database is open. Import one with File > "
                                             "Import database.")
                                        : message);
    content_->setCurrentWidget(message_);
}

QString ExploreScreen::engine_title() const {
    return engine_name_.isEmpty() ? tr("Engine") : engine_name_;
}

void ExploreScreen::show_engine_lines(const QVector<EngineLine>& lines, bool cached) {
    searching_ = false;
    engine_label_->setText(engine_title());
    engine_lines_->clear();
    for (const EngineLine& line : lines) {
        auto* item = new QListWidgetItem(engine_line_text(line), engine_lines_);
        item->setData(engine_score_role, engine_score_text(line));
        item->setData(engine_moves_role, line.pv_san);
    }
    if (lines.isEmpty()) {
        engine_detail_->clear();
    } else {
        const QString depth = QString::number(lines.front().depth);
        const QString count = lines.size() == 1 ? tr("1 line") : tr("%1 lines").arg(lines.size());
        engine_detail_->setText(cached ? tr("depth %1 (cached) · %2").arg(depth, count)
                                       : tr("depth %1 · %2").arg(depth, count));
    }
    fit_engine_lines();
}

void ExploreScreen::show_engine_searching() {
    searching_ = true;
    engine_label_->setText(engine_title());
    engine_detail_->setText(tr("thinking…"));
    engine_lines_->clear();
}

void ExploreScreen::show_engine_state(const QString& text) {
    // The state that ends a search is the engine's name; any other is said in the head.
    if (searching_) {
        searching_ = false;
        engine_name_ = text;
        engine_detail_->clear();
    } else {
        engine_detail_->setText(text);
    }
    engine_label_->setText(engine_title());
    engine_lines_->clear();
}

void ExploreScreen::fit_engine_lines() {
    engine_rows_ = std::clamp(std::max(engine_rows_, engine_lines_->count()), engine_rows_least,
                              engine_rows_most);
    engine_lines_->setFixedHeight((engine_rows_ * engine_line_height(engine_lines_->font())) +
                                  (2 * engine_lines_->frameWidth()));
}

} // namespace chesscoach::app

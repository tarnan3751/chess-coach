// The Explore screen (explore_screen.hpp).
#include "explore_screen.hpp"

#include "explore_model.hpp"

#include <QAbstractItemView>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QPainter>
#include <QRectF>
#include <QStackedWidget>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>
#include <array>
#include <cstddef>

namespace chesscoach::app {

namespace {

// Paints the Result column as a bar: white wins, draws and black wins side by side, each with
// its percentage when it is wide enough to hold it.
class ResultDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const auto shares = index.data(ExploreModel::shares_role).value<QList<double>>();
        if (shares.size() != 3) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        constexpr std::array fills{
            QColor(0xf4, 0xf4, 0xf4),
            QColor(0xa0, 0xa0, 0xa0),
            QColor(0x30, 0x30, 0x30),
        };
        constexpr std::array texts{
            QColor(0x20, 0x20, 0x20),
            QColor(0x10, 0x10, 0x10),
            QColor(0xf0, 0xf0, 0xf0),
        };
        const QRectF bar = QRectF(option.rect).adjusted(2, 3, -2, -3);
        painter->save();
        qreal left = bar.left();
        for (std::size_t i = 0; i < fills.size(); ++i) {
            const double share = shares.at(static_cast<qsizetype>(i));
            const QRectF part(left, bar.top(), bar.width() * share, bar.height());
            painter->fillRect(part, fills.at(i));
            const QString label = QString::number(qRound(100 * share)) + QLatin1Char('%');
            if (painter->fontMetrics().horizontalAdvance(label) + 4 < part.width()) {
                painter->setPen(texts.at(i));
                painter->drawText(part, Qt::AlignCenter, label);
            }
            left += part.width();
        }
        painter->setPen(QColor(0x80, 0x80, 0x80));
        painter->drawRect(bar);
        painter->restore();
    }
};

// The move table: Return and Enter activate the current row on every platform (Qt's own
// handling does not activate it on macOS), like a double-click.
class MoveTable : public QTableView {
  public:
    using QTableView::QTableView;

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
};

} // namespace

QString evaluation_text(const std::optional<PositionRow>& position) {
    if (!position || !position->eval_depth) {
        return ExploreScreen::tr("no evaluation");
    }
    if (position->eval_mate) {
        const qint64 mate = *position->eval_mate;
        return (mate < 0 ? QStringLiteral("-M") : QStringLiteral("M")) +
               QString::number(mate < 0 ? -mate : mate);
    }
    const double pawns = static_cast<double>(position->eval_cp.value_or(0)) / 100.0;
    const QString text = QString::number(pawns, 'f', 1);
    return pawns > 0 && text != QStringLiteral("0.0") ? QLatin1Char('+') + text : text;
}

ExploreScreen::ExploreScreen(QWidget* parent)
    : QWidget(parent), model_(new ExploreModel(this)), name_(new QLabel(this)),
      evaluation_(new QLabel(this)), games_(new QLabel(this)), content_(new QStackedWidget(this)),
      table_(new MoveTable(this)), message_(new QLabel(this)) {
    setObjectName(QStringLiteral("explore_screen"));
    name_->setObjectName(QStringLiteral("explore_name"));
    evaluation_->setObjectName(QStringLiteral("explore_evaluation"));
    games_->setObjectName(QStringLiteral("explore_games"));
    table_->setObjectName(QStringLiteral("explore_table"));
    message_->setObjectName(QStringLiteral("explore_message"));
    QFont bold = name_->font();
    bold.setBold(true);
    name_->setFont(bold);
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);

    table_->setModel(model_);
    table_->setItemDelegateForColumn(ExploreModel::result_column, new ResultDelegate(table_));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ExploreModel::result_column,
                                                     QHeaderView::Stretch);
    connect(table_, &QTableView::activated, this, [this](const QModelIndex& index) {
        emit move_requested(index.data(ExploreModel::uci_role).toString());
    });

    content_->addWidget(table_);
    content_->addWidget(message_);
    auto* column = new QVBoxLayout(this);
    column->addWidget(name_);
    column->addWidget(evaluation_);
    column->addWidget(games_);
    column->addWidget(content_, 1);
    show_no_database();
}

void ExploreScreen::show_data(const ExploreData& data, const QString& tier_label) {
    name_->setText(data.name.isEmpty() ? tr("Unnamed") : data.name);
    evaluation_->setText(tr("Evaluation: %1").arg(evaluation_text(data.position)));
    if (!data.node) {
        games_->clear();
        model_->set_moves({});
        message_->setText(tr("Off book at %1: %2 games").arg(tier_label).arg(0));
        content_->setCurrentWidget(message_);
        return;
    }
    games_->setText(tr("%1 games at %2").arg(data.node->games).arg(tier_label));
    model_->set_moves(data.moves);
    content_->setCurrentWidget(table_);
}

void ExploreScreen::show_no_database() {
    name_->clear();
    evaluation_->clear();
    games_->clear();
    model_->set_moves({});
    message_->setText(tr("No database loaded. Open one with --db <file>."));
    content_->setCurrentWidget(message_);
}

} // namespace chesscoach::app

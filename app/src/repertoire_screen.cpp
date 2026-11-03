// The Repertoire screen (repertoire_screen.hpp).
#include "repertoire_screen.hpp"

#include "tables.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVariant>
#include <vector>

namespace chesscoach::app {

namespace {

QString percent(double share) { return QStringLiteral("%1%").arg(share * 100.0, 0, 'f', 1); }

} // namespace

RepertoireScreen::RepertoireScreen(QWidget* parent)
    : QWidget(parent), white_(new QRadioButton(tr("White"), this)),
      black_(new QRadioButton(tr("Black"), this)),
      import_(new QPushButton(tr("Import PGN…"), this)), message_(new QLabel(this)),
      coverage_(new QLabel(this)), tree_(new QTreeView(this)), model_(new QStandardItemModel(this)),
      position_(new QLabel(this)), remove_(new QPushButton(tr("Remove choice"), this)),
      scores_(make_table(
          {
              tr("Move"),
              tr("Sound"),
              tr("Loss"),
              tr("Expected"),
              tr("Replies"),
              tr("Penalty"),
              tr("Score"),
              tr("Share"),
          },
          this)),
      choose_(new QPushButton(tr("Choose"), this)),
      gaps_(make_table({tr("Mass"), tr("Line"), tr("FEN")}, this)),
      goto_(new QPushButton(tr("Go to"), this)) {
    setObjectName(QStringLiteral("repertoire_screen"));
    white_->setObjectName(QStringLiteral("repertoire_white"));
    black_->setObjectName(QStringLiteral("repertoire_black"));
    import_->setObjectName(QStringLiteral("repertoire_import"));
    message_->setObjectName(QStringLiteral("repertoire_message"));
    coverage_->setObjectName(QStringLiteral("repertoire_coverage"));
    tree_->setObjectName(QStringLiteral("repertoire_tree"));
    position_->setObjectName(QStringLiteral("repertoire_position"));
    remove_->setObjectName(QStringLiteral("repertoire_remove"));
    scores_->setObjectName(QStringLiteral("repertoire_scores"));
    choose_->setObjectName(QStringLiteral("repertoire_choose"));
    gaps_->setObjectName(QStringLiteral("repertoire_gaps"));
    goto_->setObjectName(QStringLiteral("repertoire_goto"));
    white_->setChecked(true);
    message_->setWordWrap(true);
    position_->setWordWrap(true);
    tree_->setModel(model_);
    tree_->setHeaderHidden(true);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    gaps_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    auto* controls = new QHBoxLayout();
    controls->addWidget(white_);
    controls->addWidget(black_);
    controls->addStretch(1);
    controls->addWidget(import_);
    auto* here = new QHBoxLayout();
    here->addWidget(position_, 1);
    here->addWidget(remove_);
    auto* choosing = new QHBoxLayout();
    choosing->addWidget(new QLabel(tr("Recommended here"), this), 1);
    choosing->addWidget(choose_);
    auto* gaps = new QHBoxLayout();
    gaps->addWidget(new QLabel(tr("Gaps, most often met first"), this), 1);
    gaps->addWidget(goto_);

    auto* column = new QVBoxLayout(this);
    column->addLayout(controls);
    column->addWidget(message_);
    column->addWidget(coverage_);
    column->addWidget(tree_, 2);
    column->addLayout(here);
    column->addLayout(choosing);
    column->addWidget(scores_, 1);
    column->addLayout(gaps);
    column->addWidget(gaps_, 1);

    connect(white_, &QRadioButton::toggled, this,
            [this](bool checked) { emit colour_changed(checked); });
    connect(import_, &QPushButton::clicked, this, &RepertoireScreen::import_clicked);
    connect(tree_, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        emit navigate_requested(model_->data(index, Qt::UserRole).toStringList());
    });
    connect(remove_, &QPushButton::clicked, this, &RepertoireScreen::remove_requested);
    connect(choose_, &QPushButton::clicked, this, [this] {
        const QTableWidgetItem* selected = scores_->item(scores_->currentRow(), 0);
        if (selected != nullptr) {
            emit choose_requested(selected->data(Qt::UserRole).toString());
        }
    });
    connect(goto_, &QPushButton::clicked, this, [this] {
        const int row = gaps_->currentRow();
        if (row >= 0 && row < gap_lines_.size()) {
            emit navigate_requested(gap_lines_.at(row));
        }
    });
    show_position(true, QString());
    show_message(tr("Play a move where your colour is to move to choose it; play the "
                    "opponent's replies to go on."));
}

bool RepertoireScreen::user_white() const { return white_->isChecked(); }

void RepertoireScreen::show_repertoire(const RepertoireData& repertoire) {
    model_->clear();
    std::vector<QStandardItem*> items;
    items.reserve(static_cast<std::size_t>(repertoire.tree.size()));
    for (const RepertoireTreeNode& node : repertoire.tree) {
        auto* item = new QStandardItem(node.text);
        item->setData(node.line, Qt::UserRole);
        item->setEditable(false);
        if (node.chosen) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
        if (node.parent < 0) {
            model_->appendRow(item);
        } else {
            items.at(static_cast<std::size_t>(node.parent))->appendRow(item);
        }
        items.push_back(item);
    }
    tree_->expandAll();
}

void RepertoireScreen::show_coverage(const CoverageData& coverage, const QString& tier) {
    coverage_->setText(tr("Coverage at %1 to %2 plies: %3")
                           .arg(tier)
                           .arg(coverage.depth)
                           .arg(percent(coverage.covered)));
    gaps_->setRowCount(0);
    gap_lines_.clear();
    for (const CoverageGap& gap : coverage.gaps) {
        const int row = gaps_->rowCount();
        gaps_->insertRow(row);
        gaps_->setItem(row, 0, table_item(percent(gap.mass)));
        gaps_->setItem(row, 1, table_item(gap.line_san.isEmpty() ? tr("the start") : gap.line_san));
        gaps_->setItem(row, 2, table_item(gap.fen));
        gap_lines_.append(gap.line);
    }
    if (gaps_->rowCount() > 0) {
        gaps_->selectRow(0);
    }
    goto_->setEnabled(gaps_->rowCount() > 0);
}

void RepertoireScreen::show_coverage_message(const QString& text) {
    coverage_->setText(text);
    gaps_->setRowCount(0);
    gap_lines_.clear();
    goto_->setEnabled(false);
}

void RepertoireScreen::show_position(bool colour_to_move, const QString& choice_san) {
    if (!colour_to_move) {
        position_->setText(tr("The opponent is to move: play a reply to follow it."));
    } else if (choice_san.isEmpty()) {
        position_->setText(tr("No choice here yet: play your move, or choose one below."));
    } else {
        position_->setText(tr("Your move here: %1").arg(choice_san));
    }
    remove_->setEnabled(colour_to_move && !choice_san.isEmpty());
    scores_->setEnabled(colour_to_move);
    if (!colour_to_move) {
        scores_->setRowCount(0);
    }
    choose_->setEnabled(colour_to_move && scores_->rowCount() > 0);
}

void RepertoireScreen::show_scores(const ScoreData& scores) {
    scores_->setRowCount(0);
    for (const ScoreRow& move : scores.rows) {
        const int row = scores_->rowCount();
        scores_->insertRow(row);
        auto* san = table_item(move.san);
        san->setData(Qt::UserRole, move.uci);
        scores_->setItem(row, 0, san);
        scores_->setItem(row, 1, table_item(move.sound ? tr("yes") : tr("no")));
        scores_->setItem(row, 2, table_item(move.loss ? tr("%1 cp").arg(*move.loss) : tr("—")));
        scores_->setItem(row, 3, table_item(QString::number(move.expected, 'f', 3)));
        scores_->setItem(row, 4, table_item(QString::number(move.replies)));
        scores_->setItem(row, 5, table_item(QString::number(move.penalty, 'f', 2)));
        scores_->setItem(row, 6, table_item(QString::number(move.score, 'f', 3)));
        scores_->setItem(row, 7, table_item(percent(move.share)));
    }
    if (scores_->rowCount() > 0) {
        scores_->selectRow(0);
    }
    choose_->setEnabled(scores_->isEnabled() && scores_->rowCount() > 0);
}

void RepertoireScreen::show_message(const QString& text) { message_->setText(text); }

void RepertoireScreen::import_pgn(const QString& path) { emit import_requested(path); }

} // namespace chesscoach::app

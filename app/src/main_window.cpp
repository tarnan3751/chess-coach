// The main window's frame (main_window.hpp).
#include "main_window.hpp"

#include <chesscoach/position.hpp>
#include <chesscoach/tier.hpp>
#include <chesscoach/version.hpp>

#include "board_view.hpp"

#include <QAction>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <array>

namespace chesscoach::app {

MainWindow::MainWindow(const QString& /*data_dir*/, const QString& db, QWidget* parent)
    : QMainWindow(parent), board_(new BoardView()), move_list_(new QListWidget()),
      tier_selector_(new QComboBox()) {
    setWindowTitle(tr("chess-coach %1").arg(QString::fromStdString(to_string(current_version()))));
    resize(1100, 720);

    auto* toolbar = addToolBar(tr("Board"));
    toolbar->setObjectName(QStringLiteral("toolbar"));
    toolbar->setMovable(false);
    QAction* new_game_action = toolbar->addAction(tr("New game"));
    new_game_action->setObjectName(QStringLiteral("new_game_action"));
    connect(new_game_action, &QAction::triggered, this, &MainWindow::new_game);
    QAction* flip_action = toolbar->addAction(tr("Flip board"));
    flip_action->setObjectName(QStringLiteral("flip_action"));
    connect(flip_action, &QAction::triggered, board_, &BoardView::flip);
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Tier") + QLatin1Char(' ')));
    tier_selector_->setObjectName(QStringLiteral("tier_selector"));
    for (int index = 0; index < tier_count; ++index) {
        // Tier's values are the indices 0 to tier_count - 1.
        const std::string_view label = tier_label(static_cast<Tier>(index));
        tier_selector_->addItem(
            QString::fromUtf8(label.data(), static_cast<qsizetype>(label.size())), index);
    }
    tier_selector_->setCurrentIndex(default_tier);
    toolbar->addWidget(tier_selector_);

    move_list_->setObjectName(QStringLiteral("move_list"));
    connect(board_, &BoardView::move_played, this,
            [this](const QString& /*uci*/, const QString& san) { add_move(san); });

    auto* tabs = new QTabBar();
    tabs->setObjectName(QStringLiteral("screen_tabs"));
    auto* screens = new QStackedWidget();
    screens->setObjectName(QStringLiteral("screens"));
    const std::array<QString, 4> names{tr("Explore"), tr("Spar"), tr("Repertoire"), tr("Drill")};
    for (const QString& name : names) {
        tabs->addTab(name);
        auto* placeholder = new QLabel(name);
        placeholder->setAlignment(Qt::AlignCenter);
        screens->addWidget(placeholder);
    }
    connect(tabs, &QTabBar::currentChanged, screens, &QStackedWidget::setCurrentIndex);

    auto* left = new QVBoxLayout();
    left->addWidget(board_, 3);
    left->addWidget(move_list_, 1);
    auto* right = new QVBoxLayout();
    right->addWidget(tabs);
    right->addWidget(screens, 1);
    auto* central = new QWidget();
    auto* columns = new QHBoxLayout(central);
    columns->addLayout(left, 3);
    columns->addLayout(right, 2);
    setCentralWidget(central);

    auto* database = new QLabel(db.isEmpty() ? tr("No database") : QFileInfo(db).fileName());
    database->setObjectName(QStringLiteral("database_status"));
    database->setToolTip(db);
    auto* engine = new QLabel(tr("Engine: not started"));
    engine->setObjectName(QStringLiteral("engine_status"));
    statusBar()->addWidget(database);
    statusBar()->addPermanentWidget(engine);
}

void MainWindow::new_game() {
    board_->set_position(Position::start());
    moves_.clear();
    move_list_->clear();
}

void MainWindow::add_move(const QString& san) {
    // One row per move pair, white's move starting it: "1. e4 e5".
    moves_.append(san);
    const auto ply = moves_.size() - 1;
    if (ply % 2 == 0) {
        move_list_->addItem(QStringLiteral("%1. %2").arg((ply / 2) + 1).arg(san));
    } else if (QListWidgetItem* row = move_list_->item(move_list_->count() - 1)) {
        row->setText(row->text() + QLatin1Char(' ') + san);
    }
    move_list_->scrollToBottom();
}

} // namespace chesscoach::app

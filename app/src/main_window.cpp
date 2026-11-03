// The main window (main_window.hpp).
#include "main_window.hpp"

#include <chesscoach/position.hpp>
#include <chesscoach/tier.hpp>
#include <chesscoach/version.hpp>

#include "board_view.hpp"
#include "db_worker.hpp"
#include "eval_bar.hpp"
#include "explore_screen.hpp"

#include <QAction>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace chesscoach::app {

namespace {

QString tier_text(int tier) {
    // Tier's values are the indices 0 to tier_count - 1.
    const std::string_view label = tier_label(static_cast<Tier>(tier));
    return QString::fromUtf8(label.data(), static_cast<qsizetype>(label.size()));
}

} // namespace

MainWindow::MainWindow(const QString& /*data_dir*/, QString db, QWidget* parent)
    : QMainWindow(parent), db_path_(std::move(db)), db_worker_(new DbWorker()),
      board_(new BoardView()), eval_bar_(new EvalBar()), explore_(new ExploreScreen()),
      move_list_(new QListWidget()), tier_selector_(new QComboBox()) {
    register_explore_types();
    setWindowTitle(tr("chess-coach %1").arg(QString::fromStdString(to_string(current_version()))));
    resize(1100, 720);
    line_keys_.append(Position::start().key());

    build_toolbar();

    move_list_->setObjectName(QStringLiteral("move_list"));
    connect(board_, &BoardView::move_played, this, &MainWindow::move_played);
    connect(move_list_, &QListWidget::itemClicked, this, [this](const QListWidgetItem* item) {
        const auto row = static_cast<qsizetype>(move_list_->row(item));
        jump_to(std::min<qsizetype>(2 * (row + 1), static_cast<qsizetype>(line_uci_.size())));
    });
    connect(explore_, &ExploreScreen::move_requested, board_,
            [this](const QString& uci) { board_->play_move(uci); });

    auto* tabs = new QTabBar();
    tabs->setObjectName(QStringLiteral("screen_tabs"));
    auto* screens = new QStackedWidget();
    screens->setObjectName(QStringLiteral("screens"));
    tabs->addTab(tr("Explore"));
    screens->addWidget(explore_);
    for (const QString& name : {tr("Spar"), tr("Repertoire"), tr("Drill")}) {
        tabs->addTab(name);
        auto* placeholder = new QLabel(name);
        placeholder->setAlignment(Qt::AlignCenter);
        screens->addWidget(placeholder);
    }
    connect(tabs, &QTabBar::currentChanged, screens, &QStackedWidget::setCurrentIndex);

    auto* board_row = new QHBoxLayout();
    board_row->addWidget(eval_bar_);
    board_row->addWidget(board_, 1);
    auto* left = new QVBoxLayout();
    left->addLayout(board_row, 3);
    left->addWidget(move_list_, 1);
    auto* right = new QVBoxLayout();
    right->addWidget(tabs);
    right->addWidget(screens, 1);
    auto* central = new QWidget();
    auto* columns = new QHBoxLayout(central);
    columns->addLayout(left, 1);
    columns->addLayout(right, 1);
    setCentralWidget(central);

    build_status_bar();

    // The worker owns the store on its own thread for the window's life; every request and
    // every answer crosses by queued connection.
    db_worker_->moveToThread(&db_thread_);
    connect(&db_thread_, &QThread::finished, db_worker_, &QObject::deleteLater);
    connect(this, &MainWindow::open_requested, db_worker_, &DbWorker::open_database);
    connect(this, &MainWindow::query_requested, db_worker_, &DbWorker::query);
    connect(db_worker_, &DbWorker::database_opened, this, &MainWindow::database_opened);
    connect(db_worker_, &DbWorker::database_failed, this, &MainWindow::database_failed);
    connect(db_worker_, &DbWorker::position_ready, this, &MainWindow::position_ready);
    db_thread_.setObjectName(QStringLiteral("database"));
    db_thread_.start();
    if (!db_path_.isEmpty()) {
        emit open_requested(db_path_);
    }
}

MainWindow::~MainWindow() {
    db_thread_.quit();
    db_thread_.wait();
}

void MainWindow::build_toolbar() {
    auto* toolbar = addToolBar(tr("Board"));
    toolbar->setObjectName(QStringLiteral("toolbar"));
    toolbar->setMovable(false);
    QAction* new_game = toolbar->addAction(tr("New game"));
    new_game->setObjectName(QStringLiteral("new_game_action"));
    connect(new_game, &QAction::triggered, this, [this] { jump_to(0); });
    QAction* back = toolbar->addAction(tr("Back"));
    back->setObjectName(QStringLiteral("back_action"));
    back->setShortcut(QKeySequence::Back);
    connect(back, &QAction::triggered, this, [this] {
        if (!line_uci_.empty()) {
            jump_to(static_cast<qsizetype>(line_uci_.size()) - 1);
        }
    });
    QAction* flip = toolbar->addAction(tr("Flip board"));
    flip->setObjectName(QStringLiteral("flip_action"));
    connect(flip, &QAction::triggered, this, [this] {
        board_->flip();
        eval_bar_->set_flipped(board_->flipped());
    });
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Tier") + QLatin1Char(' ')));
    tier_selector_->setObjectName(QStringLiteral("tier_selector"));
    for (int index = 0; index < tier_count; ++index) {
        tier_selector_->addItem(tier_text(index), index);
    }
    tier_selector_->setCurrentIndex(default_tier);
    connect(tier_selector_, &QComboBox::currentIndexChanged, this, [this] { request_query(); });
    toolbar->addWidget(tier_selector_);
}

void MainWindow::build_status_bar() {
    database_label_ =
        new QLabel(db_path_.isEmpty() ? tr("No database") : QFileInfo(db_path_).fileName());
    database_label_->setObjectName(QStringLiteral("database_status"));
    database_label_->setToolTip(db_path_);
    data_version_label_ = new QLabel();
    data_version_label_->setObjectName(QStringLiteral("data_version_status"));
    data_version_label_->hide();
    auto* engine = new QLabel(tr("Engine: not started"));
    engine->setObjectName(QStringLiteral("engine_status"));
    statusBar()->addWidget(database_label_);
    statusBar()->addWidget(data_version_label_);
    statusBar()->addPermanentWidget(engine);
}

int MainWindow::tier() const { return tier_selector_->currentIndex(); }

void MainWindow::move_played(const QString& uci, const QString& san) {
    line_uci_.push_back(uci.toStdString());
    line_san_.append(san);
    line_keys_.append(board_->position().key());
    rebuild_move_list();
    request_query();
}

void MainWindow::jump_to(qsizetype ply) {
    const auto size = static_cast<std::size_t>(std::max<qsizetype>(ply, 0));
    if (size < line_uci_.size()) {
        line_uci_.resize(size);
        line_san_.resize(static_cast<qsizetype>(size));
    }
    Position position = Position::start();
    line_keys_ = {position.key()};
    for (const std::string& uci : line_uci_) {
        const auto played = position.play_uci(uci);
        if (!played) {
            break; // cannot happen: the line holds the moves the board played
        }
        line_keys_.append(played->child);
    }
    board_->set_position(position);
    rebuild_move_list();
    request_query();
}

void MainWindow::rebuild_move_list() {
    // One row per move pair, white's move starting it: "1. e4 e5".
    move_list_->clear();
    for (qsizetype ply = 0; ply < line_san_.size(); ply += 2) {
        QString row = QStringLiteral("%1. %2").arg((ply / 2) + 1).arg(line_san_.at(ply));
        if (ply + 1 < line_san_.size()) {
            row += QLatin1Char(' ') + line_san_.at(ply + 1);
        }
        move_list_->addItem(row);
    }
    move_list_->scrollToBottom();
}

void MainWindow::request_query() {
    if (database_open_) {
        emit query_requested(line_keys_, tier());
    }
}

void MainWindow::database_opened(const QString& data_version) {
    database_open_ = true;
    data_version_ = data_version;
    database_label_->setText(QFileInfo(db_path_).fileName());
    data_version_label_->setText(tr("Data version %1").arg(data_version));
    data_version_label_->show();
    request_query();
}

void MainWindow::database_failed(const QString& message) {
    database_open_ = false;
    data_version_.clear();
    database_label_->setText(tr("No database"));
    data_version_label_->hide();
    explore_->show_no_database();
    eval_bar_->set_evaluation(std::nullopt, std::nullopt);
    auto* box = new QMessageBox(QMessageBox::Warning, tr("Opening database failed"), message,
                                QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("database_error"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

void MainWindow::position_ready(const ExploreData& data) {
    // An answer for a position or a tier the window has left behind is dropped.
    if (data.key != line_keys_.back() || data.tier != tier()) {
        return;
    }
    explore_->show_data(data, tier_text(data.tier));
    const bool evaluated = data.position && data.position->eval_depth;
    eval_bar_->set_evaluation(evaluated ? data.position->eval_cp : std::nullopt,
                              evaluated ? data.position->eval_mate : std::nullopt);
}

} // namespace chesscoach::app

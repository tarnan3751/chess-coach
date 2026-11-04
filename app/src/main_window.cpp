// The main window (main_window.hpp).
#include "main_window.hpp"

#include <chesscoach/position.hpp>
#include <chesscoach/tier.hpp>
#include <chesscoach/version.hpp>

#include "about_dialog.hpp"
#include "board_theme.hpp"
#include "board_view.hpp"
#include "db_worker.hpp"
#include "drill_screen.hpp"
#include "engine_worker.hpp"
#include "eval_bar.hpp"
#include "explore_screen.hpp"
#include "file_chooser.hpp"
#include "fonts.hpp"
#include "help_dialog.hpp"
#include "icons.hpp"
#include "profile_worker.hpp"
#include "repertoire_screen.hpp"
#include "settings_dialog.hpp"
#include "sounds.hpp"
#include "spar_screen.hpp"
#include "spar_worker.hpp"
#include "study_palette.hpp"
#include "wording.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QModelIndex>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QProgressDialog>
#include <QRect>
#include <QResizeEvent>
#include <QSize>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace chesscoach::app {

namespace {

// The palette the style draws its own parts with (combo arrows, check marks, scroll bars, a
// native menu) and the move list's rows are painted with, from the theme's colours: the panel
// for buttons and bases, the selection for highlights, the hairline for Mid, and the muted
// colour for placeholders and disabled text.
QPalette qt_palette(const StudyPalette& study) {
    QPalette palette(study.panel, study.window);
    for (const auto role : {
             QPalette::WindowText,
             QPalette::Text,
             QPalette::ButtonText,
             QPalette::ToolTipText,
             QPalette::HighlightedText,
         }) {
        palette.setColor(role, study.text);
    }
    palette.setColor(QPalette::Base, study.panel);
    palette.setColor(QPalette::AlternateBase, study.panel_alt);
    palette.setColor(QPalette::ToolTipBase, study.panel);
    palette.setColor(QPalette::Highlight, study.selection);
    palette.setColor(QPalette::PlaceholderText, study.muted);
    palette.setColor(QPalette::Link, study.accent);
    palette.setColor(QPalette::LinkVisited, study.accent);
    palette.setColor(QPalette::Mid, study.border);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    palette.setColor(QPalette::Accent, study.accent);
#endif
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, study.muted);
    }
    return palette;
}

// The move list's rows, "1. e4 c5": the number in the muted colour (the palette's placeholder
// text), right-aligned in a column of its own, then each move in its column, the last move of the
// list, the board's position, marked in the selection colour (its highlight). The item's text
// stays the plain row, which the tests read.
class MoveListDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const QStringList parts = index.data().toString().split(QLatin1Char(' '));
        const QFontMetrics metrics(option.font);
        const int number_width = metrics.horizontalAdvance(QStringLiteral("000."));
        const int move_width = metrics.horizontalAdvance(QStringLiteral("Qxe8=Q+"));
        const bool last_row = index.row() == index.model()->rowCount() - 1;
        painter->save();
        painter->setFont(option.font);
        QRect cell = option.rect.adjusted(move_padding, 0, 0, 0);
        cell.setWidth(number_width);
        painter->setPen(option.palette.color(QPalette::PlaceholderText));
        painter->drawText(cell, Qt::AlignRight | Qt::AlignVCenter, parts.value(0));
        for (qsizetype move = 1; move < parts.size(); ++move) {
            const int left = option.rect.left() + move_padding + number_width + move_gap +
                             ((static_cast<int>(move) - 1) * (move_width + move_gap));
            const QRect text(left, option.rect.top(), move_width, option.rect.height());
            if (last_row && move == parts.size() - 1) {
                const int width = metrics.horizontalAdvance(parts.at(move));
                const QRect mark(left - mark_padding, option.rect.top() + 1,
                                 width + (2 * mark_padding), option.rect.height() - 2);
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(Qt::NoPen);
                painter->setBrush(option.palette.color(QPalette::Highlight));
                painter->drawRoundedRect(mark, 2, 2);
            }
            painter->setPen(option.palette.color(QPalette::Text));
            painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, parts.at(move));
        }
        painter->restore();
    }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(QFontMetrics(option.font).height() + 6);
        return size;
    }

  private:
    static constexpr int move_padding = 10; // the panel's inner margin
    static constexpr int move_gap = 8;
    static constexpr int mark_padding = 4;
};

// The Rating chooser: a combo whose arrow is the chevron-down icon in the muted colour (the
// palette's placeholder text), as the stylesheet, which draws the rest, cannot recolour an image.
class RatingChooser : public QComboBox {
  public:
    using QComboBox::QComboBox;

  protected:
    void changeEvent(QEvent* event) override {
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
            const QColor muted = palette().color(QPalette::PlaceholderText);
            chevron_ = tinted_icon(QStringLiteral("chevron-down"), muted, muted);
        }
        QComboBox::changeEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        QComboBox::paintEvent(event);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        QRect chevron(0, 0, 16, 16);
        chevron.moveCenter(
            style()
                ->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, this)
                .center());
        QPainter painter(this);
        chevron_.paint(&painter, chevron);
    }

  private:
    QIcon chevron_;
};

// The evaluation bar and the board side by side, 8 px apart at the row's top left: the board
// square, the bar exactly its height. Columns sets the side the row asks for; the row is as tall as
// it is wide, less the bar, so that the move list below takes the rest of the column.
class BoardRow : public QWidget {
  public:
    static constexpr int bar_width = 14;
    static constexpr int gap = 8;
    static constexpr int min_side = 300;

    BoardRow(QWidget* bar, QWidget* board) : bar_(bar), board_(board) {
        bar_->setParent(this);
        board_->setParent(this);
        QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }

    void set_side(int side) {
        if (side != side_) {
            side_ = side;
            updateGeometry();
        }
    }

    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override {
        return std::max(0, width - bar_width - gap);
    }
    [[nodiscard]] QSize sizeHint() const override { return {side_ + bar_width + gap, side_}; }
    [[nodiscard]] QSize minimumSizeHint() const override {
        return {min_side + bar_width + gap, min_side};
    }

  protected:
    void resizeEvent(QResizeEvent* /*event*/) override {
        const int side = std::max(0, std::min(height(), width() - bar_width - gap));
        bar_->setGeometry(0, 0, bar_width, side);
        board_->setGeometry(bar_width + gap, 0, side, side);
    }

  private:
    QWidget* bar_;
    QWidget* board_;
    int side_ = min_side;
};

// The window's content: the board column (the board row over the move list) and the screen column
// (the tabs over the screens), 14 px from the window's edges and from each other, 10 px between a
// column's parts. The board takes the side the window's height allows, as long as the screen
// column keeps its minimum, at least screen_min_width; the screen column takes the rest.
class Columns : public QWidget {
  public:
    static constexpr int margin = 14;
    static constexpr int spacing = 10;
    static constexpr int screen_min_width = 700;

    Columns(BoardRow* row, QWidget* move_list, QWidget* tabs, QWidget* screens)
        : row_(row), move_list_(move_list), right_(new QVBoxLayout()) {
        auto* left = new QVBoxLayout();
        left->setSpacing(spacing);
        left->addWidget(row);
        left->addWidget(move_list, 1);
        screens->setMinimumWidth(screen_min_width);
        right_->setSpacing(spacing);
        right_->addWidget(tabs);
        right_->addWidget(screens, 1);
        auto* columns = new QHBoxLayout(this);
        columns->setContentsMargins(margin, margin, margin, margin);
        columns->setSpacing(margin);
        columns->addLayout(left);
        columns->addLayout(right_, 1);
    }

  protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        const int by_height = height() - (2 * margin) - spacing - move_list_->minimumHeight();
        const int by_width = width() - (3 * margin) - right_->minimumSize().width() -
                             BoardRow::bar_width - BoardRow::gap;
        row_->set_side(std::max(BoardRow::min_side, std::min(by_height, by_width)));
    }

  private:
    BoardRow* row_;
    QWidget* move_list_;
    QVBoxLayout* right_; // owned by the columns' layout
};

} // namespace

QString MainWindow::tier_text(int tier) { return rating_band(tier); }

MainWindow::MainWindow(const QString& data_dir, const QString& db, QWidget* parent)
    : QMainWindow(parent), data_dir_(data_dir), settings_(data_dir), resources_(bundle_resources()),
      located_(find_opening_database(db, data_dir, resources_)),
      db_path_(located_ ? QString::fromStdString(located_->path.string()) : QString()),
      db_worker_(new DbWorker()), engine_worker_(new EngineWorker(data_dir, resources_)),
      spar_worker_(new SparWorker(data_dir, db_path_, resources_)),
      profile_worker_(new ProfileWorker(data_dir, db_path_)), board_(new BoardView()),
      eval_bar_(new EvalBar()), explore_(new ExploreScreen()), spar_(new SparScreen()),
      repertoire_(new RepertoireScreen()), drill_(new DrillScreen()), tabs_(new QTabBar()),
      move_list_(new QListWidget()), tier_selector_(new RatingChooser()) {
    keep_qt_settings_in(data_dir);
    // The Study look on every platform: Fusion, which draws what the stylesheet cannot reach
    // (radio buttons, spin boxes, scroll bars, the tab strip's alignment) in the theme's palette,
    // where the macOS style draws them in the system's colours; and the bundled fonts.
    QApplication::setStyle(QStringLiteral("Fusion"));
    install_fonts();
    register_explore_types();
    register_engine_types();
    register_spar_types();
    register_profile_types();
    setWindowTitle(tr("chess-coach %1").arg(QString::fromStdString(to_string(current_version()))));
    resize(1200, 780);
    line_keys_.append(Position::start().key());
    sounds_.set_enabled(settings_.sounds_enabled());

    build_toolbar();
    build_menus();

    move_list_->setObjectName(QStringLiteral("move_list"));
    move_list_->setItemDelegate(new MoveListDelegate(move_list_));
    move_list_->setSelectionMode(QAbstractItemView::NoSelection);
    move_list_->setMinimumHeight(96);
    // Every move the board shows, the user's or one it is given, sounds once.
    connect(board_, &BoardView::move_shown, this,
            [this](const QString& san) { sounds_.play(move_sound(san)); });
    connect(board_, &BoardView::move_played, this, &MainWindow::move_played);
    connect(move_list_, &QListWidget::itemClicked, this, [this](const QListWidgetItem* item) {
        // Explore's line and the repertoire's are lines to go back along; a session's moves and
        // a card's are not.
        const auto row = static_cast<qsizetype>(move_list_->row(item));
        if (screen_ == Screen::explore) {
            jump_to(std::min<qsizetype>(2 * (row + 1), static_cast<qsizetype>(line_uci_.size())));
        } else if (screen_ == Screen::repertoire) {
            repertoire_jump(
                std::min<qsizetype>(2 * (row + 1), static_cast<qsizetype>(repertoire_uci_.size())));
        }
    });
    connect(explore_, &ExploreScreen::move_requested, board_,
            [this](const QString& uci) { board_->play_move(uci); });

    tabs_->setObjectName(QStringLiteral("screen_tabs"));
    // The stylesheet draws the tabs and the hairline under the whole strip, which the current
    // tab's rule covers; the style's own base line is off.
    tabs_->setAttribute(Qt::WA_StyledBackground);
    tabs_->setDrawBase(false);
    tabs_->setExpanding(false);
    auto* screens = new QStackedWidget();
    screens->setObjectName(QStringLiteral("screens"));
    tabs_->addTab(tr("Explore"));
    screens->addWidget(explore_);
    tabs_->addTab(tr("Practice")); // the Spar screen (PLAN.md §11, 2026-11-04)
    screens->addWidget(spar_);
    tabs_->addTab(tr("Repertoire"));
    screens->addWidget(repertoire_);
    tabs_->addTab(tr("Drill"));
    screens->addWidget(drill_);
    connect(tabs_, &QTabBar::currentChanged, screens, &QStackedWidget::setCurrentIndex);
    connect(tabs_, &QTabBar::currentChanged, this, &MainWindow::screen_changed);
    connect(spar_, &SparScreen::start_requested, this, &MainWindow::spar_start);
    connect(spar_, &SparScreen::end_requested, this, &MainWindow::spar_end_requested);
    connect(spar_, &SparScreen::continue_requested, this, &MainWindow::update_board_input);
    connect(spar_, &SparScreen::replay_requested, this, &MainWindow::spar_replay_requested);
    connect(spar_, &SparScreen::replay_step, this, &MainWindow::spar_replay_step);
    connect(repertoire_, &RepertoireScreen::colour_changed, this,
            &MainWindow::repertoire_colour_changed);
    connect(repertoire_, &RepertoireScreen::import_clicked, this, [this] {
        const QString path = choose_file_to_open(this, settings_, tr("Add lines from a PGN file"),
                                                 {QStringLiteral("*.pgn")});
        if (!path.isEmpty()) {
            repertoire_->import_pgn(path);
        }
    });
    connect(repertoire_, &RepertoireScreen::import_requested, this,
            [this](const QString& path) { emit pgn_import_requested(path, repertoire_white_); });
    connect(repertoire_, &RepertoireScreen::navigate_requested, this, &MainWindow::repertoire_go);
    connect(repertoire_, &RepertoireScreen::choose_requested, this, &MainWindow::repertoire_choose);
    connect(repertoire_, &RepertoireScreen::remove_requested, this, &MainWindow::repertoire_remove);
    connect(drill_, &DrillScreen::colour_changed, this, [this] { enter_drill(); });
    connect(drill_, &DrillScreen::sync_requested, this, [this] { enter_drill(); });
    connect(drill_, &DrillScreen::start_requested, this, &MainWindow::drill_start);
    connect(drill_, &DrillScreen::next_requested, this, &MainWindow::drill_next);
    connect(drill_, &DrillScreen::grade_chosen, this, &MainWindow::drill_grade);

    setCentralWidget(new Columns(new BoardRow(eval_bar_, board_), move_list_, tabs_, screens));

    build_status_bar();
    apply_theme(settings_.theme());

    // The worker owns the store on its own thread for the window's life; every request and
    // every answer crosses by queued connection.
    db_worker_->moveToThread(&db_thread_);
    connect(&db_thread_, &QThread::finished, db_worker_, &QObject::deleteLater);
    connect(this, &MainWindow::open_requested, db_worker_, &DbWorker::open_database);
    connect(this, &MainWindow::query_requested, db_worker_, &DbWorker::query);
    connect(db_worker_, &DbWorker::database_opened, this, &MainWindow::database_opened);
    connect(db_worker_, &DbWorker::database_failed, this, &MainWindow::database_failed);
    connect(db_worker_, &DbWorker::position_ready, this, &MainWindow::position_ready);
    connect(this, &MainWindow::database_import_requested, db_worker_, &DbWorker::import_database);
    connect(db_worker_, &DbWorker::database_imported, this, &MainWindow::database_imported);
    connect(db_worker_, &DbWorker::database_import_failed, this,
            &MainWindow::database_import_failed);
    db_thread_.setObjectName(QStringLiteral("database"));
    db_thread_.start();
    if (!db_path_.isEmpty()) {
        emit open_requested(db_path_);
    } else {
        explore_->show_no_database(no_database_text(data_dir_));
    }

    // The engine worker likewise, with the one engine and its cache connection; the window
    // stops its search directly (cancel_before) and sends everything else queued.
    engine_worker_->moveToThread(&engine_thread_);
    connect(&engine_thread_, &QThread::finished, engine_worker_, &QObject::deleteLater);
    connect(this, &MainWindow::configure_requested, engine_worker_, &EngineWorker::configure);
    connect(this, &MainWindow::evaluate_requested, engine_worker_, &EngineWorker::evaluate);
    connect(engine_worker_, &EngineWorker::engine_state, this, &MainWindow::engine_state);
    connect(engine_worker_, &EngineWorker::lines_ready, this, &MainWindow::lines_ready);
    connect(engine_worker_, &EngineWorker::engine_failed, this, &MainWindow::engine_failed);
    engine_thread_.setObjectName(QStringLiteral("engine"));
    engine_thread_.start();
    emit configure_requested(settings_.engine());
    request_evaluation();

    // The spar worker likewise, with the live session, its own opening database connection,
    // its own engine for the off-book replies, and the profile store that saves the sessions.
    spar_worker_->moveToThread(&spar_thread_);
    connect(&spar_thread_, &QThread::finished, spar_worker_, &QObject::deleteLater);
    connect(this, &MainWindow::spar_configure_requested, spar_worker_, &SparWorker::configure);
    connect(this, &MainWindow::spar_start_requested, spar_worker_, &SparWorker::start);
    connect(this, &MainWindow::spar_move_requested, spar_worker_, &SparWorker::user_move);
    connect(this, &MainWindow::spar_end_requested, spar_worker_, &SparWorker::end_session);
    connect(this, &MainWindow::spar_list_requested, spar_worker_, &SparWorker::list_sessions);
    connect(this, &MainWindow::spar_replay_requested, spar_worker_, &SparWorker::replay_session);
    connect(this, &MainWindow::database_changed, spar_worker_, &SparWorker::set_database);
    connect(spar_worker_, &SparWorker::session_started, this, &MainWindow::spar_session_started);
    connect(spar_worker_, &SparWorker::move_accepted, this, &MainWindow::spar_move_accepted);
    connect(spar_worker_, &SparWorker::coach_moved, this, &MainWindow::spar_coach_moved);
    connect(spar_worker_, &SparWorker::session_ended, this, &MainWindow::spar_session_ended);
    connect(spar_worker_, &SparWorker::session_failed, this, &MainWindow::spar_failed);
    connect(spar_worker_, &SparWorker::sessions_ready, spar_, &SparScreen::show_sessions);
    connect(spar_worker_, &SparWorker::replay_ready, this, &MainWindow::spar_replay_ready);
    spar_thread_.setObjectName(QStringLiteral("spar"));
    spar_thread_.start();
    emit spar_configure_requested(settings_.engine(), settings_.spar().seconds);

    // The profile worker likewise, with the repertoires and cards and its own opening database
    // connection for coverage and the score.
    profile_worker_->moveToThread(&profile_thread_);
    connect(&profile_thread_, &QThread::finished, profile_worker_, &QObject::deleteLater);
    connect(this, &MainWindow::choice_requested, profile_worker_, &ProfileWorker::set_choice);
    connect(this, &MainWindow::choice_removal_requested, profile_worker_,
            &ProfileWorker::remove_choice);
    connect(this, &MainWindow::repertoire_requested, profile_worker_,
            &ProfileWorker::load_repertoire);
    connect(this, &MainWindow::coverage_requested, profile_worker_,
            &ProfileWorker::compute_coverage);
    connect(this, &MainWindow::scores_requested, profile_worker_, &ProfileWorker::score_position);
    connect(this, &MainWindow::pgn_import_requested, profile_worker_, &ProfileWorker::import_pgn);
    connect(this, &MainWindow::sync_requested, profile_worker_, &ProfileWorker::sync_cards);
    connect(this, &MainWindow::due_requested, profile_worker_, &ProfileWorker::due_cards);
    connect(this, &MainWindow::review_requested, profile_worker_, &ProfileWorker::record_review);
    connect(this, &MainWindow::database_changed, profile_worker_, &ProfileWorker::set_database);
    connect(this, &MainWindow::close_stores_requested, engine_worker_, &EngineWorker::close_stores,
            Qt::BlockingQueuedConnection);
    connect(this, &MainWindow::close_stores_requested, spar_worker_, &SparWorker::close_stores,
            Qt::BlockingQueuedConnection);
    connect(this, &MainWindow::end_session_requested, spar_worker_, &SparWorker::end_session,
            Qt::BlockingQueuedConnection);
    connect(this, &MainWindow::review_record_requested, profile_worker_,
            &ProfileWorker::record_review, Qt::BlockingQueuedConnection);
    connect(this, &MainWindow::profile_import_requested, profile_worker_,
            &ProfileWorker::import_profiles);
    connect(profile_worker_, &ProfileWorker::profiles_imported, this,
            &MainWindow::profile_import_done);
    connect(this, &MainWindow::export_requested, profile_worker_, &ProfileWorker::export_profiles);
    connect(profile_worker_, &ProfileWorker::repertoire_changed, this,
            &MainWindow::repertoire_changed);
    connect(profile_worker_, &ProfileWorker::repertoire_ready, this, &MainWindow::repertoire_ready);
    connect(profile_worker_, &ProfileWorker::coverage_ready, this, &MainWindow::coverage_ready);
    connect(profile_worker_, &ProfileWorker::scores_ready, this, &MainWindow::scores_ready);
    connect(profile_worker_, &ProfileWorker::pgn_imported, this, &MainWindow::pgn_imported);
    connect(profile_worker_, &ProfileWorker::cards_synced, this, &MainWindow::cards_synced);
    connect(profile_worker_, &ProfileWorker::cards_ready, this, &MainWindow::cards_ready);
    connect(profile_worker_, &ProfileWorker::review_recorded, this, &MainWindow::review_recorded);
    connect(profile_worker_, &ProfileWorker::profiles_exported, this, [this](const QString& path) {
        statusBar()->showMessage(tr("Your work is backed up in %1.").arg(path), 5000);
    });
    connect(profile_worker_, &ProfileWorker::export_failed, this, &MainWindow::show_profile_error);
    connect(profile_worker_, &ProfileWorker::profile_failed, this, &MainWindow::profile_failed);
    profile_thread_.setObjectName(QStringLiteral("profiles"));
    profile_thread_.start();
}

MainWindow::~MainWindow() {
    // A search in progress stops first, so the engine thread finishes at once; the worker's
    // engine then quits with it.
    engine_worker_->cancel_before(std::numeric_limits<int>::max());
    engine_thread_.quit();
    engine_thread_.wait();
    // A running session is saved as stopped: the coach's search in progress, if any, stops at
    // once, and the window waits for the worker to save the session before it joins the thread.
    if (spar_running_) {
        spar_worker_->stop_coach();
        emit end_session_requested();
    }
    spar_thread_.quit();
    spar_thread_.wait();
    // A correct answer's grade not yet recorded is recorded before the profile worker stops.
    if (card_.has_value() && answered_ && !recorded_) {
        recorded_ = true;
        const DrillCard card = *card_;
        const int grade = grade_;
        emit review_record_requested(card.id, card.white, card.key, grade);
    }
    profile_thread_.quit();
    profile_thread_.wait();
    db_thread_.quit();
    db_thread_.wait();
}

void MainWindow::build_toolbar() {
    // Icon and label buttons, then the Rating chooser, then the icon-only buttons at the right;
    // apply_theme gives the icons the theme's colours.
    auto* toolbar = addToolBar(tr("Board"));
    toolbar->setObjectName(QStringLiteral("toolbar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    new_game_action_ = toolbar->addAction(tr("New game"));
    new_game_action_->setObjectName(QStringLiteral("new_game_action"));
    themed_actions_.emplace_back(new_game_action_, QStringLiteral("plus"));
    connect(new_game_action_, &QAction::triggered, this, [this] {
        if (screen_ == Screen::repertoire) {
            repertoire_jump(0);
        } else {
            jump_to(0);
        }
    });
    back_action_ = toolbar->addAction(tr("Back"));
    back_action_->setObjectName(QStringLiteral("back_action"));
    back_action_->setShortcut(QKeySequence::Back);
    themed_actions_.emplace_back(back_action_, QStringLiteral("back"));
    connect(back_action_, &QAction::triggered, this, [this] {
        if (screen_ == Screen::repertoire && !repertoire_uci_.empty()) {
            repertoire_jump(static_cast<qsizetype>(repertoire_uci_.size()) - 1);
        } else if (screen_ == Screen::explore && !line_uci_.empty()) {
            jump_to(static_cast<qsizetype>(line_uci_.size()) - 1);
        }
    });
    QAction* flip = toolbar->addAction(tr("Flip board"));
    flip->setObjectName(QStringLiteral("flip_action"));
    themed_actions_.emplace_back(flip, QStringLiteral("flip"));
    connect(flip, &QAction::triggered, this, [this] {
        board_->flip();
        eval_bar_->set_flipped(board_->flipped());
    });
    toolbar->addSeparator();
    auto* rating = new QLabel(tr("Rating"));
    rating->setObjectName(QStringLiteral("rating_label"));
    toolbar->addWidget(rating);
    tier_selector_->setObjectName(QStringLiteral("tier_selector"));
    for (int index = 0; index < tier_count; ++index) {
        // The chooser's items stand alone, so each starts with a capital: "Under 1200".
        QString band = tier_text(index);
        band.replace(0, 1, band.at(0).toUpper());
        tier_selector_->addItem(band, index);
    }
    tier_selector_->setCurrentIndex(settings_.last_tier().value_or(default_tier));
    connect(tier_selector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        settings_.set_last_tier(index);
        request_query();
        if (screen_ == Screen::repertoire) {
            repertoire_refresh();
        }
    });
    toolbar->addWidget(tier_selector_);
    auto* stretch = new QWidget();
    stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(stretch);
    QAction* settings = toolbar->addAction(tr("Settings"));
    settings->setObjectName(QStringLiteral("settings_toolbar_action"));
    connect(settings, &QAction::triggered, this, &MainWindow::show_settings);
    themed_actions_.emplace_back(settings, QStringLiteral("gear"));
    if (auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(settings))) {
        button->setObjectName(QStringLiteral("settings_button"));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setToolTip(tr("Settings"));
    }
    add_toolbar_tooltips(*this);
}

void MainWindow::build_menus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    // The profile's export and import, in the user's words a backup and its restore.
    QAction* export_action = file->addAction(tr("&Back up your work…"));
    export_action->setObjectName(QStringLiteral("export_action"));
    connect(export_action, &QAction::triggered, this, [this] {
        // An export never replaces a file, which the chooser's save mode refuses.
        const QString path =
            choose_file_to_save(this, settings_, tr("Back up your work"), {QStringLiteral("*.db")},
                                QStringLiteral("chess-coach-backup.db"));
        if (!path.isEmpty()) {
            export_profile(path);
        }
    });
    import_action_ = file->addAction(tr("&Restore a backup…"));
    import_action_->setObjectName(QStringLiteral("import_action"));
    connect(import_action_, &QAction::triggered, this, [this] {
        const QString path =
            choose_file_to_open(this, settings_, tr("Restore a backup"), {QStringLiteral("*.db")});
        if (!path.isEmpty()) {
            import_profile(path);
        }
    });

    file->addSeparator();
    import_database_action_ = file->addAction(tr("Import &database…"));
    import_database_action_->setObjectName(QStringLiteral("import_database_action"));
    connect(import_database_action_, &QAction::triggered, this, [this] {
        const QString path = choose_file_to_open(
            this, settings_, tr("Choose an opening database to import"), {QStringLiteral("*.db")});
        if (!path.isEmpty()) {
            import_database(path);
        }
    });

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    QAction* settings = edit->addAction(tr("&Settings\u2026"));
    settings->setObjectName(QStringLiteral("settings_action"));
    settings->setMenuRole(QAction::PreferencesRole);
    connect(settings, &QAction::triggered, this, &MainWindow::show_settings);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    QMenu* theme = view->addMenu(tr("&Theme"));
    themes_ = new QActionGroup(this);
    light_action_ = theme->addAction(tr("Light"));
    dark_action_ = theme->addAction(tr("Dark"));
    light_action_->setObjectName(QStringLiteral("theme_light"));
    dark_action_->setObjectName(QStringLiteral("theme_dark"));
    for (QAction* action : {light_action_, dark_action_}) {
        action->setCheckable(true);
        themes_->addAction(action);
    }
    connect(light_action_, &QAction::triggered, this,
            [this] { choose_theme(QStringLiteral("light")); });
    connect(dark_action_, &QAction::triggered, this,
            [this] { choose_theme(QStringLiteral("dark")); });

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    QAction* how_to = help->addAction(tr("&How to use chess-coach"));
    how_to->setObjectName(QStringLiteral("help_action"));
    connect(how_to, &QAction::triggered, this, [this] {
        auto* dialog = new HelpDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->open();
    });
    QAction* about = help->addAction(tr("&About chess-coach"));
    about->setObjectName(QStringLiteral("about_action"));
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, &MainWindow::show_about);
    QAction* about_qt = help->addAction(tr("About &Qt"));
    about_qt->setObjectName(QStringLiteral("about_qt_action"));
    about_qt->setMenuRole(QAction::AboutQtRole);
    connect(about_qt, &QAction::triggered, this, [] { QApplication::aboutQt(); });
}

void MainWindow::build_status_bar() {
    // The opening database on the left, by its months and data version, and the engine on the
    // right.
    database_label_ = new QLabel();
    database_label_->setObjectName(QStringLiteral("database_status"));
    show_database_label();
    engine_label_ = new QLabel(engine_status(EngineState::not_started, QString()));
    engine_label_->setObjectName(QStringLiteral("engine_status"));
    statusBar()->setContentsMargins(8, 0, 8, 0);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(database_label_, 1);
    statusBar()->addPermanentWidget(engine_label_);
}

int MainWindow::tier() const { return tier_selector_->currentIndex(); }

void MainWindow::move_played(const QString& uci, const QString& san) {
    switch (screen_) {
    case Screen::explore:
        break;
    case Screen::spar:
        spar_user_moved(uci, san);
        return;
    case Screen::repertoire:
        repertoire_move_played(uci, san);
        return;
    case Screen::drill:
        drill_move_played(uci, san);
        return;
    }
    line_uci_.push_back(uci.toStdString());
    line_san_.append(san);
    line_keys_.append(board_->position().key());
    rebuild_move_list();
    request_query();
    request_evaluation();
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
    request_evaluation();
}

void MainWindow::rebuild_move_list() {
    // One row per move pair, white's move starting it: "1. e4 e5"; Explore's line, on the Spar
    // screen the session's moves or the replay's up to its ply, on Repertoire its line, and
    // nothing on Drill, whose card has no line.
    QStringList sans = line_san_;
    if (spar_active_ && replay_) {
        sans.clear();
        for (int ply = 0; ply < replay_ply_; ++ply) {
            sans << replay_->plies.at(ply).san;
        }
    } else if (spar_active_) {
        sans = spar_san_;
    } else if (screen_ == Screen::repertoire) {
        sans = repertoire_san_;
    } else if (screen_ == Screen::drill) {
        sans.clear();
    }
    move_list_->clear();
    for (qsizetype ply = 0; ply < sans.size(); ply += 2) {
        QString row = QStringLiteral("%1. %2").arg((ply / 2) + 1).arg(sans.at(ply));
        if (ply + 1 < sans.size()) {
            row += QLatin1Char(' ') + sans.at(ply + 1);
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

void MainWindow::show_database_label() {
    // The database by its months and data version once it is open, never by its file, which the
    // tooltip names with where it was found: "/…/openings.db (data directory)".
    if (!located_) {
        database_label_->setText(tr("No opening database"));
        database_label_->setToolTip(no_database_text(data_dir_));
        return;
    }
    database_label_->setToolTip(
        tr("%1 (%2)").arg(QDir::toNativeSeparators(QString::fromStdString(located_->path.string())),
                          located_->source));
    if (!database_open_) {
        database_label_->setText(tr("Loading the opening database\u2026"));
        return;
    }
    const QString held = month_range(months_);
    database_label_->setText(
        held.isEmpty() ? tr("Opening database: data version %1").arg(data_version_)
                       : tr("Opening database: %1, data version %2").arg(held, data_version_));
}

void MainWindow::database_opened(const QString& data_version, const QString& months) {
    database_open_ = true;
    data_version_ = data_version;
    months_ = months;
    show_database_label();
    request_query();
}

void MainWindow::database_failed(const QString& message) {
    database_open_ = false;
    data_version_.clear();
    months_.clear();
    database_label_->setText(tr("No opening database"));
    // The file the window chose did not open: say which, not where else it might have looked.
    QString empty = no_database_text(data_dir_);
    if (located_) {
        const QString file =
            QDir::toNativeSeparators(QString::fromStdString(located_->path.string()));
        empty = QFileInfo::exists(file)
                    ? tr("The opening database %1 could not be opened. Import another one with "
                         "File > Import database.")
                          .arg(file)
                    : tr("There is no opening database at %1. Check the file given with --db.")
                          .arg(file);
    }
    explore_->show_no_database(empty);
    eval_bar_->set_evaluation(std::nullopt, std::nullopt);
    auto* box = new QMessageBox(QMessageBox::Warning, tr("Opening database"), message,
                                QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("database_error"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

void MainWindow::position_ready(const ExploreData& explore) {
    // An answer for a position or a tier the window has left behind is dropped.
    if (explore.key != line_keys_.back() || explore.tier != tier()) {
        return;
    }
    explore_->show_data(explore, tier_text(explore.tier));
    stored_score_.reset();
    if (explore.position && explore.position->eval_depth) {
        stored_score_ = Score{
            .cp = explore.position->eval_cp,
            .mate = explore.position->eval_mate,
            .mated = std::nullopt,
        };
    }
    update_eval_bar();
}

void MainWindow::request_evaluation() {
    // The search in progress, for a position the board has left, stops; its answer and any older
    // request are dropped by their id.
    ++request_id_;
    engine_worker_->cancel_before(request_id_);
    if (screen_ != Screen::explore) {
        return; // Explore's engine idles while the board is another screen's
    }
    stored_score_.reset();
    engine_score_.reset();
    update_eval_bar();
    const Position& position = board_->position();
    emit evaluate_requested(position.key(), QString::fromStdString(position.fen()), request_id_);
}

void MainWindow::engine_state(EngineState state, const QString& name, const QString& detail) {
    engine_label_->setText(engine_status(state, name));
    // Explore's engine panel keeps the worker's words; a start in progress leaves it as it is.
    if (state == EngineState::searching) {
        explore_->show_engine_searching();
    } else if (state != EngineState::starting) {
        explore_->show_engine_state(detail);
    }
}

QString MainWindow::engine_status(EngineState state, const QString& name) {
    switch (state) {
    case EngineState::not_started:
        return tr("Engine not started");
    case EngineState::starting:
        return tr("Engine starting\u2026");
    case EngineState::ready:
        return tr("%1 ready").arg(name);
    case EngineState::searching:
        return tr("%1 thinking\u2026").arg(name);
    case EngineState::not_found:
        return tr("No engine found");
    case EngineState::failed:
        return tr("The engine did not start");
    }
    return {};
}

void MainWindow::lines_ready(quint64 key, int request_id, const QVector<EngineLine>& lines,
                             bool cached) {
    if (request_id != request_id_ || key != line_keys_.back()) {
        return;
    }
    explore_->show_engine_lines(lines, cached);
    engine_score_.reset();
    if (!lines.isEmpty()) {
        const EngineLine& first = lines.front();
        engine_score_ = Score{
            .cp = first.cp ? std::optional<qint64>(*first.cp) : std::nullopt,
            .mate = first.mate ? std::optional<qint64>(*first.mate) : std::nullopt,
            .mated = first.mated,
        };
    }
    update_eval_bar();
}

void MainWindow::engine_failed(const QString& message) { explore_->show_engine_state(message); }

void MainWindow::update_eval_bar() {
    if (screen_ != Screen::explore) {
        eval_bar_->set_evaluation(std::nullopt, std::nullopt); // the bar is Explore's position's
        return;
    }
    const std::optional<Score>& score = stored_score_ ? stored_score_ : engine_score_;
    if (!score) {
        eval_bar_->set_evaluation(std::nullopt, std::nullopt);
        return;
    }
    eval_bar_->set_evaluation(score->cp, score->mate, score->mated);
}

void MainWindow::show_settings() {
    auto* dialog =
        new SettingsDialog(settings_.engine(), settings_.spar(), settings_.repertoire_depth(),
                           sounds_.enabled(), settings_, resources_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog] {
        // Applying saves the settings and restarts the engines with them at the next search; a
        // new move limit applies from the next session.
        const EngineSettings engine = dialog->engine();
        const SparSettings spar = dialog->spar();
        settings_.set_engine(engine);
        settings_.set_spar(spar);
        settings_.set_repertoire_depth(dialog->repertoire_depth());
        set_sounds_enabled(dialog->sounds_enabled());
        emit configure_requested(engine);
        emit spar_configure_requested(engine, spar.seconds);
        request_evaluation();
        if (screen_ == Screen::repertoire) {
            repertoire_refresh(); // the coverage to the new depth
        }
    });
    dialog->open();
}

void MainWindow::set_sounds_enabled(bool enabled) {
    settings_.set_sounds_enabled(enabled);
    sounds_.set_enabled(enabled);
}

void MainWindow::export_profile(const QString& path) { emit export_requested(path); }

void MainWindow::import_profile(const QString& path) {
    // Every other connection to profiles.db closes first: the engine worker's search stops so
    // that it answers at once, and the engine and spar workers drop their stores. The profile
    // worker then closes its own and replaces the file on its thread, the copy and the busy
    // timeout being no work for the GUI thread; both imports are off until it answers.
    ++request_id_;
    engine_worker_->cancel_before(request_id_);
    if (answered_ && !recorded_) {
        drill_record();
    }
    emit close_stores_requested();
    // The busy dialog keeps the window from sending anything while the file is swapped: no move
    // opens the engine cache and no End the game saves a session meanwhile.
    begin_import(tr("Restoring your work\u2026"), QStringLiteral("profile_import_progress"));
    emit profile_import_requested(path);
}

void MainWindow::profile_import_done(const QString& path, const QString& error) {
    end_import();
    // A store opened while the import ran holds the replaced file: they all open the path again.
    emit close_stores_requested();
    if (!error.isEmpty()) {
        show_profile_error(error);
    } else {
        statusBar()->showMessage(tr("Your work is restored from %1.").arg(path), 5000);
    }
    // Every screen reads the profile again.
    switch (screen_) {
    case Screen::explore:
        request_evaluation();
        break;
    case Screen::spar:
        emit spar_list_requested();
        break;
    case Screen::repertoire:
        repertoire_refresh();
        break;
    case Screen::drill:
        leave_drill();
        enter_drill();
        break;
    }
}

void MainWindow::import_database(const QString& path) {
    // The database worker closes its store, copies the file into place and opens it, all on its
    // thread; meanwhile a busy dialog shows, since a copy has no progress to give.
    database_open_ = false;
    begin_import(tr("Importing the opening database\u2026"),
                 QStringLiteral("database_import_progress"));
    emit database_import_requested(data_dir_, path, db_path_);
}

void MainWindow::begin_import(const QString& text, const QString& name) {
    // A window-modal busy dialog, which takes the window's input until the import answers, and
    // neither import can start again meanwhile.
    set_imports_enabled(false);
    import_progress_ = new QProgressDialog(text, QString(), 0, 0, this);
    import_progress_->setObjectName(name);
    import_progress_->setWindowModality(Qt::WindowModal);
    import_progress_->setCancelButton(nullptr);
    import_progress_->setMinimumDuration(0);
    import_progress_->installEventFilter(this); // Escape and closing do nothing: see eventFilter
    import_progress_->show();
}

void MainWindow::end_import() {
    if (!import_progress_.isNull()) {
        // Hidden, not closed: the dialog refuses a close while the import runs. The filter goes
        // first: the dying dialog's child-removed events would otherwise reach eventFilter, whose
        // QPointer downcast of a half-destroyed object UBSan's vptr check reports.
        import_progress_->removeEventFilter(this);
        import_progress_->hide();
        import_progress_->deleteLater();
    }
    set_imports_enabled(true);
}

void MainWindow::database_imported(const QString& path, qint64 data_version,
                                   const QString& months) {
    end_import();
    // Every worker reads the new file from now on; the database worker, which has it open,
    // answers database_opened next, which refreshes Explore, the status bar and About.
    located_ = DatabaseChoice{.path = path.toStdString(), .source = tr("data directory")};
    db_path_ = path;
    show_database_label();
    emit database_changed(db_path_);
    if (screen_ == Screen::repertoire) {
        repertoire_refresh();
    }
    // "It holds games from October 2026." when meta names its months.
    const QString held = month_range(months);
    QString text = tr("The new opening database is ready (data version %1).").arg(data_version);
    if (!held.isEmpty()) {
        text += QLatin1Char(' ') + tr("It holds games from %1.").arg(held);
    }
    auto* box = new QMessageBox(QMessageBox::Information, tr("Opening database imported"), text,
                                QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("database_imported"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

void MainWindow::database_import_failed(const QString& message) {
    end_import();
    auto* box = new QMessageBox(QMessageBox::Warning, tr("Opening database not imported"), message,
                                QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("database_error"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // The busy dialog stays until the import answers: Escape would reject it and a close would
    // close it, so both are eaten.
    const auto type = event->type();
    if ((type == QEvent::Close || type == QEvent::KeyPress) && !import_progress_.isNull() &&
        watched == import_progress_.data()) {
        const auto* key = dynamic_cast<const QKeyEvent*>(event);
        if (type == QEvent::Close) {
            event->ignore();
            return true;
        }
        if (type == QEvent::KeyPress && key != nullptr && key->key() == Qt::Key_Escape) {
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::set_imports_enabled(bool enabled) {
    import_action_->setEnabled(enabled);
    import_database_action_->setEnabled(enabled);
}

void MainWindow::show_profile_error(const QString& message) {
    auto* box = new QMessageBox(QMessageBox::Warning, tr("Your saved work"), message,
                                QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("profile_error"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

Position MainWindow::explore_position() const {
    Position position = Position::start();
    for (const std::string& uci : line_uci_) {
        static_cast<void>(position.play_uci(uci));
    }
    return position;
}

void MainWindow::screen_changed(int index) {
    const auto next = static_cast<Screen>(index);
    if (next == screen_) {
        return;
    }
    const Screen previous = screen_;
    screen_ = next;
    spar_active_ = next == Screen::spar;
    new_game_action_->setEnabled(next == Screen::explore || next == Screen::repertoire);
    back_action_->setEnabled(next == Screen::explore || next == Screen::repertoire);
    if (previous == Screen::drill) {
        leave_drill();
    }
    if (previous == Screen::explore) {
        // Explore's engine idles while the board is another screen's, and its evaluation bar,
        // which is Explore's position's, empties; its line and its side of the board wait as
        // they were.
        ++request_id_;
        engine_worker_->cancel_before(request_id_);
        explore_flipped_ = board_->flipped();
    }
    update_eval_bar();
    switch (next) {
    case Screen::explore:
        if (board_->flipped() != explore_flipped_) {
            board_->flip();
            eval_bar_->set_flipped(explore_flipped_);
        }
        board_->set_position(explore_position());
        rebuild_move_list();
        request_query();
        request_evaluation();
        break;
    case Screen::spar: {
        Position position = Position::start();
        const std::vector<std::string> moves =
            replay_ ? std::vector<std::string>() : spar_uci_; // a replay restarts at its ply 0
        for (const std::string& uci : moves) {
            static_cast<void>(position.play_uci(uci));
        }
        board_->set_position(position);
        // The board faces the user: the replay's colour, the running session's, or the one
        // chosen for the next.
        if (replay_) {
            face(replay_->user_white);
        } else {
            face(spar_running_ ? spar_white_ : spar_->user_white());
        }
        replay_ply_ = 0;
        if (replay_) {
            spar_replay_step(0);
        }
        rebuild_move_list();
        emit spar_list_requested();
        break;
    }
    case Screen::repertoire:
        enter_repertoire();
        break;
    case Screen::drill:
        enter_drill();
        break;
    }
    tier_selector_->setEnabled(!(spar_active_ && spar_running_)); // Explore's tier is its own
    update_board_input();
}

void MainWindow::update_board_input() {
    switch (screen_) {
    case Screen::explore:
    case Screen::repertoire:
        board_->setEnabled(true);
        break;
    case Screen::spar:
        board_->setEnabled(spar_running_ && spar_started_ && !spar_waiting_ &&
                           !spar_->interruption_shown() && !replay_);
        break;
    case Screen::drill:
        board_->setEnabled(drilling_ && card_.has_value() && !answered_);
        break;
    }
}

void MainWindow::face(bool white) {
    if (board_->flipped() == white) {
        board_->flip();
        eval_bar_->set_flipped(board_->flipped());
    }
}

void MainWindow::spar_start(bool user_white) {
    replay_.reset();
    spar_->end_replay();
    spar_->hide_interruption();
    spar_running_ = true;
    spar_white_ = user_white;
    spar_started_ = false;
    spar_waiting_ = true;
    spar_uci_.clear();
    spar_san_.clear();
    tier_selector_->setEnabled(false);
    spar_->set_running(true);
    spar_->show_message(tr("Starting a practice game\u2026"));
    board_->set_position(Position::start());
    face(user_white);
    rebuild_move_list();
    const quint64 seed =
        settings_.spar_seed().value_or(static_cast<quint64>(QDateTime::currentSecsSinceEpoch()));
    emit spar_start_requested(tier(), user_white, seed, settings_.spar().end_move);
    update_board_input();
}

void MainWindow::spar_user_moved(const QString& uci, const QString& san) {
    if (applying_coach_ || !spar_running_) {
        return;
    }
    spar_uci_.push_back(uci.toStdString());
    spar_san_.append(san);
    spar_waiting_ = true;
    rebuild_move_list();
    update_board_input();
    emit spar_move_requested(uci);
}

void MainWindow::spar_session_started(quint64 /*seed*/, const QString& /*fen*/,
                                      const SparState& state) {
    spar_started_ = true;
    spar_waiting_ = !state.user_to_move;
    spar_->show_state(state);
    update_board_input();
}

void MainWindow::spar_move_accepted(const QString& /*san*/, const QString& /*uci*/,
                                    const std::optional<SparInterruption>& interruption,
                                    const SparState& state) {
    spar_->show_state(state);
    if (interruption) {
        spar_->show_interruption(*interruption);
        sounds_.play(SoundEvent::interruption);
    }
    update_board_input();
}

void MainWindow::spar_coach_moved(const QString& san, const QString& uci, const SparState& state) {
    spar_uci_.push_back(uci.toStdString());
    spar_san_.append(san);
    if (spar_active_) {
        applying_coach_ = true;
        board_->play_move(uci);
        applying_coach_ = false;
        rebuild_move_list();
    }
    spar_->show_state(state);
    spar_waiting_ = !state.user_to_move;
    update_board_input();
}

void MainWindow::spar_session_ended(const SparSummary& summary) {
    spar_running_ = false;
    spar_started_ = false;
    spar_waiting_ = false;
    tier_selector_->setEnabled(true);
    spar_->set_running(false);
    spar_->show_message(tr("The practice game is over."));
    spar_->show_summary(summary);
    update_board_input();
}

void MainWindow::spar_failed(const QString& message) {
    spar_->show_message(message);
    if (spar_running_ && !spar_started_) {
        // The session never started.
        spar_running_ = false;
        spar_waiting_ = false;
        tier_selector_->setEnabled(true);
        spar_->set_running(false);
    }
    update_board_input();
}

void MainWindow::spar_replay_ready(const SparReplay& replay) {
    replay_ = replay;
    replay_ply_ = 0;
    face(replay.user_white);
    spar_replay_step(0);
}

void MainWindow::spar_replay_step(int delta) {
    if (!replay_.has_value()) {
        return;
    }
    const SparReplay& replay = *replay_;
    const auto count = static_cast<int>(replay.plies.size());
    replay_ply_ = std::clamp(replay_ply_ + delta, 0, count);
    Position position = Position::start();
    for (int ply = 0; ply < replay_ply_; ++ply) {
        static_cast<void>(position.play_uci(replay.plies.at(ply).uci.toStdString()));
    }
    std::optional<SparInterruption> interruption;
    if (replay_ply_ > 0) {
        interruption = replay.plies.at(replay_ply_ - 1).interruption;
    }
    if (spar_active_) {
        board_->set_position(position);
        rebuild_move_list();
    }
    spar_->show_replay(replay_ply_, count, interruption);
    update_board_input();
}

void MainWindow::choose_theme(const QString& theme) {
    settings_.set_theme(theme);
    apply_theme(theme);
}

void MainWindow::apply_theme(const QString& theme) {
    // The palette first, so that the stylesheet's polish starts from the theme's colours.
    const bool dark = theme == QStringLiteral("dark");
    const StudyPalette& palette = StudyPalette::named(theme);
    QApplication::setPalette(qt_palette(palette));
    QFile stylesheet(dark ? QStringLiteral(":/themes/dark.qss")
                          : QStringLiteral(":/themes/light.qss"));
    if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
    }
    explore_->set_palette(palette);
    spar_->set_palette(palette);
    board_->set_theme(dark ? BoardTheme::dark() : BoardTheme::light());
    eval_bar_->set_palette(palette);
    for (const auto& [action, icon] : themed_actions_) {
        action->setIcon(themed_icon(icon, palette));
    }
    (dark ? dark_action_ : light_action_)->setChecked(true);
}

void MainWindow::show_about() {
    auto* dialog = new AboutDialog(data_version_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

} // namespace chesscoach::app

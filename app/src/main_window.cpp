// The main window (main_window.hpp).
#include "main_window.hpp"

#include <chesscoach/position.hpp>
#include <chesscoach/profile_store.hpp>
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
#include "profile_worker.hpp"
#include "repertoire_screen.hpp"
#include "settings_dialog.hpp"
#include "spar_screen.hpp"
#include "spar_worker.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace chesscoach::app {

QString MainWindow::tier_text(int tier) {
    // Tier's values are the indices 0 to tier_count - 1.
    const std::string_view label = tier_label(static_cast<Tier>(tier));
    return QString::fromUtf8(label.data(), static_cast<qsizetype>(label.size()));
}

MainWindow::MainWindow(const QString& data_dir, QString db, QWidget* parent)
    : QMainWindow(parent), data_dir_(data_dir), settings_(data_dir), db_path_(std::move(db)),
      db_worker_(new DbWorker()), engine_worker_(new EngineWorker(data_dir)),
      spar_worker_(new SparWorker(data_dir, db_path_)),
      profile_worker_(new ProfileWorker(data_dir, db_path_)), board_(new BoardView()),
      eval_bar_(new EvalBar()), explore_(new ExploreScreen()), spar_(new SparScreen()),
      repertoire_(new RepertoireScreen()), drill_(new DrillScreen()), tabs_(new QTabBar()),
      move_list_(new QListWidget()), tier_selector_(new QComboBox()),
      grade_timer_(new QTimer(this)) {
    register_explore_types();
    register_engine_types();
    register_spar_types();
    register_profile_types();
    setWindowTitle(tr("chess-coach %1").arg(QString::fromStdString(to_string(current_version()))));
    resize(1100, 720);
    line_keys_.append(Position::start().key());

    build_toolbar();
    build_menus();

    move_list_->setObjectName(QStringLiteral("move_list"));
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
    auto* screens = new QStackedWidget();
    screens->setObjectName(QStringLiteral("screens"));
    tabs_->addTab(tr("Explore"));
    screens->addWidget(explore_);
    tabs_->addTab(tr("Spar"));
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
    grade_timer_->setSingleShot(true);
    grade_timer_->setInterval(2000);
    connect(grade_timer_, &QTimer::timeout, this, &MainWindow::drill_record);

    auto* board_row = new QHBoxLayout();
    board_row->addWidget(eval_bar_);
    board_row->addWidget(board_, 1);
    auto* left = new QVBoxLayout();
    left->addLayout(board_row, 3);
    left->addWidget(move_list_, 1);
    auto* right = new QVBoxLayout();
    right->addWidget(tabs_);
    right->addWidget(screens, 1);
    auto* central = new QWidget();
    auto* columns = new QHBoxLayout(central);
    columns->addLayout(left, 1);
    columns->addLayout(right, 1);
    setCentralWidget(central);

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
    db_thread_.setObjectName(QStringLiteral("database"));
    db_thread_.start();
    if (!db_path_.isEmpty()) {
        emit open_requested(db_path_);
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
        statusBar()->showMessage(tr("Profile exported to %1").arg(path), 5000);
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
        QMetaObject::invokeMethod(spar_worker_, &SparWorker::end_session,
                                  Qt::BlockingQueuedConnection);
    }
    spar_thread_.quit();
    spar_thread_.wait();
    // A correct answer's grade not yet recorded is recorded before the profile worker stops.
    if (card_.has_value() && answered_ && !recorded_) {
        recorded_ = true;
        const DrillCard card = *card_;
        const int grade = grade_;
        QMetaObject::invokeMethod(
            profile_worker_,
            [this, card, grade] {
                profile_worker_->record_review(card.id, card.white, card.key, grade);
            },
            Qt::BlockingQueuedConnection);
    }
    profile_thread_.quit();
    profile_thread_.wait();
    db_thread_.quit();
    db_thread_.wait();
}

void MainWindow::build_toolbar() {
    auto* toolbar = addToolBar(tr("Board"));
    toolbar->setObjectName(QStringLiteral("toolbar"));
    toolbar->setMovable(false);
    new_game_action_ = toolbar->addAction(tr("New game"));
    new_game_action_->setObjectName(QStringLiteral("new_game_action"));
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
    connect(back_action_, &QAction::triggered, this, [this] {
        if (screen_ == Screen::repertoire && !repertoire_uci_.empty()) {
            repertoire_jump(static_cast<qsizetype>(repertoire_uci_.size()) - 1);
        } else if (screen_ == Screen::explore && !line_uci_.empty()) {
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
    tier_selector_->setCurrentIndex(settings_.last_tier().value_or(default_tier));
    connect(tier_selector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        settings_.set_last_tier(index);
        request_query();
        if (screen_ == Screen::repertoire) {
            repertoire_refresh();
        }
    });
    toolbar->addWidget(tier_selector_);
}

void MainWindow::build_menus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* export_action = file->addAction(tr("&Export profile…"));
    export_action->setObjectName(QStringLiteral("export_action"));
    connect(export_action, &QAction::triggered, this, [this] {
        // An export never replaces a file, so the dialog does not offer to.
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export the profile"), QStringLiteral("chess-coach-profile.db"),
            tr("chess-coach profiles (*.db);;All files (*)"), nullptr,
            QFileDialog::DontConfirmOverwrite);
        if (!path.isEmpty()) {
            export_profile(path);
        }
    });
    QAction* import_action = file->addAction(tr("&Import profile…"));
    import_action->setObjectName(QStringLiteral("import_action"));
    connect(import_action, &QAction::triggered, this, [this] {
        const QString path =
            QFileDialog::getOpenFileName(this, tr("Import a profile"), QString(),
                                         tr("chess-coach profiles (*.db);;All files (*)"));
        if (!path.isEmpty()) {
            import_profile(path);
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
    database_label_ =
        new QLabel(db_path_.isEmpty() ? tr("No database") : QFileInfo(db_path_).fileName());
    database_label_->setObjectName(QStringLiteral("database_status"));
    database_label_->setToolTip(db_path_);
    data_version_label_ = new QLabel();
    data_version_label_->setObjectName(QStringLiteral("data_version_status"));
    data_version_label_->hide();
    engine_label_ = new QLabel(tr("Engine: not started"));
    engine_label_->setObjectName(QStringLiteral("engine_status"));
    statusBar()->addWidget(database_label_);
    statusBar()->addWidget(data_version_label_);
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

void MainWindow::engine_state(const QString& text) {
    engine_label_->setText(tr("Engine: %1").arg(text));
    if (text == EngineWorker::tr("searching")) {
        explore_->show_engine_searching();
    } else {
        explore_->show_engine_state(text);
    }
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
    auto* dialog = new SettingsDialog(settings_.engine(), settings_.spar(),
                                      settings_.repertoire_depth(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog] {
        // Applying saves the settings and restarts the engines with them at the next search; a
        // new move limit applies from the next session.
        const EngineSettings engine = dialog->engine();
        const SparSettings spar = dialog->spar();
        settings_.set_engine(engine);
        settings_.set_spar(spar);
        settings_.set_repertoire_depth(dialog->repertoire_depth());
        emit configure_requested(engine);
        emit spar_configure_requested(engine, spar.seconds);
        request_evaluation();
        if (screen_ == Screen::repertoire) {
            repertoire_refresh(); // the coverage to the new depth
        }
    });
    dialog->open();
}

void MainWindow::export_profile(const QString& path) { emit export_requested(path); }

void MainWindow::import_profile(const QString& path) {
    // Every connection to profiles.db closes first: the engine worker's search stops so that it
    // answers at once, and each worker closes its store before the file is replaced here; each
    // reopens it at its next use.
    ++request_id_;
    engine_worker_->cancel_before(request_id_);
    if (answered_ && !recorded_) {
        drill_record();
    }
    QMetaObject::invokeMethod(engine_worker_, &EngineWorker::close_stores,
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(spar_worker_, &SparWorker::close_stores,
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(profile_worker_, &ProfileWorker::close_stores,
                              Qt::BlockingQueuedConnection);
    const auto imported =
        ProfileStore::import_profiles(data_dir_.toStdString(), path.toStdString());
    if (!imported) {
        show_profile_error(
            tr("The profile was not imported: %1.").arg(ProfileWorker::describe(imported.error())));
    } else {
        statusBar()->showMessage(tr("Profile imported from %1").arg(path), 5000);
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

void MainWindow::show_profile_error(const QString& message) {
    auto* box =
        new QMessageBox(QMessageBox::Warning, tr("Profile"), message, QMessageBox::Ok, this);
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
    spar_->show_message(tr("Starting\u2026"));
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
    spar_->show_message(tr("Ended"));
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
    const bool dark = theme == QStringLiteral("dark");
    QFile stylesheet(dark ? QStringLiteral(":/themes/dark.qss")
                          : QStringLiteral(":/themes/light.qss"));
    if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
    }
    board_->set_theme(dark ? BoardTheme::dark() : BoardTheme::light());
    (dark ? dark_action_ : light_action_)->setChecked(true);
}

void MainWindow::show_about() {
    auto* dialog = new AboutDialog(data_version_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

} // namespace chesscoach::app

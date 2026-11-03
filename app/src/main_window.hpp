// The main window (docs/reference/app.md, **Shell**): a toolbar with New game, Back, Flip board
// and the tier selector; the evaluation bar and the board on the left with the move list below
// them; the screen switcher on the right over Explore, Spar, Repertoire and Drill; the File >
// Export and Import profile, Edit > Settings, View > Theme and Help menus; the database and the
// engine in the status bar. The board belongs to the current screen: it keeps Explore's line and
// sends each position at the selected tier to the database worker and each position to the
// engine worker; it runs Spar's sessions through the spar worker; and it builds the repertoires
// and drills their cards through the profile worker, each worker on its own thread, and shows
// their answers. It never touches the opening store, the profile store or an engine itself, but
// for replacing profiles.db on import once every worker has closed its connection. GUI thread
// only. It writes settings.ini itself (settings.hpp) and sends Qt's own settings to the data
// directory; the workers write profiles.db; nothing is written outside the data directory.
#pragma once

#include <chesscoach/position.hpp>

#include "engine_worker.hpp"
#include "explore_data.hpp"
#include "locations.hpp"
#include "profile_data.hpp"
#include "settings.hpp"
#include "spar_data.hpp"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <QtGlobal>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QActionGroup;
class QTabBar;
class QComboBox;
class QLabel;
class QListWidget;
class QProgressDialog;
class QTimer;

namespace chesscoach::app {

class BoardView;
class DbWorker;
class DrillScreen;
class EvalBar;
class ExploreScreen;
class ProfileWorker;
class RepertoireScreen;
class SparScreen;
class SparWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    // The tier the selector shows when the settings name none.
    static constexpr int default_tier = 2;

    // data_dir holds settings.ini (--data-dir, or the platform's application data location);
    // db is the opening database file (--db), empty when none was given, which the worker opens.
    MainWindow(const QString& data_dir, const QString& db, QWidget* parent = nullptr);
    ~MainWindow() override;
    Q_DISABLE_COPY_MOVE(MainWindow)

    // The tier selected, 0 to 6.
    [[nodiscard]] int tier() const;
    // The moves played from the start position to the board's position, in UCI.
    [[nodiscard]] const std::vector<std::string>& line() const { return line_uci_; }

    // File > Export profile and Import profile once their file dialogs have answered: a copy of
    // profiles.db written to path; profiles.db replaced by the file at path, every screen then
    // reading it. Errors in a message box (profile_error).
    void export_profile(const QString& path);
    void import_profile(const QString& path);
    // File > Import database once its chooser has answered: the opening database at path put in
    // place as <data-dir>/openings.db by the database worker (core's import_opening_database) and
    // read by every worker from then on, a busy dialog showing meanwhile (database_import_progress)
    // and both imports disabled. Errors in a message box (database_error); the months it holds in
    // another (database_imported).
    void import_database(const QString& path);

    // Keeps an import's busy dialog up: Escape and a close do nothing to it.
    bool eventFilter(QObject* watched, QEvent* event) override;

  signals:
    void open_requested(const QString& path);
    void query_requested(const QVector<quint64>& line_keys, int tier);
    void configure_requested(const chesscoach::app::EngineSettings& settings);
    void evaluate_requested(quint64 key, const QString& fen, int request_id);
    void spar_configure_requested(const chesscoach::app::EngineSettings& settings, double seconds);
    void spar_start_requested(int tier, bool user_white, quint64 seed, int end_move);
    void spar_move_requested(const QString& uci);
    void spar_end_requested();
    void spar_list_requested();
    void spar_replay_requested(qint64 id);
    void choice_requested(bool white, quint64 key, const QString& fen, const QString& uci);
    void choice_removal_requested(bool white, quint64 key);
    void repertoire_requested(bool white);
    void coverage_requested(bool white, int tier, int depth);
    void scores_requested(quint64 key, int tier);
    void pgn_import_requested(const QString& path, bool white);
    void sync_requested(bool white);
    void due_requested(bool white);
    void review_requested(qint64 card_id, bool white, quint64 key, int grade);
    void export_requested(const QString& path);
    void database_changed(const QString& path);
    void database_import_requested(const QString& data_dir, const QString& source,
                                   const QString& current);

  private:
    void build_toolbar();
    void build_menus();
    void build_status_bar();
    void move_played(const QString& uci, const QString& san);
    void screen_changed(int index);
    [[nodiscard]] Position explore_position() const;
    void update_board_input();
    void spar_start(bool user_white);
    void spar_user_moved(const QString& uci, const QString& san);
    void spar_session_started(quint64 seed, const QString& fen, const SparState& state);
    void spar_move_accepted(const QString& san, const QString& uci,
                            const std::optional<SparInterruption>& interruption,
                            const SparState& state);
    void spar_coach_moved(const QString& san, const QString& uci, const SparState& state);
    void spar_session_ended(const SparSummary& summary);
    void spar_failed(const QString& message);
    void spar_replay_ready(const SparReplay& replay);
    void spar_replay_step(int delta);
    void face(bool white);
    [[nodiscard]] static QString tier_text(int tier);
    // Repertoire (main_window_repertoire.cpp).
    void enter_repertoire();
    void repertoire_move_played(const QString& uci, const QString& san);
    void repertoire_go(const QStringList& line);
    void repertoire_jump(qsizetype ply);
    void repertoire_refresh();
    void repertoire_update_position();
    void repertoire_show_choice();
    void repertoire_colour_changed(bool white);
    void repertoire_choose(const QString& uci);
    void repertoire_remove();
    void repertoire_changed(bool white, const QString& message);
    void repertoire_ready(const RepertoireData& repertoire);
    void coverage_ready(const CoverageData& coverage);
    void scores_ready(const ScoreData& scores);
    void pgn_imported(const PgnImportInfo& import);
    [[nodiscard]] Position repertoire_position() const;
    // Drill (main_window_drill.cpp).
    void enter_drill();
    void leave_drill();
    void drill_start();
    void drill_move_played(const QString& uci, const QString& san);
    void drill_record();
    void drill_next();
    void drill_grade(int grade);
    void cards_synced(bool white, int created, int reset, int deleted);
    void cards_ready(const CardsData& cards);
    void review_recorded(qint64 card_id, qint64 due_at);
    void profile_failed(const QString& message);
    void show_profile_error(const QString& message);
    void show_database_label();
    void database_imported(const QString& path, qint64 data_version, const QString& months);
    void database_import_failed(const QString& message);
    void end_database_import();
    void set_imports_enabled(bool enabled);
    void jump_to(qsizetype ply);
    void rebuild_move_list();
    void request_query();
    void request_evaluation();
    void engine_state(const QString& text);
    void lines_ready(quint64 key, int request_id, const QVector<EngineLine>& lines, bool cached);
    void engine_failed(const QString& message);
    void update_eval_bar();
    void show_settings();
    void database_opened(const QString& data_version);
    void database_failed(const QString& message);
    void position_ready(const ExploreData& explore);
    void choose_theme(const QString& theme);
    void apply_theme(const QString& theme);
    void show_about();

    QString data_dir_;
    Settings settings_;
    // The app bundle's resources, worked out once here and handed to the workers.
    std::optional<std::filesystem::path> resources_;
    std::optional<DatabaseChoice> located_; // the opening database, and where it was found
    QString db_path_;
    QString data_version_;
    bool database_open_ = false;
    QThread db_thread_;
    DbWorker* db_worker_; // lives on db_thread_, deleted when it finishes
    QThread engine_thread_;
    EngineWorker* engine_worker_; // lives on engine_thread_, deleted when it finishes
    QThread spar_thread_;
    SparWorker* spar_worker_; // lives on spar_thread_, deleted when it finishes
    QThread profile_thread_;
    ProfileWorker* profile_worker_; // lives on profile_thread_, deleted when it finishes
    int request_id_ = 0;            // the evaluation the window waits for
    BoardView* board_;
    EvalBar* eval_bar_;
    ExploreScreen* explore_;
    SparScreen* spar_;
    RepertoireScreen* repertoire_;
    DrillScreen* drill_;
    QTabBar* tabs_;
    QAction* new_game_action_ = nullptr;
    QAction* back_action_ = nullptr;
    QAction* import_action_ = nullptr;          // File > Import profile…
    QAction* import_database_action_ = nullptr; // File > Import database…
    QPointer<QProgressDialog> import_progress_; // while a database import runs
    QListWidget* move_list_;
    QComboBox* tier_selector_;
    QLabel* database_label_ = nullptr;
    QLabel* data_version_label_ = nullptr;
    QLabel* engine_label_ = nullptr;
    QActionGroup* themes_ = nullptr;
    QAction* light_action_ = nullptr;
    QAction* dark_action_ = nullptr;
    std::vector<std::string> line_uci_;
    QStringList line_san_;
    QVector<quint64> line_keys_; // the start position's key, then one per move
    // The evaluation bar's two sources for the current position: the stored evaluation, which
    // wins, and the engine's first line.
    struct Score {
        std::optional<qint64> cp;
        std::optional<qint64> mate;
        std::optional<Color> mated; // the engine's: the side to move is checkmated
    };
    std::optional<Score> stored_score_;
    std::optional<Score> engine_score_;

    // Spar: the board belongs to the Spar screen while it is the current one, and Explore's line
    // is left as it was.
    bool spar_active_ = false;
    bool spar_running_ = false;   // a session runs on the worker
    bool spar_white_ = true;      // the colour the user plays in it
    bool spar_started_ = false;   // the worker has answered session_started for it
    bool spar_waiting_ = false;   // the coach is to move
    bool applying_coach_ = false; // the board is playing the coach's move, not the user's
    std::vector<std::string> spar_uci_;
    QStringList spar_san_;
    std::optional<SparReplay> replay_;
    int replay_ply_ = 0;

    // The screen the board belongs to, and Explore's side of the board, which Flip board sets
    // there; the other screens turn the board to the side they play.
    enum class Screen : std::uint8_t { explore, spar, repertoire, drill };
    Screen screen_ = Screen::explore;
    bool explore_flipped_ = false;

    // Repertoire: the line the board is at, from the start, and the colour's choices by key.
    bool repertoire_white_ = true;
    std::vector<std::string> repertoire_uci_;
    QStringList repertoire_san_;
    QHash<quint64, QString> choices_;

    // Drill: the card shown, its answer and its grade, which is recorded once.
    bool drilling_ = false;
    std::optional<DrillCard> card_;
    bool answered_ = false;
    bool recorded_ = false;
    int grade_ = 0;
    bool applying_answer_ = false; // the board is showing the card's move, not the user's
    QTimer* grade_timer_;          // ends the time to change a correct answer's grade
    int reviewed_ = 0;
    int correct_ = 0;
    int again_ = 0;
};

} // namespace chesscoach::app

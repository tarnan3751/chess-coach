// The main window (docs/reference/app.md, **Shell**): a toolbar with New game, Back, Flip board
// and the tier selector; the evaluation bar and the board on the left with the move list below
// them; the screen switcher on the right over the four screens, Explore live and the others
// placeholders; the Edit > Settings, View > Theme and Help menus; the database and the engine in
// the status bar. It keeps the line played on the board, sends each position at the selected
// tier to the database worker and each position to the engine worker, each on its own thread,
// and shows their answers; it never touches the opening store, the profile store or the engine
// itself. GUI thread only. It writes the settings file itself (settings.hpp); the engine worker
// writes the engine cache, profiles.db.
#pragma once

#include "engine_worker.hpp"
#include "explore_data.hpp"
#include "settings.hpp"

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <QtGlobal>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QActionGroup;
class QComboBox;
class QLabel;
class QListWidget;

namespace chesscoach::app {

class BoardView;
class DbWorker;
class EvalBar;
class ExploreScreen;

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    // The tier the selector shows when the settings name none.
    static constexpr int default_tier = 2;

    // data_dir holds settings.ini (--data-dir, or the platform's application data location);
    // db is the opening database file (--db), empty when none was given, which the worker opens.
    MainWindow(const QString& data_dir, QString db, QWidget* parent = nullptr);
    ~MainWindow() override;
    Q_DISABLE_COPY_MOVE(MainWindow)

    // The tier selected, 0 to 6.
    [[nodiscard]] int tier() const;
    // The moves played from the start position to the board's position, in UCI.
    [[nodiscard]] const std::vector<std::string>& line() const { return line_uci_; }

  signals:
    void open_requested(const QString& path);
    void query_requested(const QVector<quint64>& line_keys, int tier);
    void configure_requested(const chesscoach::app::EngineSettings& settings);
    void evaluate_requested(quint64 key, const QString& fen, int request_id);

  private:
    void build_toolbar();
    void build_menus();
    void build_status_bar();
    void move_played(const QString& uci, const QString& san);
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

    Settings settings_;
    QString db_path_;
    QString data_version_;
    bool database_open_ = false;
    QThread db_thread_;
    DbWorker* db_worker_; // lives on db_thread_, deleted when it finishes
    QThread engine_thread_;
    EngineWorker* engine_worker_; // lives on engine_thread_, deleted when it finishes
    int request_id_ = 0;          // the evaluation the window waits for
    BoardView* board_;
    EvalBar* eval_bar_;
    ExploreScreen* explore_;
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
    };
    std::optional<Score> stored_score_;
    std::optional<Score> engine_score_;
};

} // namespace chesscoach::app

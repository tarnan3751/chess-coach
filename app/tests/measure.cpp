// app_measure: the app's measurements (docs/reference/testing-tools.md, **Measuring**). A Qt
// program of its own that opens the real main window on the platform QT_QPA_PLATFORM names
// (offscreen under ctest and tools/measure.sh), on a scratch data directory of its own (a
// QTemporaryDir, never the owner's) and the opening database --db names or the app tests' fixture
// (--fixture), drives it as a user would (clicks on the board through QTest, the screens' buttons)
// and times with QElapsedTimer what the user waits for: the cold start to the first paint and to
// the Explore table's first rows, an Explore step to the new rows painted (and the engine's lines),
// a Practice reply to the coach's move painted, the board's repaints and its animation's frames,
// the Repertoire screen's tree and coverage on entering, on changing colour and after saving a
// move, Drill's cards synced, shown, answered and graded, and the resident memory along a session.
// "Painted" is the end of the paint pass in which a widget showing the answer painted: the
// application's notify sees every event of the GUI thread, so a pass ends when the outermost event
// that held the paint (an update request, or an expose) has returned, flush included. Every wait is
// driven by those events, never by sleeping between polls as QTest::qWait does, which would delay
// what is measured.
//
//   app_measure <startup|explore|practice|board|repertoire|drill|memory|all>
//               (--db <file> | --fixture) [--engine <binary>] [--runs <n>] [--tier <0-6>]
//               [--sounds]
//
// The windows are silent (CHESS_COACH_SILENT, set before the first one), so every move's time
// leaves out the sound the app plays as the board shows it (on macOS, the GUI thread's post of the
// play to the backend's queue, and the play there, on a thread of its own); --sounds leaves the
// variable unset, so the app's own backend plays them (NSSound on macOS, audibly: the measurement
// does not reach the app's volume), and every metric's name then ends in ".sounds"; it is refused
// elsewhere, where the app has no backend and would play nothing.
// --engine names the Stockfish both engines use, at one thread and 16 MB of hash; without it the
// engine is off (a binary that is not there), Explore shows no lines and Practice ends a game at
// the first position where it cannot stay in book. --runs is the repetitions inside one process
// (5): of each Explore step, of the board's measures, of the practice games, of the Repertoire
// screen's steps at each coverage depth and of the drills, each of those two on a window of its
// own; startup measures once per process, and memory follows one fixed session. It prints
// tab-separated lines that tools/measure.sh gathers over runs into medians and spreads: "info <key>
// <value>", "method <metric> <unit> <how>" once per metric before its first sample, and "sample
// <metric> <value>". Nothing of it is in the app's own code paths: it finds the window's widgets by
// their object names, as the UI tests do (ui_harness.hpp, built here to leave the silence to main),
// and chooses its moves and computes what the window should show from its own read-only connection
// to the database. ctest runs every scenario once on the fixture with the fake engine (app; bench,
// release preset): a smoke run, which fails only when a step never completes, never on a time.
#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/profile_store.hpp>
#include <chesscoach/repertoire.hpp>
#include <chesscoach/sampler.hpp>
#include <chesscoach/tier.hpp>

#include "application.hpp"
#include "board_scene.hpp"
#include "board_view.hpp"
#include "engine_worker.hpp"
#include "explore_data.hpp"
#include "explore_screen.hpp"
#include "fixture_db.hpp"
#include "main_window.hpp"
#include "settings.hpp"
#include "ui_harness.hpp"
#include "wording.hpp"

#include <fmt/format.h>

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QGraphicsItem>
#include <QGraphicsSvgItem>
#include <QGuiApplication>
#include <QLabel>
#include <QListWidget>
#include <QSettings>
#include <QStringList>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <QWidget>
#include <QtGlobal>

#ifdef __APPLE__
#include <libproc.h>
#include <mach/mach.h>
#include <sys/time.h>
#include <unistd.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using chesscoach::Color;
using chesscoach::Key;
using chesscoach::MoveRow;
using chesscoach::OpeningStore;
using chesscoach::Position;
using chesscoach::ProfileStore;
using chesscoach::Tier;
using chesscoach::app::BoardView;
using chesscoach::app::EngineSettings;
using chesscoach::app::ExploreData;
using chesscoach::app::MainWindow;
using chesscoach::app::Settings;
using chesscoach::app::SparSettings;
using chesscoach::app::test::child;
using chesscoach::app::test::click;
using chesscoach::app::test::square_point;

// How long any step may take before the run fails: far above every measured time, so that only a
// step that never completes fails.
constexpr int timeout_ms = 60000;
// The engine of a run without one: a path that names nothing, so the window finds none.
constexpr const char* missing_engine = "/nonexistent/chess-coach-measure-engine";
// Practice: a fixed seed, so the coach's book replies repeat; games end at this move; the user
// stays in book this many moves before leaving it.
constexpr int practice_seed = 7;
constexpr int practice_end_move = 10;
constexpr int practice_book_moves = 4;
// How long the user looks at the board after Start a practice game before the first move, as a
// user does: what the spar worker does as a session starts is not in the first reply's time.
constexpr int practice_look_ms = 500;
// The coverage depths, in plies, that the Repertoire scenario measures (the setting's default is
// 20).
constexpr std::array<int, 2> repertoire_depths{10, 20};
// Memory: the Explore steps between two readings, the readings, a line's longest stretch before
// New game, and the practice games.
constexpr int memory_steps = 50;
constexpr int memory_rounds = 4;
constexpr std::size_t memory_line_plies = 12;
constexpr int memory_games = 3;

// A saved move: the colour's choice in the position after the line.
struct Choice {
    Color colour;
    std::vector<std::string> line;
    std::string move;
};

// White's moves along the Ruy Lopez, one card each: the memory session's drill.
std::vector<Choice> white_repertoire() {
    const std::vector<std::string> ruy{
        "e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4", "g8f6", "e1g1",
    };
    std::vector<Choice> choices;
    for (std::size_t ply = 0; ply < ruy.size(); ply += 2) {
        choices.push_back(Choice{
            .colour = Color::white,
            .line = std::vector<std::string>(ruy.begin(),
                                             ruy.begin() + static_cast<std::ptrdiff_t>(ply)),
            .move = ruy.at(ply),
        });
    }
    return choices;
}

// Those and five of black's, ten saved moves across both colours: the Repertoire and Drill
// scenarios' repertoire.
std::vector<Choice> both_repertoires() {
    std::vector<Choice> choices = white_repertoire();
    const auto black = [&choices](std::vector<std::string> line, std::string move) {
        choices.push_back(
            Choice{.colour = Color::black, .line = std::move(line), .move = std::move(move)});
    };
    black({"e2e4"}, "c7c5");
    black({"e2e4", "c7c5", "g1f3"}, "d7d6");
    black({"d2d4"}, "g8f6");
    black({"d2d4", "g8f6", "c2c4"}, "e7e6");
    black({"c2c4"}, "e7e5");
    return choices;
}

// A step that never completed, which ends the run with status 1.
class Incomplete : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

double elapsed_ms(const QElapsedTimer& clock) {
    return static_cast<double>(clock.nsecsElapsed()) / 1.0e6;
}

// The lines tools/measure.sh reads; with the sounds on, every metric's name ends in ".sounds".
class Report {
  public:
    explicit Report(bool sounds) : sounds_(sounds) {}

    static void info(std::string_view key, std::string_view value) {
        fmt::print("info\t{}\t{}\n", key, value);
        static_cast<void>(std::fflush(stdout));
    }

    void sample(const std::string& metric, double value, std::string_view unit,
                std::string_view method) {
        const std::string name = sounds_ ? metric + ".sounds" : metric;
        if (described_.insert(name).second) {
            fmt::print("method\t{}\t{}\t{}{}\n", name, unit, method,
                       sounds_ ? " (the app's sounds playing)" : "");
        }
        fmt::print("sample\t{}\t{:.2f}\n", name, value);
        static_cast<void>(std::fflush(stdout));
    }

  private:
    bool sounds_;
    std::set<std::string, std::less<>> described_;
};

// The process's age at the call, in milliseconds, from the kernel's record of its start; nullopt
// where the platform does not say (only macOS is read).
std::optional<double> process_age_ms() {
#ifdef __APPLE__
    proc_bsdinfo info{};
    if (proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) !=
        static_cast<int>(sizeof(info))) {
        return std::nullopt;
    }
    timeval now{};
    gettimeofday(&now, nullptr);
    const double start = (static_cast<double>(info.pbi_start_tvsec) * 1.0e3) +
                         (static_cast<double>(info.pbi_start_tvusec) / 1.0e3);
    return (static_cast<double>(now.tv_sec) * 1.0e3) + (static_cast<double>(now.tv_usec) / 1.0e3) -
           start;
#else
    return std::nullopt;
#endif
}

// The resident size and, on macOS, the physical footprint (Activity Monitor's "Memory") of the
// process, in megabytes; nullopt for what the platform does not say.
struct Memory {
    std::optional<double> resident;
    std::optional<double> footprint;
};

Memory memory_now() {
    constexpr double megabyte = 1024.0 * 1024.0;
    Memory memory;
#ifdef __APPLE__
    mach_task_basic_info_data_t basic{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&basic),
                  &count) == KERN_SUCCESS) {
        memory.resident = static_cast<double>(basic.resident_size) / megabyte;
    }
    task_vm_info_data_t vm{};
    count = TASK_VM_INFO_COUNT;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) ==
        KERN_SUCCESS) {
        memory.footprint = static_cast<double>(vm.phys_footprint) / megabyte;
    }
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    long pages = 0;
    long resident = 0;
    if (statm >> pages >> resident) {
        memory.resident =
            static_cast<double>(resident) * static_cast<double>(sysconf(_SC_PAGESIZE)) / megabyte;
    }
#endif
    return memory;
}

// Told of what the GUI thread delivers: each paint, the end of a paint pass (the outermost event
// during which a widget painted), and the end of every outermost event.
class EventObserver {
  public:
    EventObserver() = default;
    EventObserver(const EventObserver&) = delete;
    EventObserver& operator=(const EventObserver&) = delete;
    EventObserver(EventObserver&&) = delete;
    EventObserver& operator=(EventObserver&&) = delete;
    virtual ~EventObserver() = default;

    virtual void painted(const QWidget& /*widget*/) {}
    virtual void pass_ended() {}
    virtual void event_ended() {}
};

// The application, which sees every event delivered on the GUI thread and tells the observers.
class MeasureApplication final : public QApplication {
  public:
    MeasureApplication(int& argc, char** argv) : QApplication(argc, argv) {}

    void observe(EventObserver* observer) { observers_.push_back(observer); }
    void forget(EventObserver* observer) { std::erase(observers_, observer); }

    bool notify(QObject* receiver, QEvent* event) override {
        if (QThread::currentThread() != thread()) {
            return QApplication::notify(receiver, event); // a worker's queued call
        }
        if (event->type() == QEvent::Paint) {
            if (const auto* widget = qobject_cast<const QWidget*>(receiver); widget != nullptr) {
                painted_ = true;
                for (EventObserver* observer : std::vector(observers_)) {
                    observer->painted(*widget);
                }
            }
        }
        ++depth_;
        const bool result = QApplication::notify(receiver, event);
        --depth_;
        if (depth_ == 0) {
            const bool pass = std::exchange(painted_, false);
            for (EventObserver* observer : std::vector(observers_)) {
                if (pass) {
                    observer->pass_ended();
                }
                observer->event_ended();
            }
        }
        return result;
    }

  private:
    std::vector<EventObserver*> observers_;
    int depth_ = 0;
    bool painted_ = false;
};

// Whether the widget is root or inside it.
bool within(const QWidget& root, const QWidget& widget) {
    return &widget == &root || root.isAncestorOf(&widget);
}

// An observer with an event loop to wait in until it is met; wait() returns at once when it was
// met before, in an earlier event loop.
class Waiter : public EventObserver {
  public:
    explicit Waiter(MeasureApplication& application) : application_(&application) {
        application_->observe(this);
    }
    ~Waiter() override { application_->forget(this); }
    Waiter(const Waiter&) = delete;
    Waiter& operator=(const Waiter&) = delete;
    Waiter(Waiter&&) = delete;
    Waiter& operator=(Waiter&&) = delete;

    void run(const std::string& what) {
        if (!met_) {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(timeout_ms);
            loop_ = &loop;
            loop.exec();
            loop_ = nullptr;
        }
        if (!met_) {
            throw Incomplete(what + " did not happen");
        }
    }

  protected:
    [[nodiscard]] bool met() const { return met_; }
    void meet() {
        met_ = true;
        if (loop_ != nullptr) {
            loop_->quit();
        }
    }

  private:
    MeasureApplication* application_;
    QEventLoop* loop_ = nullptr;
    bool met_ = false;
};

// A condition the window shows: met at the end of the first paint pass in which one of root's
// widgets painted while the condition held. Made before acting, since the pass may run in any
// later event loop, the wait's or another's.
class Watch final : public Waiter {
  public:
    Watch(MeasureApplication& application, const QWidget& root, std::function<bool()> ready,
          const QElapsedTimer& clock)
        : Waiter(application), root_(&root), ready_(std::move(ready)), clock_(&clock) {}

    void painted(const QWidget& widget) override {
        pending_ = pending_ || (!met() && within(*root_, widget) && ready_());
    }
    void pass_ended() override {
        if (pending_ && !met()) {
            at_ = elapsed_ms(*clock_);
            meet();
        }
        pending_ = false;
    }

    // The clock's milliseconds at the end of that pass.
    double wait(const std::string& what) {
        run(what);
        return at_;
    }

  private:
    const QWidget* root_;
    std::function<bool()> ready_;
    const QElapsedTimer* clock_;
    bool pending_ = false;
    double at_ = 0;
};

// A condition of the window's state, checked after every event.
class Until final : public Waiter {
  public:
    Until(MeasureApplication& application, std::function<bool()> done)
        : Waiter(application), done_(std::move(done)) {}

    void event_ended() override {
        if (!met() && done_()) {
            meet();
        }
    }

  private:
    std::function<bool()> done_;
};

// Runs the event loop until done holds.
void wait_for(MeasureApplication& application, const std::function<bool()>& done,
              const std::string& what) {
    if (done()) {
        return;
    }
    Until until(application, done);
    until.run(what);
}

// Runs the event loop for the milliseconds.
void pause(int milliseconds) {
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(milliseconds);
    loop.exec();
}

// The ends of the paint passes in which one of root's widgets painted, between start and stop.
class Frames final : public EventObserver {
  public:
    Frames(MeasureApplication& application, const QWidget& root)
        : application_(&application), root_(&root) {
        application_->observe(this);
    }
    ~Frames() override { application_->forget(this); }
    Frames(const Frames&) = delete;
    Frames& operator=(const Frames&) = delete;
    Frames(Frames&&) = delete;
    Frames& operator=(Frames&&) = delete;

    void start(const QElapsedTimer& clock) {
        clock_ = &clock;
        ends_.clear();
    }
    void stop() { clock_ = nullptr; }
    [[nodiscard]] const std::vector<double>& ends() const { return ends_; }

    void painted(const QWidget& widget) override {
        pending_ = pending_ || (clock_ != nullptr && within(*root_, widget));
    }
    void pass_ended() override {
        if (pending_ && clock_ != nullptr) {
            ends_.push_back(elapsed_ms(*clock_));
        }
        pending_ = false;
    }

  private:
    MeasureApplication* application_;
    const QWidget* root_;
    const QElapsedTimer* clock_ = nullptr;
    std::vector<double> ends_;
    bool pending_ = false;
};

struct Options {
    QString db;
    QString engine; // empty: the engine is off
    int runs = 5;
    int tier = MainWindow::default_tier;
    bool sounds = false; // the app's sounds play (--sounds)
};

// With the sounds on, lets the last one end before the next timed move, as a user's thinking
// does: longer than the longest of the app's sounds (wrong.wav, 280 ms), so a move's sound starts
// from silence rather than stopping one still playing.
void let_sounds_end(const Options& options) {
    constexpr int longest_sound_ms = 300;
    if (options.sounds) {
        pause(longest_sound_ms);
    }
}

// How a metric names the engine's state: engine_on or engine_off.
std::string_view engine_label(const Options& options) {
    return options.engine.isEmpty() ? "engine_off" : "engine_on";
}

Position after(const std::vector<std::string>& line) {
    Position position = Position::start();
    for (const std::string& move : line) {
        if (!position.play_uci(move)) {
            throw std::logic_error("an illegal move in a measured line: " + move);
        }
    }
    return position;
}

std::vector<std::string> sorted_legal(const Position& position) {
    std::vector<std::string> moves = position.legal_moves_uci();
    std::ranges::sort(moves);
    return moves;
}

// The opening database as the measurement reads it, on its own read-only connection: the moves
// to play, and what the Explore screen should say once it shows a position.
class Book {
  public:
    Book(const QString& path, int tier) : store_(open(path)), tier_(tier_of(tier)), index_(tier) {}

    [[nodiscard]] bool in_book(const Position& position) const {
        return store_.node(position.key(), tier_).has_value();
    }

    // The position's moves at the tier, most played first, then by UCI.
    [[nodiscard]] std::vector<std::string> popular(const Position& position) const {
        std::vector<MoveRow> rows = store_.moves(position.key(), tier_);
        std::ranges::sort(rows, [](const MoveRow& a, const MoveRow& b) {
            return a.total() != b.total() ? a.total() > b.total() : a.move < b.move;
        });
        std::vector<std::string> moves;
        moves.reserve(rows.size());
        for (const MoveRow& row : rows) {
            moves.push_back(row.move);
        }
        return moves;
    }

    // Whether the coach answers the position from the book while the game is in book: it has
    // a candidate for the sampler, at the default configuration the spar worker gives it
    // (SparSession::coach_reply); else the engine replies.
    [[nodiscard]] bool samples(const Position& position) const {
        return !chesscoach::sampler_candidates(store_.moves(position.key(), tier_),
                                               chesscoach::SamplerConfig{})
                    .empty();
    }

    // The most played move whose child is in book at the tier with a reply for the coach to
    // sample, if any.
    [[nodiscard]] std::optional<std::string> staying(const Position& position) const {
        for (const std::string& move : popular(position)) {
            Position child = position;
            if (child.play_uci(move) && in_book(child) && samples(child)) {
                return move;
            }
        }
        return std::nullopt;
    }

    // The first legal move, by UCI, whose child is off book at the tier, else the first legal.
    [[nodiscard]] std::string leaving(const Position& position) const {
        const std::vector<std::string> legal = sorted_legal(position);
        for (const std::string& move : legal) {
            Position child = position;
            if (child.play_uci(move) && !in_book(child)) {
                return move;
            }
        }
        return legal.front();
    }

    // The line under the Explore screen's name for the position after ply moves.
    [[nodiscard]] QString summary(const Position& position, std::size_t ply) const {
        ExploreData data;
        data.key = position.key();
        data.tier = index_;
        data.position = store_.position(position.key());
        data.node = store_.node(position.key(), tier_);
        return chesscoach::app::position_summary(data, chesscoach::app::rating_band(index_),
                                                 ply % 2 == 0 ? Color::white : Color::black);
    }

  private:
    static OpeningStore open(const QString& path) {
        auto store = OpeningStore::open(path.toStdString());
        if (!store) {
            throw std::runtime_error("the opening database " + path.toStdString() +
                                     " does not open");
        }
        return std::move(*store);
    }
    static Tier tier_of(int index) {
        const auto tier = chesscoach::tier_from_index(index);
        if (!tier) {
            throw std::runtime_error("no tier " + std::to_string(index));
        }
        return *tier;
    }

    OpeningStore store_;
    Tier tier_;
    int index_;
};

// A scratch data directory set up as a measured run wants it: the engine (or none) at one thread
// and 16 MB of hash, its seconds and lines the app's defaults; the tier; Practice's fixed seed and
// short games; the repertoire's coverage depth when one is given (plies; 0 keeps the default);
// and the choices, saved through the profile store, each a card due now.
class Scratch {
  public:
    explicit Scratch(const Options& options, const std::vector<Choice>& choices = {},
                     int depth = 0) {
        if (!root_.isValid()) {
            throw std::runtime_error("no scratch directory");
        }
        Settings settings(data_dir_);
        EngineSettings engine = settings.engine(); // the defaults: nothing is set yet
        engine.binary =
            options.engine.isEmpty() ? QString::fromLatin1(missing_engine) : options.engine;
        engine.threads = 1;
        engine.hash_mb = 16;
        settings.set_engine(engine);
        settings.set_spar(
            SparSettings{.end_move = practice_end_move, .seconds = SparSettings{}.seconds});
        settings.set_last_tier(options.tier);
        QSettings ini(settings.file(), QSettings::IniFormat);
        ini.setValue(QStringLiteral("spar/seed"), practice_seed);
        ini.sync();
        if (depth > 0) {
            settings.set_repertoire_depth(depth);
        }
        if (!choices.empty()) {
            save(choices);
        }
    }

    [[nodiscard]] const QString& data_dir() const { return data_dir_; }

    // The cards' answers: the repertoire's move at each of its positions, by key.
    [[nodiscard]] const std::map<Key, std::string>& answers() const { return answers_; }

    // Takes the cards away, leaving the choices, as no store call does (the app tests'
    // drop_cards), so that entering Drill syncs them back.
    void drop_cards() {
        chesscoach::app::test::drop_cards(
            QDir(data_dir_).filePath(QStringLiteral("profiles.db")).toStdString());
        dropped_ = true;
    }
    [[nodiscard]] bool dropped() const { return dropped_; }

  private:
    void save(const std::vector<Choice>& choices) {
        auto profiles = ProfileStore::open(data_dir_.toStdString());
        if (!profiles) {
            throw std::runtime_error("profiles.db does not open");
        }
        const std::int64_t profile = profiles->profiles().front().id;
        for (const Choice& choice : choices) {
            const Position position = after(choice.line);
            const auto set = profiles->set_choice(profile, choice.colour,
                                                  chesscoach::RepertoireChoice{
                                                      .key = position.key(),
                                                      .fen = position.fen(),
                                                      .move = choice.move,
                                                  });
            if (!set) {
                throw std::runtime_error("the measured repertoire is not saved");
            }
            answers_.emplace(position.key(), choice.move);
        }
    }

    QTemporaryDir root_;
    QString data_dir_ = root_.filePath(QStringLiteral("data"));
    std::map<Key, std::string> answers_;
    bool dropped_ = false;
};

// The process's start as main measured it: its age at main, and the application's construction.
struct Launch {
    std::optional<double> before_main;
    double application = 0;
};

// What a measured window needs at hand.
struct Session {
    MeasureApplication* application;
    MainWindow* window;
    const Book* book;
    const Options* options;
};

// The first of a move's two clicks, which picks the piece; the event loop runs until it has
// painted, as it would before a user's second click.
void pick(MeasureApplication& application, const BoardView& board, const std::string& uci) {
    QElapsedTimer picked;
    picked.start();
    Watch shown(application, *board.viewport(), [] { return true; }, picked);
    click(board, uci.substr(0, 2));
    static_cast<void>(shown.wait("the pick of " + uci));
}

// The second click, which plays the picked piece's move, clock starting with it; make the watches
// of what follows before. Returns the milliseconds the click took: the GUI thread's own work for
// the move.
double drop(const BoardView& board, const std::string& uci, QElapsedTimer& clock) {
    clock.start();
    QTest::mouseClick(board.viewport(), Qt::LeftButton, Qt::KeyboardModifiers(),
                      square_point(board, uci.substr(2, 2)));
    return elapsed_ms(clock);
}

// The window's Explore line taken to the moves, through New game and the board, with the table
// showing the last position and no piece moving.
void explore_to(const Session& session, const std::vector<std::string>& line) {
    child<QAction>(*session.window, QStringLiteral("new_game_action")).trigger();
    auto& board = child<BoardView>(*session.window, QStringLiteral("board"));
    for (const std::string& move : line) {
        if (!board.play_move(QString::fromStdString(move))) {
            throw std::logic_error("the board refused " + move);
        }
    }
    const QString expected = session.book->summary(after(line), line.size());
    const auto& summary = child<QLabel>(*session.window, QStringLiteral("explore_summary"));
    wait_for(
        *session.application, [&] { return summary.text() == expected && !board.animating(); },
        "the Explore table after " + std::to_string(line.size()) + " moves");
    pause(20); // any answer still in flight for an earlier position lands
    let_sounds_end(*session.options);
}

void startup(MeasureApplication& application, Report& report, const Options& options,
             const Launch& launch) {
    // Untimed: what the real app does not do, the scratch directory and the expected summary.
    const Scratch scratch(options);
    const Book book(options.db, options.tier);
    const QString expected = book.summary(Position::start(), 0);
    const double before = launch.before_main.value_or(0) + launch.application;
    const std::string from = launch.before_main ? "from the process's start" : "from main";

    QElapsedTimer clock;
    clock.start();
    auto window = std::make_unique<MainWindow>(scratch.data_dir(), options.db);
    const double constructed = elapsed_ms(clock);
    const auto& status = child<QLabel>(*window, QStringLiteral("database_status"));
    const auto& summary = child<QLabel>(*window, QStringLiteral("explore_summary"));
    Watch first_paint(application, *window, [] { return true; }, clock);
    Watch database(
        application, *window,
        [&status] { return status.text().startsWith(QStringLiteral("Opening database: ")); },
        clock);
    Watch rows(
        application, *window, [&summary, &expected] { return summary.text() == expected; }, clock);
    window->show();
    const double painted = first_paint.wait("the first paint");
    const double opened = database.wait("the database's name in the status bar");
    const double shown = rows.wait("the start position's rows");

    const std::string name = fmt::format("startup.{}.", engine_label(options));
    if (launch.before_main) {
        report.sample(name + "before_main", *launch.before_main, "ms",
                      "the process's start (the kernel's record) to main: loading and linking "
                      "Qt and the program");
    }
    report.sample(name + "application", launch.application, "ms",
                  "QApplication and configure_application (the style, the bundled fonts)");
    report.sample(name + "window", constructed, "ms",
                  "MainWindow's constructor: the widgets, the theme, the four workers started");
    report.sample(name + "first_paint", before + painted, "ms",
                  from + " to the end of the window's first paint pass");
    report.sample(name + "database_open", before + opened, "ms",
                  from + " to the status bar naming the open database, painted");
    report.sample(name + "first_rows", before + shown, "ms",
                  from + " to the Explore table's rows at the start painted");
}

// The Explore steps; the first one's click is reported apart when it is the process's first move
// (first_in_process).
void explore(MeasureApplication& application, Report& report, const Options& options,
             bool first_in_process) {
    const Scratch scratch(options);
    const Book book(options.db, options.tier);
    MainWindow window(scratch.data_dir(), options.db);
    window.show();
    const Session session{
        .application = &application,
        .window = &window,
        .book = &book,
        .options = &options,
    };
    const auto& board = child<BoardView>(window, QStringLiteral("board"));
    const auto& summary = child<QLabel>(window, QStringLiteral("explore_summary"));
    const auto& detail = child<QLabel>(window, QStringLiteral("engine_detail"));
    const auto& lines = child<QListWidget>(window, QStringLiteral("engine_lines"));
    const bool engine = !options.engine.isEmpty();
    bool lines_arrived = false;
    QObject::connect(lines.model(), &QAbstractItemModel::rowsInserted, &window,
                     [&lines_arrived] { lines_arrived = true; });
    explore_to(session, {});
    if (engine) {
        wait_for(application, [&lines] { return lines.count() > 0; }, "the engine's first lines");
    }

    struct Step {
        std::string name;
        std::vector<std::string> setup;
        std::string move;
    };
    const std::vector<std::string> sicilian{"e2e4", "c7c5", "g1f3", "d7d6"};
    const std::string leave = book.leaving(after(sicilian));
    std::vector<std::string> off = sicilian;
    off.push_back(leave);
    const std::vector<Step> steps{
        {.name = "start", .setup = {}, .move = "e2e4"},
        {.name = "sicilian", .setup = sicilian, .move = "d2d4"},
        {.name = "leave_book", .setup = sicilian, .move = leave},
        {.name = "off_book", .setup = off, .move = sorted_legal(after(off)).front()},
    };
    Report::info("explore_leave_book", leave);
    Report::info("explore_off_book", steps.back().move);
    const std::string label(engine_label(options));
    bool first_move = first_in_process;
    for (int run = 0; run < options.runs; ++run) {
        for (const Step& step : steps) {
            explore_to(session, step.setup);
            std::vector<std::string> line = step.setup;
            line.push_back(step.move);
            const QString expected = book.summary(after(line), line.size());
            pick(application, board, step.move);
            QElapsedTimer clock;
            lines_arrived = false;
            Watch table(
                application, window, [&summary, &expected] { return summary.text() == expected; },
                clock);
            Watch engine_lines(
                application, window, [&lines_arrived] { return lines_arrived; }, clock);
            const double gui = drop(board, step.move, clock);
            const double shown = table.wait("the table after " + step.move);
            if (std::exchange(first_move, false)) {
                report.sample("explore.first_click", gui, "ms",
                              "the process's first move's second click handled, as "
                              "explore.click; its sound, when on, the first a move plays");
            } else {
                report.sample(
                    "explore.click", gui, "ms",
                    "the board's second click handled: the move played and its sound (when on), "
                    "the line and the move list updated, the database and engine asked");
            }
            report.sample(fmt::format("explore.{}.{}", label, step.name), shown, "ms",
                          fmt::format("the board's second click to the Explore table (or its "
                                      "off-book message) for the new position painted, the "
                                      "engine {}",
                                      engine ? "searching" : "off"));
            if (engine) {
                const double searched = engine_lines.wait("the engine's lines after " + step.move);
                const bool cached = detail.text().contains(QStringLiteral("earlier search"));
                report.sample(
                    fmt::format("explore.engine_lines{}.{}", cached ? "_cached" : "", step.name),
                    searched, "ms",
                    cached ? "the second click to the engine's lines from the cache painted"
                           : "the second click to the engine's lines painted (the search is the "
                             "settings' 2 s of it)");
            }
        }
    }
}

// One practice game as white: book moves for practice_book_moves moves, then a move that leaves
// book, then the first legal move, to the game's move limit; each reply timed into report when one
// is given, as a book reply or an engine reply by the session's own rules (SparSession: the game
// is in book until a move reaches a position off book or the engine has replied, and in book the
// coach samples when it has a candidate). first_engine is whether no engine reply has been timed
// yet in this process.
void practice_game(const Session& session, Report* report, bool& first_engine) {
    MainWindow& window = *session.window;
    const Book& book = *session.book;
    const auto& board = child<BoardView>(window, QStringLiteral("board"));
    const auto& end = child<QAbstractButton>(window, QStringLiteral("spar_end"));
    const auto& interruption = child<QWidget>(window, QStringLiteral("spar_interruption"));
    const bool engine = !session.options->engine.isEmpty();
    const auto turn = [&board, &end] { return board.isEnabled() || !end.isEnabled(); };
    child<QAbstractButton>(window, QStringLiteral("spar_white")).click();
    child<QAbstractButton>(window, QStringLiteral("spar_start")).click();
    wait_for(*session.application, turn, "the practice game's start");
    pause(practice_look_ms);
    bool in_book = book.in_book(Position::start());
    int moves = 0;
    while (end.isEnabled() && board.isEnabled()) {
        let_sounds_end(*session.options);
        const Position before = board.position();
        std::string move;
        if (in_book && moves < practice_book_moves) {
            move = book.staying(before).value_or(book.leaving(before));
        } else if (in_book) {
            move = book.leaving(before);
        } else {
            const std::vector<std::string> legal = sorted_legal(before);
            if (legal.empty()) {
                break;
            }
            move = legal.front();
        }
        Position played = before;
        static_cast<void>(played.play_uci(move));
        in_book = in_book && book.in_book(played);
        const bool from_book = in_book && book.samples(played);
        if (!from_book && !engine) {
            break; // no engine to reply
        }
        ++moves;
        const Key user = played.key();
        pick(*session.application, board, move);
        QElapsedTimer clock;
        Watch reply(
            *session.application, window,
            [&board, &end, user] { return board.position().key() != user || !end.isEnabled(); },
            clock);
        static_cast<void>(drop(board, move, clock));
        const double shown = reply.wait("the coach's reply to " + move);
        const bool replied = board.position().key() != user;
        if (replied && report != nullptr) {
            const double seconds = SparSettings{}.seconds * 1000.0;
            if (from_book) {
                report->sample("practice.book_reply", shown, "ms",
                               "the user's second click to the coach's reply from the book "
                               "painted on the board (its animation starting)");
            } else if (first_engine) {
                first_engine = false;
                report->sample("practice.first_engine_reply", shown, "ms",
                               "the same for the process's first reply from the engine, the "
                               "coach's engine started with the session, then a 0.5 s search");
            } else {
                report->sample("practice.engine_reply", shown, "ms",
                               "the same for a later reply from the engine, which searches "
                               "Practice's 0.5 s");
                report->sample("practice.engine_reply_overhead", shown - seconds, "ms",
                               "practice.engine_reply less the 0.5 s search the setting asks");
            }
        }
        in_book = in_book && from_book && replied && book.in_book(board.position());
        if (interruption.isVisible()) {
            child<QAbstractButton>(window, QStringLiteral("spar_continue")).click();
        }
        wait_for(*session.application, turn, "the user's turn after " + move);
    }
    if (end.isEnabled()) {
        child<QAbstractButton>(window, QStringLiteral("spar_end")).click();
    }
    wait_for(*session.application, [&end] { return !end.isEnabled(); }, "the practice game's end");
}

void practice(MeasureApplication& application, Report& report, const Options& options) {
    const Scratch scratch(options);
    const Book book(options.db, options.tier);
    MainWindow window(scratch.data_dir(), options.db);
    window.show();
    const Session session{
        .application = &application,
        .window = &window,
        .book = &book,
        .options = &options,
    };
    explore_to(session, {});
    child<QTabBar>(window, QStringLiteral("screen_tabs")).setCurrentIndex(1);
    bool first_engine = true;
    for (int run = 0; run < options.runs; ++run) {
        practice_game(session, &report, first_engine);
    }
}

void board(MeasureApplication& application, Report& report, const Options& options) {
    const Scratch scratch(options);
    const Book book(options.db, options.tier);
    MainWindow window(scratch.data_dir(), options.db);
    window.show();
    const Session session{
        .application = &application,
        .window = &window,
        .book = &book,
        .options = &options,
    };
    auto& board = child<BoardView>(window, QStringLiteral("board"));
    QWidget& viewport = *board.viewport();
    auto& flip = child<QAction>(window, QStringLiteral("flip_action"));
    explore_to(session, {});
    // The metrics name the device pixel ratio: 1 offscreen, 2 under QT_SCALE_FACTOR=2 as on a
    // Retina display.
    const std::string name = fmt::format("board.dpr{}.", viewport.devicePixelRatioF());
    Report::info("board_pixels", fmt::format("{}x{}", viewport.width(), viewport.height()));
    const QGraphicsItem* piece =
        board.board_scene().piece_item(chesscoach::app::test::square("e2"));
    Report::info("piece_cache_mode",
                 piece != nullptr && piece->cacheMode() == QGraphicsItem::DeviceCoordinateCache
                     ? "DeviceCoordinateCache"
                     : "other");
    Frames frames(application, viewport);
    for (int run = 0; run < options.runs; ++run) {
        explore_to(session, {});
        pick(application, board, "e2e4");
        QElapsedTimer clock;
        Watch painted(application, viewport, [] { return true; }, clock);
        frames.start(clock);
        static_cast<void>(drop(board, "e2e4", clock));
        report.sample(name + "move_painted", painted.wait("the board after e2e4"), "ms",
                      "the second click to the end of the board's first paint after it: 32 "
                      "piece items made again, the moved piece at its start square");
        wait_for(application, [&board] { return !board.animating(); }, "the animation's end");
        pause(50); // the last frame, at the target square
        frames.stop();
        const std::vector<double>& ends = frames.ends();
        if (ends.size() >= 2) {
            double longest = 0;
            for (std::size_t i = 1; i < ends.size(); ++i) {
                longest = std::max(longest, ends.at(i) - ends.at(i - 1));
            }
            const double span = ends.back() - ends.front();
            report.sample(name + "animation_frames", static_cast<double>(ends.size()), "frames",
                          "the board's paint passes from the click to the end of the 150 ms "
                          "slide");
            report.sample(name + "animation_fps",
                          span > 0 ? (static_cast<double>(ends.size()) - 1) * 1000.0 / span : 0,
                          "fps", "those passes per second, first to last");
            report.sample(name + "frame_gap_max", longest, "ms",
                          "the longest gap between two of those passes");
        }

        clock.start();
        viewport.repaint();
        report.sample(name + "repaint", elapsed_ms(clock), "ms",
                      "a full synchronous repaint of the board (QWidget::repaint), its pieces "
                      "drawn before");

        Watch flipped(application, viewport, [] { return true; }, clock);
        clock.start();
        flip.trigger();
        report.sample(name + "flip_painted", flipped.wait("the flipped board"), "ms",
                      "Flip board triggered to the end of the board's next paint");
        Watch back(application, viewport, [] { return true; }, clock);
        flip.trigger();
        static_cast<void>(back.wait("the board flipped back"));

        clock.start();
        board.set_position(after({"e2e4"}));
        viewport.repaint();
        report.sample(name + "set_position_repaint", elapsed_ms(clock), "ms",
                      "set_position (New game, a jump, a screen's position) and a full "
                      "synchronous repaint: the piece items made again and drawn the first time");
    }
}

void report_memory(Report& report, const std::string& checkpoint, const std::string& what) {
    const Memory memory = memory_now();
    if (memory.resident) {
        report.sample("memory.resident." + checkpoint, *memory.resident, "MB",
                      "resident size " + what);
    }
    if (memory.footprint) {
        report.sample("memory.footprint." + checkpoint, *memory.footprint, "MB",
                      "physical footprint (Activity Monitor's Memory) " + what);
    }
}

// A drill of the white cards over the repertoire Scratch saved: Drill's tab (its cards synced back
// when Scratch dropped them), Start the drill, and every card answered right and graded About
// right; each step timed into report when one is given.
void drill_session(const Session& session, const Scratch& scratch, Report* report) {
    MeasureApplication& application = *session.application;
    MainWindow& window = *session.window;
    const auto& board = child<BoardView>(window, QStringLiteral("board"));
    const auto& due = child<QLabel>(window, QStringLiteral("drill_due"));
    const auto& status = child<QLabel>(window, QStringLiteral("drill_status"));
    const auto& verdict = child<QLabel>(window, QStringLiteral("drill_verdict"));
    const auto& message = child<QLabel>(window, QStringLiteral("message_status"));
    const auto& summary = child<QWidget>(window, QStringLiteral("drill_summary"));
    auto& good = child<QAbstractButton>(window, QStringLiteral("drill_good"));
    auto& next = child<QAbstractButton>(window, QStringLiteral("drill_next"));
    const auto sample = [report](const std::string& metric, double value, std::string_view method) {
        if (report != nullptr) {
            report->sample(metric, value, "ms", method);
        }
    };
    QElapsedTimer clock;
    {
        Watch entered(
            application, window,
            [&] {
                return due.text().contains(QStringLiteral("due now,")) &&
                       (!scratch.dropped() || status.text().startsWith(QStringLiteral(
                                                  "Your cards now match your repertoire.")));
            },
            clock);
        clock.start();
        child<QTabBar>(window, QStringLiteral("screen_tabs")).setCurrentIndex(3);
        sample("drill.enter", entered.wait("the drill's cards"),
               "the Drill tab to its cards synced back from the repertoire (five made) and the "
               "count due painted");
    }
    {
        Watch shown(
            application, window,
            [&] {
                return board.isEnabled() && status.text().startsWith(QStringLiteral("Your move"));
            },
            clock);
        clock.start();
        child<QAbstractButton>(window, QStringLiteral("drill_start")).click();
        sample("drill.first_card", shown.wait("the first card"),
               "Start the drill to the first card's position and question painted");
    }
    for (std::size_t card = 0; !summary.isVisible(); ++card) {
        if (card > scratch.answers().size()) {
            throw std::logic_error("more cards than the repertoire holds");
        }
        const Key key = board.position().key();
        const auto answer = scratch.answers().find(key);
        if (answer == scratch.answers().end()) {
            throw std::logic_error("a card the repertoire does not hold");
        }
        let_sounds_end(*session.options);
        pick(application, board, answer->second);
        Watch right(
            application, window,
            [&] { return verdict.text().startsWith(QStringLiteral("Right.")) && good.isVisible(); },
            clock);
        static_cast<void>(drop(board, answer->second, clock));
        sample("drill.answer", right.wait("the verdict"),
               "the answer's second click to the verdict and the grades painted");
        if (card == 0) {
            // The status bar's day is the same text for every card graded today, so only the
            // first card's shows a change to time.
            Watch graded(
                application, window,
                [&message] {
                    return message.text().startsWith(
                        QStringLiteral("You will see this card again on "));
                },
                clock);
            clock.start();
            good.click();
            sample("drill.grade", graded.wait("the review's day"),
                   "About right to the status bar's day the card comes back painted, the "
                   "review recorded by the profile worker (the first card's)");
        } else {
            good.click();
        }
        wait_for(application, [&next] { return next.isVisible(); }, "Next card");
        Watch following(
            application, window,
            [&] {
                return (board.isEnabled() && board.position().key() != key) || summary.isVisible();
            },
            clock);
        clock.start();
        next.click();
        const double shown = following.wait("the next card");
        sample(summary.isVisible() ? "drill.summary" : "drill.next_card", shown,
               summary.isVisible() ? "Next card, with none due, to the drill's summary painted"
                                   : "Next card to the next card's position and question painted");
    }
}

void drill(MeasureApplication& application, Report& report, const Options& options) {
    for (int run = 0; run < options.runs; ++run) {
        Scratch scratch(options, both_repertoires());
        scratch.drop_cards();
        const Book book(options.db, options.tier);
        MainWindow window(scratch.data_dir(), options.db);
        window.show();
        const Session session{
            .application = &application,
            .window = &window,
            .book = &book,
            .options = &options,
        };
        explore_to(session, {});
        drill_session(session, scratch, &report);
    }
}

// One window's Repertoire screen over the ten saved moves at the coverage depth: entering it (the
// white repertoire), changing to the black one, and saving black's move at its first gap, each
// timed to the tree and to the coverage and its gaps painted.
void repertoire_session(MeasureApplication& application, Report& report, const Options& options,
                        int depth) {
    int tree_resets = 0;
    int gap_changes = 0;
    const Scratch scratch(options, both_repertoires(), depth);
    const Book book(options.db, options.tier);
    MainWindow window(scratch.data_dir(), options.db);
    window.show();
    const Session session{
        .application = &application,
        .window = &window,
        .book = &book,
        .options = &options,
    };
    explore_to(session, {});
    const auto& board = child<BoardView>(window, QStringLiteral("board"));
    const auto& tree = child<QTreeView>(window, QStringLiteral("repertoire_tree"));
    const auto& gaps = child<QTableWidget>(window, QStringLiteral("repertoire_gaps"));
    const auto& coverage = child<QLabel>(window, QStringLiteral("repertoire_coverage"));
    QObject::connect(tree.model(), &QAbstractItemModel::modelReset, &window,
                     [&tree_resets] { ++tree_resets; });
    QObject::connect(gaps.model(), &QAbstractItemModel::rowsInserted, &window,
                     [&gap_changes] { ++gap_changes; });
    QObject::connect(gaps.model(), &QAbstractItemModel::rowsRemoved, &window,
                     [&gap_changes] { ++gap_changes; });
    const std::string name = fmt::format("repertoire.d{}.", depth);
    // A step: the tree shown again, and the coverage answered, its line or its gaps changed.
    const auto step = [&](const std::string& what, std::string_view how,
                          const std::function<void(QElapsedTimer&)>& act) {
        QElapsedTimer clock;
        const int resets = tree_resets;
        const int changes = gap_changes;
        const QString before = coverage.text();
        Watch tree_shown(
            application, window, [&tree_resets, resets] { return tree_resets > resets; }, clock);
        Watch covered(
            application, window,
            [&] {
                return coverage.text().startsWith(QStringLiteral("Coverage is")) &&
                       (gap_changes > changes || coverage.text() != before);
            },
            clock);
        act(clock);
        report.sample(name + what + "_tree", tree_shown.wait("the tree " + what), "ms",
                      fmt::format("{} to the repertoire's tree painted", how));
        report.sample(
            name + what + "_coverage", covered.wait("the coverage " + what), "ms",
            fmt::format("{} to the coverage and its gaps painted, {} plies deep", how, depth));
    };
    step("enter", "the Repertoire tab, the white repertoire's five moves,",
         [&](QElapsedTimer& clock) {
             clock.start();
             child<QTabBar>(window, QStringLiteral("screen_tabs")).setCurrentIndex(2);
         });
    step("colour", "Black repertoire, its five moves,", [&](QElapsedTimer& clock) {
        clock.start();
        child<QAbstractButton>(window, QStringLiteral("repertoire_black")).click();
    });
    auto& go = child<QAbstractButton>(window, QStringLiteral("repertoire_goto"));
    if (!go.isEnabled()) {
        Report::info("repertoire_save", "skipped: the black repertoire has no gap");
        return;
    }
    go.click(); // the board at the first gap, black to move
    pause(50);  // the gap's scores land
    const Position gap = board.position();
    std::vector<std::string> moves = book.popular(gap);
    if (moves.empty()) {
        moves = sorted_legal(gap);
    }
    const std::string move = moves.front();
    step("save", "a board move saving black's answer at the first gap", [&](QElapsedTimer& clock) {
        let_sounds_end(options);
        pick(application, board, move);
        static_cast<void>(drop(board, move, clock));
    });
}

void repertoire(MeasureApplication& application, Report& report, const Options& options) {
    for (const int depth : repertoire_depths) {
        for (int run = 0; run < options.runs; ++run) {
            repertoire_session(application, report, options, depth);
        }
    }
}

void memory(MeasureApplication& application, Report& report, const Options& options) {
    const Scratch scratch(options, white_repertoire());
    const Book book(options.db, options.tier);
    MainWindow window(scratch.data_dir(), options.db);
    window.show();
    const Session session{
        .application = &application,
        .window = &window,
        .book = &book,
        .options = &options,
    };
    explore_to(session, {});
    report_memory(report, "start", "once the Explore table shows the start");
    auto& board = child<BoardView>(window, QStringLiteral("board"));
    const auto& summary = child<QLabel>(window, QStringLiteral("explore_summary"));
    std::vector<std::string> line;
    std::size_t step = 0;
    for (int round = 1; round <= memory_rounds; ++round) {
        for (int i = 0; i < memory_steps; ++i, ++step) {
            const Position position = after(line);
            std::vector<std::string> moves = book.popular(position);
            if (moves.empty()) {
                moves = sorted_legal(position);
            }
            if (moves.empty() || line.size() >= memory_line_plies) {
                line.clear();
                explore_to(session, line);
                continue;
            }
            // The most played move, the second or the third in turn, so the lines vary.
            const std::string& move = moves.at(step % std::min<std::size_t>(moves.size(), 3));
            line.push_back(move);
            static_cast<void>(board.play_move(QString::fromStdString(move)));
            const QString expected = book.summary(after(line), line.size());
            wait_for(
                application, [&summary, &expected] { return summary.text() == expected; },
                "an Explore step");
        }
        report_memory(report, fmt::format("explore_{}", round * memory_steps),
                      fmt::format("after {} Explore steps", round * memory_steps));
    }
    if (!options.engine.isEmpty()) {
        child<QTabBar>(window, QStringLiteral("screen_tabs")).setCurrentIndex(1);
        bool first_engine = true;
        for (int game = 1; game <= memory_games; ++game) {
            practice_game(session, nullptr, first_engine);
            report_memory(report, fmt::format("practice_{}", game),
                          fmt::format("after {} practice game{}", game, game > 1 ? "s" : ""));
        }
    }
    drill_session(session, scratch, nullptr);
    report_memory(report, "drill",
                  fmt::format("after a drill of {} cards", scratch.answers().size()));
}

} // namespace

int main(int argc, char* argv[]) {
    QElapsedTimer since_main;
    since_main.start();
    const std::optional<double> before_main = process_age_ms();
    MeasureApplication application(argc, argv);
    chesscoach::app::configure_application();
    const Launch launch{.before_main = before_main, .application = elapsed_ms(since_main)};
    QApplication::setApplicationName(QStringLiteral("chess-coach"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("The app's measurements (tools/measure.sh)."));
    parser.addHelpOption();
    parser.addPositionalArgument(
        QStringLiteral("scenario"),
        QStringLiteral("startup, explore, practice, board, repertoire, drill, memory or all"));
    const QCommandLineOption db_option(QStringList{QStringLiteral("db")},
                                       QStringLiteral("The opening database."),
                                       QStringLiteral("file"));
    const QCommandLineOption fixture_option(QStringList{QStringLiteral("fixture")},
                                            QStringLiteral("The app tests' fixture database."));
    const QCommandLineOption engine_option(QStringList{QStringLiteral("engine")},
                                           QStringLiteral("The engine; none without it."),
                                           QStringLiteral("binary"));
    const QCommandLineOption runs_option(QStringList{QStringLiteral("runs")},
                                         QStringLiteral("Repetitions inside the process (5)."),
                                         QStringLiteral("n"), QStringLiteral("5"));
    const QCommandLineOption tier_option(QStringList{QStringLiteral("tier")},
                                         QStringLiteral("The tier, 0 to 6 (the app's default)."),
                                         QStringLiteral("tier"),
                                         QString::number(MainWindow::default_tier));
    const QCommandLineOption sounds_option(QStringList{QStringLiteral("sounds")},
                                           QStringLiteral("The app's sounds play."));
    parser.addOptions(
        {db_option, fixture_option, engine_option, runs_option, tier_option, sounds_option});
    parser.process(application);
    const QStringList positional = parser.positionalArguments();
    const QString scenario = positional.isEmpty() ? QString() : positional.front();
    const QStringList scenarios{
        QStringLiteral("startup"), QStringLiteral("explore"),    QStringLiteral("practice"),
        QStringLiteral("board"),   QStringLiteral("repertoire"), QStringLiteral("drill"),
        QStringLiteral("memory"),  QStringLiteral("all"),
    };
    bool runs_ok = false;
    bool tier_ok = false;
    Options options;
    options.engine = parser.value(engine_option);
    options.runs = parser.value(runs_option).toInt(&runs_ok);
    options.tier = parser.value(tier_option).toInt(&tier_ok);
    if (positional.size() != 1 || !scenarios.contains(scenario) || !runs_ok || options.runs < 1 ||
        !tier_ok || parser.isSet(db_option) == parser.isSet(fixture_option)) {
        static_cast<void>(
            std::fputs("usage: app_measure <startup|explore|practice|board|repertoire|drill|"
                       "memory|all> "
                       "(--db <file> | --fixture) [--engine <binary>] [--runs <n>] "
                       "[--tier <0-6>] [--sounds]\n",
                       stderr));
        return 2;
    }
    try {
        std::unique_ptr<chesscoach::app::test::FixtureDb> fixture;
        if (parser.isSet(fixture_option)) {
            fixture = std::make_unique<chesscoach::app::test::FixtureDb>();
            options.db = QString::fromStdString(fixture->path());
        } else {
            options.db = parser.value(db_option);
        }
        Report::info("qt", qVersion());
        Report::info("platform", QGuiApplication::platformName().toStdString());
        Report::info("db", options.db.toStdString());
        Report::info("tier", std::to_string(options.tier));
        Report::info("engine", options.engine.isEmpty() ? "none" : options.engine.toStdString());
        // The silence, or not, before the first window makes its sounds (sounds.hpp).
        const bool sounds = parser.isSet(sounds_option);
#ifndef __APPLE__
        if (sounds) {
            throw std::runtime_error(
                "--sounds needs the app's sound backend, which only macOS has");
        }
#endif
        options.sounds = sounds;
        if (sounds) {
            qunsetenv("CHESS_COACH_SILENT");
        } else if (!qputenv("CHESS_COACH_SILENT", "1")) {
            throw std::runtime_error("CHESS_COACH_SILENT could not be set");
        }
        Report::info("sounds", sounds ? "on" : "off");
        Report report(sounds);
        const bool all = scenario == QStringLiteral("all");
        if (all || scenario == QStringLiteral("startup")) {
            startup(application, report, options, launch);
        }
        if (all || scenario == QStringLiteral("explore")) {
            explore(application, report, options, true);
        }
        if (all && !options.engine.isEmpty()) {
            Options off = options;
            off.engine.clear();
            explore(application, report, off, false);
        }
        if ((all || scenario == QStringLiteral("practice")) && !options.engine.isEmpty()) {
            practice(application, report, options);
        }
        if (all || scenario == QStringLiteral("board")) {
            board(application, report, options);
        }
        if (all || scenario == QStringLiteral("repertoire")) {
            repertoire(application, report, options);
        }
        if (all || scenario == QStringLiteral("drill")) {
            drill(application, report, options);
        }
        if (all || scenario == QStringLiteral("memory")) {
            memory(application, report, options);
        }
    } catch (const std::exception& error) {
        fmt::print(stderr, "app_measure: {}\n", error.what());
        return 1;
    }
    return 0;
}

// fake-uci: a scripted UCI engine for core_engine_test (docs/reference/core.md, **The fake
// engine**). Not a test itself. Usage: fake-uci <script> [delay_ms] [log_path]. Every line it
// receives is appended to log_path when one is given, so a test can check exactly what the client
// sent and in which order. Scripts:
//   normal            the protocol as Stockfish speaks it: id name, UCI_Elo and MultiPV options,
//                     uciok; readyok; on go, depths 1 to 3 for each multipv, then bestmove after
//                     the movetime or on stop
//   late-readyok      normal, but waits delay_ms before every readyok after the first
//   interleaved-info  on go, junk lines, info string, currmove and nps lines, multipv 2 before 1,
//                     repeated depths with changed scores, a bound score, a multipv above the
//                     request and a mate score, then bestmove with ponder
//   no-bestmove       infos, then never bestmove, even after stop
//   die-mid-search    exits after the first info of a search
//   garbage           non-UCI lines (an early readyok and bestmove among them) before uciok, and
//                     no options advertised; normal afterwards
// The scores and moves are fixed, not chess: the client is under test, not the engine.
#include <array>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace {

// Lines go out whole: the search thread and the main thread both write.
std::mutex& output_mutex() {
    static std::mutex mutex;
    return mutex;
}

void say(std::string_view line) {
    const std::scoped_lock lock(output_mutex());
    std::cout << line << '\n' << std::flush;
}

constexpr std::array<std::string_view, 6> moves{"e2e4", "d2d4", "g1f3", "c2c4", "b1c3", "g2g3"};

// A decimal number, or the fallback when the text is not one.
int number(std::string_view text, int fallback) {
    int value = 0;
    const auto* const begin = text.data(); // NOLINT(bugprone-suspicious-stringview-data-usage)
    const auto [end, error] = std::from_chars(begin, begin + text.size(), value);
    return error == std::errc() && end != begin ? value : fallback;
}

// The search a go starts: waits for its movetime or a stop, then answers bestmove unless the
// script never does.
class Search {
  public:
    void start(int movetime_ms, bool answer, std::string bestmove) {
        join();
        {
            const std::scoped_lock lock(mutex_);
            stopped_ = false;
        }
        thread_ = std::thread([this, movetime_ms, answer, bestmove = std::move(bestmove)] {
            std::unique_lock lock(mutex_);
            stop_.wait_for(lock, std::chrono::milliseconds(movetime_ms),
                           [this] { return stopped_; });
            lock.unlock();
            if (answer) {
                say(bestmove);
            }
        });
    }
    void stop() {
        {
            const std::scoped_lock lock(mutex_);
            stopped_ = true;
        }
        stop_.notify_all();
    }
    void join() {
        if (thread_.joinable()) {
            stop();
            thread_.join();
        }
    }
    Search() = default;
    Search(const Search&) = delete;
    Search& operator=(const Search&) = delete;
    Search(Search&&) = delete;
    Search& operator=(Search&&) = delete;
    ~Search() { join(); }

  private:
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable stop_;
    bool stopped_ = false;
};

int value_after(std::string_view line, std::string_view word, int fallback) {
    const auto at = line.find(word);
    if (at == std::string_view::npos) {
        return fallback;
    }
    return number(line.substr(at + word.size()), fallback);
}

void normal_infos(int multipv) {
    for (int depth = 1; depth <= 3; ++depth) {
        for (int k = 1; k <= multipv; ++k) {
            const auto move = moves.at(static_cast<std::size_t>(k - 1) % moves.size());
            say("info depth " + std::to_string(depth) + " seldepth " + std::to_string(depth) +
                " multipv " + std::to_string(k) + " score cp " + std::to_string((depth * 10) - k) +
                " nodes 100 nps 1000 time 1 pv " + std::string(move) + " e7e5");
        }
    }
}

void interleaved_infos() {
    for (const std::string_view line : {
             "this is not UCI",
             "info string starting the search",
             "info depth 1 currmove e2e4 currmovenumber 1",
             "info nodes 100 nps 1000",
             "info depth 1 multipv 2 score cp 10 pv d2d4 d7d5",
             "info depth 1 multipv 1 score cp 30 pv e2e4 e7e5",
             "info depth 2 multipv 2 score cp 12 pv d2d4 g8f6",
             "info depth 2 multipv 1 score cp 28 pv e2e4 c7c5",
             "info depth 2 multipv 1 score cp 25 pv e2e4 e7e5 g1f3",
             "info depth 3 multipv 1 score cp 999 lowerbound pv a2a3",
             "info depth 3 multipv 3 score cp 0 pv h2h3",
             "info depth 3 multipv 2 score mate -2 pv d2d4 e7e5",
             "info depth x multipv 1 score cp 1 pv a2a4",
         }) {
        say(line);
    }
}

// The engine's loop over its input, until quit or the end of input.
int run(const std::vector<std::string>& args) {
    const std::string script = args.size() > 1 ? args.at(1) : "normal";
    const int delay_ms = args.size() > 2 ? number(args.at(2), 0) : 0;
    std::ofstream log;
    if (args.size() > 3) {
        log.open(args.at(3), std::ios::app);
    }

    Search search;
    int multipv = 1;
    int readyoks = 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (log.is_open()) {
            log << line << '\n' << std::flush;
        }
        if (line == "uci") {
            if (script == "garbage") {
                for (const std::string_view junk : {
                         "Fake engine by nobody",
                         "",
                         "readyok",
                         "id",
                         "option name",
                         "bestmove e2e4",
                         "uciok is coming",
                     }) {
                    say(junk);
                }
            }
            say("id name FakeUCI 1.0");
            say("id author nobody");
            if (script != "garbage") {
                say("option name MultiPV type spin default 1 min 1 max 500");
                say("option name UCI_LimitStrength type check default false");
                say("option name UCI_Elo type spin default 1320 min 1320 max 3190");
            }
            say("uciok");
        } else if (line == "isready") {
            if (script == "late-readyok" && readyoks > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
            ++readyoks;
            say("readyok");
        } else if (line.starts_with("setoption name MultiPV value ")) {
            multipv = value_after(line, "value ", 1);
        } else if (line.starts_with("go")) {
            const int movetime = value_after(line, "movetime ", 1000);
            if (script == "die-mid-search") {
                say("info depth 1 multipv 1 score cp 10 pv e2e4");
                return 0;
            }
            if (script == "interleaved-info") {
                interleaved_infos();
                search.start(movetime, true, "bestmove e2e4 ponder e7e5");
            } else {
                normal_infos(multipv);
                search.start(movetime, script != "no-bestmove",
                             "bestmove " + std::string(moves.front()));
            }
        } else if (line == "stop") {
            search.stop();
        } else if (line == "quit") {
            break;
        }
    }
    search.join();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(std::vector<std::string>(argv, argv + argc));
    } catch (const std::exception& error) {
        std::cerr << "fake-uci: " << error.what() << '\n';
        return 1;
    }
}

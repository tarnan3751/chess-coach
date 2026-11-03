// The UCI client over reproc++ (chesscoach/engine.hpp). All I/O with the child happens on the
// thread that called start, new_game or search: stdout is polled in short slices, each slice checks
// the deadline and the stop flag, and the searching thread writes stop itself, so stop() and
// stop_before() only set flags under the mutex that also marks a search as running and records its
// generation. A missed bound or a dead pipe kills the process and marks the engine dead for good
// (docs/reference/core.md, **The engine**).
#include <chesscoach/engine.hpp>

#include <reproc++/reproc.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace chesscoach {

using SteadyClock = std::chrono::steady_clock;

namespace {

// The engine's process and protocol state, all of it touched only by the calling thread but for
// running, the generations and stop_requested, which stop() and stop_before() read and write
// under the mutex.
struct EngineState {
    reproc::process process;
    std::string name;
    std::optional<std::pair<int, int>> elo_range; // UCI_Elo's min and max, when advertised
    std::optional<int> multipv_max;               // MultiPV's max, when advertised
    std::chrono::milliseconds reply_timeout{};
    std::string pending; // output read but not yet split into lines
    std::atomic<bool> alive{true};

    std::mutex mutex; // guards running and the generations; stop() and the calls take it
    bool running = false;
    std::int64_t running_generation = 0; // the running search's
    std::int64_t stopped_below = 0;      // the highest generation stop_before recorded
    std::atomic<bool> stop_requested{false};

    int multipv_sent = 1;        // UCI's default MultiPV
    bool limit_sent = false;     // UCI's default UCI_LimitStrength
    std::optional<int> elo_sent; // the last UCI_Elo sent, which the engine keeps
};

// How long one poll of stdout waits before the deadline and the stop flag are checked again.
constexpr std::chrono::milliseconds poll_slice{25};

// Writing to a dead engine's stdin raises SIGPIPE, which by default ends this process; with the
// signal ignored, the write fails with EPIPE and the engine is reported dead.
void ignore_sigpipe_once() {
#ifdef SIGPIPE
    static const bool ignored = [] {
        // NOLINTNEXTLINE(cert-err33-c): SIG_ERR would leave the default, and nothing better exists
        std::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    static_cast<void>(ignored);
#endif
}

std::vector<std::string_view> split_tokens(std::string_view text) {
    std::vector<std::string_view> tokens;
    while (!text.empty()) {
        const auto start = text.find_first_not_of(" \t");
        if (start == std::string_view::npos) {
            break;
        }
        text.remove_prefix(start);
        const auto end = text.find_first_of(" \t");
        tokens.push_back(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end);
    }
    return tokens;
}

std::optional<int> parse_int(std::string_view text) {
    int value = 0;
    const auto* const begin = text.data(); // NOLINT(bugprone-suspicious-stringview-data-usage)
    const auto* const end = begin + text.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    if (error != std::errc() || ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> parse_next_int(std::optional<std::string_view> token) {
    return token ? parse_int(*token) : std::nullopt;
}

// An info line with a multipv (1 when absent), a depth and an exact score; nullopt for anything
// else: info string, currmove and nps lines, lines with a bound score, unreadable numbers.
std::optional<SearchLine> parse_info(std::string_view line) {
    const auto tokens = split_tokens(line);
    if (tokens.empty() || tokens.front() != "info") {
        return std::nullopt;
    }
    SearchLine info;
    bool has_depth = false;
    bool has_score = false;
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const auto token = tokens.at(i);
        const auto next = [&]() -> std::optional<std::string_view> {
            return i + 1 < tokens.size() ? std::optional(tokens.at(++i)) : std::nullopt;
        };
        if (token == "string") {
            return std::nullopt;
        }
        if (token == "multipv" || token == "depth") {
            const auto value = parse_next_int(next());
            if (!value) {
                return std::nullopt;
            }
            (token == "multipv" ? info.multipv : info.depth) = *value;
            has_depth = has_depth || token == "depth";
        } else if (token == "score") {
            const auto kind = next();
            const auto value = parse_next_int(next());
            if (has_score || !kind || !value || (*kind != "cp" && *kind != "mate")) {
                return std::nullopt; // a second score makes the line ambiguous
            }
            has_score = true;
            (*kind == "cp" ? info.score_cp : info.score_mate) = value;
        } else if (token == "lowerbound" || token == "upperbound") {
            return std::nullopt;
        } else if (token == "pv") {
            for (++i; i < tokens.size(); ++i) {
                info.pv.emplace_back(tokens.at(i));
            }
        }
    }
    if (!has_depth || (!info.score_cp && !info.score_mate) || info.multipv < 1) {
        return std::nullopt;
    }
    return info;
}

// Kills the process and marks the engine dead; every later call reports died.
EngineError fail(EngineState& impl, EngineError error) {
    impl.alive = false;
    if (!impl.process.kill()) {
        static_cast<void>(impl.process.wait(reproc::milliseconds(1000)));
    }
    return error;
}

tl::expected<void, EngineError> send(EngineState& impl, std::string_view command) {
    std::string line(command);
    line += '\n';
    // reproc writes bytes; the line's chars are those bytes.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* data = reinterpret_cast<const std::uint8_t*>(line.data());
    std::size_t left = line.size();
    while (left > 0) {
        const auto [written, error] = impl.process.write(data, left);
        if (error) {
            return tl::unexpected(fail(impl, EngineError::died));
        }
        data += written;
        left -= written;
    }
    return {};
}

// The next line of the engine's output by the deadline. before_poll runs before every poll and may
// move the deadline, which the caller owns, so a stop's shorter bound holds for every later line.
template <typename BeforePoll>
tl::expected<std::string, EngineError>
read_line(EngineState& impl, SteadyClock::time_point& deadline, BeforePoll before_poll) {
    std::array<std::uint8_t, 4096> buffer{};
    while (true) {
        if (const auto end = impl.pending.find('\n'); end != std::string::npos) {
            std::string line = impl.pending.substr(0, end);
            impl.pending.erase(0, end + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }
        if (const auto sent = before_poll(deadline); !sent) {
            return tl::unexpected(sent.error());
        }
        const auto now = SteadyClock::now();
        if (now >= deadline) {
            return tl::unexpected(fail(impl, EngineError::timeout));
        }
        const auto slice =
            std::min(std::chrono::duration_cast<reproc::milliseconds>(deadline - now),
                     reproc::milliseconds(poll_slice));
        const auto [events, poll_error] = impl.process.poll(reproc::event::out, slice);
        if (poll_error && poll_error != std::errc::timed_out) {
            return tl::unexpected(fail(impl, EngineError::died));
        }
        if ((static_cast<unsigned>(events) & static_cast<unsigned>(reproc::event::out)) == 0U) {
            continue;
        }
        const auto [read, read_error] =
            impl.process.read(reproc::stream::out, buffer.data(), buffer.size());
        if (read_error) {
            return tl::unexpected(fail(impl, EngineError::died));
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): reproc reads bytes
        impl.pending.append(reinterpret_cast<const char*>(buffer.data()), read);
    }
}

tl::expected<std::string, EngineError> read_line(EngineState& impl,
                                                 SteadyClock::time_point deadline) {
    return read_line(
        impl, deadline,
        [](SteadyClock::time_point&) -> tl::expected<void, EngineError> { return {}; });
}

// Whether the line's first token, spaces and a carriage return aside, is the word.
bool starts_with_token(std::string_view line, std::string_view word) {
    const auto tokens = split_tokens(line);
    return !tokens.empty() && tokens.front() == word;
}

// Reads and drops lines up to one whose first token is `reply`, within reply_timeout.
tl::expected<void, EngineError> await(EngineState& impl, std::string_view reply) {
    const auto deadline = SteadyClock::now() + impl.reply_timeout;
    while (true) {
        const auto line = read_line(impl, deadline);
        if (!line) {
            return tl::unexpected(line.error());
        }
        if (starts_with_token(*line, reply)) {
            return {};
        }
    }
}

tl::expected<void, EngineError> ready(EngineState& impl) {
    return send(impl, "isready").and_then([&] { return await(impl, "readyok"); });
}

// The min and max of an option line's spin, when it gives both.
std::optional<std::pair<int, int>> spin_range(const std::vector<std::string_view>& tokens) {
    std::optional<int> low;
    std::optional<int> high;
    for (std::size_t i = 3; i + 1 < tokens.size(); ++i) {
        if (tokens.at(i) == "min") {
            low = parse_int(tokens.at(i + 1));
        } else if (tokens.at(i) == "max") {
            high = parse_int(tokens.at(i + 1));
        }
    }
    if (!low || !high || *low > *high) {
        return std::nullopt;
    }
    return std::pair{*low, *high};
}

// The handshake's lines up to uciok: the id name, and UCI_Elo's range and MultiPV's maximum when
// the engine offers them.
tl::expected<void, EngineError> handshake(EngineState& impl) {
    if (const auto sent = send(impl, "uci"); !sent) {
        return sent;
    }
    const auto deadline = SteadyClock::now() + impl.reply_timeout;
    while (true) {
        const auto line = read_line(impl, deadline);
        if (!line) {
            return tl::unexpected(line.error());
        }
        if (starts_with_token(*line, "uciok")) {
            return {};
        }
        const auto tokens = split_tokens(*line);
        if (tokens.size() > 2 && tokens.at(0) == "id" && tokens.at(1) == "name") {
            impl.name = std::string_view(*line).substr(line->find("name") + 5);
        }
        if (tokens.size() > 2 && tokens.at(0) == "option" && tokens.at(1) == "name") {
            if (tokens.at(2) == "UCI_Elo") {
                impl.elo_range = spin_range(tokens);
            } else if (tokens.at(2) == "MultiPV") {
                if (const auto range = spin_range(tokens); range && range->second >= 1) {
                    impl.multipv_max = range->second;
                }
            }
        }
    }
}

// Marks the engine as running a call for as long as it lives, or reports that it already is; a
// search's generation is recorded, and compared with the stopped ones, under the same lock.
class RunningCall {
  public:
    explicit RunningCall(EngineState& impl, std::int64_t generation = 0) : impl_(impl) {
        const std::scoped_lock lock(impl_.mutex);
        if (impl_.running) {
            return;
        }
        impl_.running = true;
        impl_.running_generation = generation;
        impl_.stop_requested = false;
        acquired_ = true;
        stale_ = generation < impl_.stopped_below;
    }
    RunningCall(const RunningCall&) = delete;
    RunningCall& operator=(const RunningCall&) = delete;
    RunningCall(RunningCall&&) = delete;
    RunningCall& operator=(RunningCall&&) = delete;
    ~RunningCall() {
        if (acquired_) {
            const std::scoped_lock lock(impl_.mutex);
            impl_.running = false;
            impl_.stop_requested = false;
        }
    }
    [[nodiscard]] bool acquired() const noexcept { return acquired_; }
    // The search's generation was stopped before it started.
    [[nodiscard]] bool stale() const noexcept { return stale_; }

  private:
    EngineState& impl_;
    bool acquired_ = false;
    bool stale_ = false;
};

} // namespace

struct Engine::Impl : EngineState {};

Engine::Engine(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Engine::Engine(Engine&& other) noexcept = default;
Engine& Engine::operator=(Engine&& other) noexcept = default;

Engine::~Engine() {
    if (impl_ && impl_->alive) {
        // quit, written without allocating, then the stop actions.
        constexpr std::array<std::uint8_t, 5> quit{'q', 'u', 'i', 't', '\n'};
        static_cast<void>(impl_->process.write(quit.data(), quit.size()));
        static_cast<void>(impl_->process.stop({
            .first = {.action = reproc::stop::wait, .timeout = reproc::milliseconds(1000)},
            .second = {.action = reproc::stop::terminate, .timeout = reproc::milliseconds(1000)},
            .third = {.action = reproc::stop::kill, .timeout = reproc::milliseconds(1000)},
        }));
    }
}

tl::expected<Engine, EngineError> Engine::start(const std::filesystem::path& binary,
                                                EngineOptions options) {
    ignore_sigpipe_once();
    auto impl = std::make_unique<Impl>();
    impl->reply_timeout = options.reply_timeout;
    std::vector<std::string> arguments{binary.string()};
    arguments.insert(arguments.end(), options.arguments.begin(), options.arguments.end());
    reproc::options process_options;
    process_options.redirect.err.type = reproc::redirect::discard;
    process_options.stop = {
        .first = {.action = reproc::stop::terminate, .timeout = reproc::milliseconds(1000)},
        .second = {.action = reproc::stop::kill, .timeout = reproc::milliseconds(1000)},
        .third = {.action = reproc::stop::noop, .timeout = reproc::milliseconds(0)},
    };
    if (impl->process.start(arguments, process_options)) {
        impl->alive = false;
        return tl::unexpected(EngineError::start_failed);
    }
    const auto started =
        handshake(*impl)
            .and_then([&] {
                return send(*impl,
                            "setoption name Threads value " + std::to_string(options.threads));
            })
            .and_then([&] {
                return send(*impl, "setoption name Hash value " + std::to_string(options.hash_mb));
            })
            .and_then([&] { return ready(*impl); });
    if (!started) {
        return tl::unexpected(started.error());
    }
    return Engine(std::move(impl));
}

const std::string& Engine::name() const noexcept { return impl_->name; }

bool Engine::is_alive() const noexcept { return impl_->alive; }

void Engine::stop() noexcept {
    const std::scoped_lock lock(impl_->mutex);
    if (impl_->running) {
        impl_->stop_requested = true;
    }
}

void Engine::stop_before(std::int64_t generation) noexcept {
    const std::scoped_lock lock(impl_->mutex);
    impl_->stopped_below = std::max(impl_->stopped_below, generation);
    if (impl_->running && impl_->running_generation < generation) {
        impl_->stop_requested = true;
    }
}

tl::expected<void, EngineError> Engine::new_game() {
    const RunningCall call(*impl_);
    if (!call.acquired()) {
        return tl::unexpected(EngineError::busy);
    }
    if (!impl_->alive) {
        return tl::unexpected(EngineError::died);
    }
    return send(*impl_, "ucinewgame").and_then([&] { return ready(*impl_); });
}

tl::expected<SearchResult, EngineError> Engine::search(const Position& position,
                                                       const SearchRequest& request) {
    if (request.lines < 1 || !(request.seconds > 0.0) ||
        !(request.seconds <= maximum_search_seconds)) {
        return tl::unexpected(EngineError::invalid_request);
    }
    const RunningCall call(*impl_, request.generation);
    if (!call.acquired()) {
        return tl::unexpected(EngineError::busy);
    }
    auto& impl = *impl_;
    SearchResult result;
    result.engine_name = impl.name;
    if (call.stale()) {
        result.stopped = true;
        return result;
    }
    if (!impl.alive) {
        return tl::unexpected(EngineError::died);
    }
    const int line_count =
        impl.multipv_max ? std::min(request.lines, *impl.multipv_max) : request.lines;
    std::vector<std::string> commands;
    if (line_count != impl.multipv_sent) {
        commands.push_back("setoption name MultiPV value " + std::to_string(line_count));
    }
    if (request.limit_elo) {
        if (!impl.elo_range) {
            return tl::unexpected(EngineError::unsupported);
        }
        const auto [low, high] = *impl.elo_range;
        result.limited = true;
        result.elo = std::clamp(*request.limit_elo, low, high);
        if (!impl.limit_sent) {
            commands.emplace_back("setoption name UCI_LimitStrength value true");
        }
        if (impl.elo_sent != result.elo) {
            commands.push_back("setoption name UCI_Elo value " + std::to_string(*result.elo));
        }
    } else if (impl.limit_sent) {
        commands.emplace_back("setoption name UCI_LimitStrength value false");
    }
    for (const auto& command : commands) {
        if (const auto sent = send(impl, command); !sent) {
            return tl::unexpected(sent.error());
        }
    }
    impl.multipv_sent = line_count;
    impl.limit_sent = result.limited;
    if (result.elo) {
        impl.elo_sent = result.elo;
    }

    const auto movetime = std::max<std::int64_t>(1, std::llround(request.seconds * 1000.0));
    const auto searched =
        ready(impl)
            .and_then([&] { return send(impl, "position fen " + position.fen() + " 0 1"); })
            .and_then([&] { return send(impl, "go movetime " + std::to_string(movetime)); });
    if (!searched) {
        return tl::unexpected(searched.error());
    }

    std::map<int, SearchLine> lines;
    // One deadline for the whole search: a stop shortens it, and the shorter bound holds for
    // every line after the stop.
    auto deadline = SteadyClock::now() + std::chrono::milliseconds(movetime) + impl.reply_timeout;
    const auto stop_if_asked =
        [&](SteadyClock::time_point& until) -> tl::expected<void, EngineError> {
        if (result.stopped || !impl.stop_requested) {
            return {};
        }
        result.stopped = true;
        until = std::min(until, SteadyClock::now() + impl.reply_timeout);
        return send(impl, "stop");
    };
    while (true) {
        const auto line = read_line(impl, deadline, stop_if_asked);
        if (!line) {
            return tl::unexpected(line.error());
        }
        const auto tokens = split_tokens(*line);
        if (!tokens.empty() && tokens.front() == "bestmove") {
            if (tokens.size() > 1 && tokens.at(1) != "(none)") {
                result.best_move = tokens.at(1);
            }
            break;
        }
        if (auto info = parse_info(*line); info && info->multipv <= line_count) {
            lines.insert_or_assign(info->multipv, std::move(*info));
        }
    }
    for (auto& [multipv, line] : lines) {
        result.lines.push_back(std::move(line));
    }
    return result;
}

} // namespace chesscoach

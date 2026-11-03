// The UCI client over reproc++ (chesscoach/engine.hpp). All I/O with the child happens on the
// thread that called start, new_game or search: stdout is polled in short slices, each slice checks
// the deadline and the stop flag, and the searching thread writes stop itself, so stop() only sets
// a flag under the mutex that also marks a search as running. A missed bound or a dead pipe kills
// the process and marks the engine dead for good (docs/reference/core.md, **The engine**).
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
// running and stop_requested, which stop() reads and writes under the mutex.
struct EngineState {
    reproc::process process;
    std::string name;
    std::optional<std::pair<int, int>> elo_range; // UCI_Elo's min and max, when advertised
    std::chrono::milliseconds reply_timeout{};
    std::string pending; // output read but not yet split into lines
    std::atomic<bool> alive{true};

    std::mutex mutex; // guards running; stop() and the start and end of a call take it
    bool running = false;
    std::atomic<bool> stop_requested{false};

    int multipv_sent = 1;    // UCI's default MultiPV
    bool limit_sent = false; // UCI's default UCI_LimitStrength
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
            if (!kind || !value || (*kind != "cp" && *kind != "mate")) {
                return std::nullopt;
            }
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
// move the deadline (a stop shortens it).
template <typename BeforePoll>
tl::expected<std::string, EngineError>
read_line(EngineState& impl, SteadyClock::time_point deadline, BeforePoll before_poll) {
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

// Reads and drops lines up to the one that is exactly `reply`, within reply_timeout.
tl::expected<void, EngineError> await(EngineState& impl, std::string_view reply) {
    const auto deadline = SteadyClock::now() + impl.reply_timeout;
    while (true) {
        const auto line = read_line(impl, deadline);
        if (!line) {
            return tl::unexpected(line.error());
        }
        if (*line == reply) {
            return {};
        }
    }
}

tl::expected<void, EngineError> ready(EngineState& impl) {
    return send(impl, "isready").and_then([&] { return await(impl, "readyok"); });
}

// The handshake's lines up to uciok: the id name, and UCI_Elo's range when the engine offers it.
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
        if (*line == "uciok") {
            return {};
        }
        const auto tokens = split_tokens(*line);
        if (tokens.size() > 2 && tokens.at(0) == "id" && tokens.at(1) == "name") {
            impl.name = std::string_view(*line).substr(line->find("name") + 5);
        }
        if (tokens.size() > 2 && tokens.at(0) == "option" && tokens.at(1) == "name" &&
            tokens.at(2) == "UCI_Elo") {
            std::optional<int> low;
            std::optional<int> high;
            for (std::size_t i = 3; i + 1 < tokens.size(); ++i) {
                if (tokens.at(i) == "min") {
                    low = parse_int(tokens.at(i + 1));
                } else if (tokens.at(i) == "max") {
                    high = parse_int(tokens.at(i + 1));
                }
            }
            if (low && high && *low <= *high) {
                impl.elo_range = std::pair{*low, *high};
            }
        }
    }
}

// Marks the engine as running a call for as long as it lives, or reports that it already is.
class RunningCall {
  public:
    explicit RunningCall(EngineState& impl) : impl_(impl) {
        const std::scoped_lock lock(impl_.mutex);
        if (impl_.running) {
            return;
        }
        impl_.running = true;
        impl_.stop_requested = false;
        acquired_ = true;
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

  private:
    EngineState& impl_;
    bool acquired_ = false;
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
    if (request.lines < 1 || !(request.seconds > 0.0)) {
        return tl::unexpected(EngineError::invalid_request);
    }
    const RunningCall call(*impl_);
    if (!call.acquired()) {
        return tl::unexpected(EngineError::busy);
    }
    auto& impl = *impl_;
    if (!impl.alive) {
        return tl::unexpected(EngineError::died);
    }
    SearchResult result;
    result.engine_name = impl.name;
    std::vector<std::string> commands;
    if (request.lines != impl.multipv_sent) {
        commands.push_back("setoption name MultiPV value " + std::to_string(request.lines));
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
        commands.push_back("setoption name UCI_Elo value " + std::to_string(*result.elo));
    } else if (impl.limit_sent) {
        commands.emplace_back("setoption name UCI_LimitStrength value false");
    }
    for (const auto& command : commands) {
        if (const auto sent = send(impl, command); !sent) {
            return tl::unexpected(sent.error());
        }
    }
    impl.multipv_sent = request.lines;
    impl.limit_sent = result.limited;

    const auto movetime = std::max<std::int64_t>(1, std::llround(request.seconds * 1000.0));
    const auto searched =
        ready(impl)
            .and_then([&] { return send(impl, "position fen " + position.fen() + " 0 1"); })
            .and_then([&] { return send(impl, "go movetime " + std::to_string(movetime)); });
    if (!searched) {
        return tl::unexpected(searched.error());
    }

    std::map<int, SearchLine> lines;
    auto const deadline =
        SteadyClock::now() + std::chrono::milliseconds(movetime) + impl.reply_timeout;
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
        if (auto info = parse_info(*line); info && info->multipv <= request.lines) {
            lines.insert_or_assign(info->multipv, std::move(*info));
        }
    }
    for (auto& [multipv, line] : lines) {
        result.lines.push_back(std::move(line));
    }
    return result;
}

} // namespace chesscoach

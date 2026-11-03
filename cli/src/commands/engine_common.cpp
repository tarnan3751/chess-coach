// What eval and explore --engine share (engine_common.hpp).
#include "engine_common.hpp"

#include <fmt/format.h>

#include <cstdlib>
#include <string_view>
#include <system_error>
#include <utility>

namespace chesscoach::cli {

namespace {

std::string_view describe(EngineError error) {
    switch (error) {
    case EngineError::start_failed:
        return "did not start";
    case EngineError::timeout:
        return "did not answer in time";
    case EngineError::died:
        return "died";
    case EngineError::busy:
        return "was busy";
    case EngineError::unsupported:
        return "does not support the request";
    case EngineError::invalid_request:
        return "was given an invalid request";
    }
    return "failed";
}

std::string_view describe(ProfileError error) {
    switch (error) {
    case ProfileError::unwritable:
        return "cannot be created or opened";
    case ProfileError::not_profiles:
        return "is not a profiles.db";
    case ProfileError::newer_version:
        return "was written by a newer version";
    case ProfileError::migration_failed:
        return "could not be migrated";
    case ProfileError::invalid_name:
    case ProfileError::duplicate_name:
    case ProfileError::no_such_profile:
    case ProfileError::last_profile:
        return "refused a profile change";
    case ProfileError::storage:
        return "could not be written";
    }
    return "failed";
}

bool is_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

// stockfish in one of the PATH's directories, or nullopt.
std::optional<std::filesystem::path> on_path() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read before any engine thread starts
    const char* path = std::getenv("PATH");
    if (path == nullptr) {
        return std::nullopt;
    }
#ifdef _WIN32
    constexpr char separator = ';';
    constexpr std::string_view name = "stockfish.exe";
#else
    constexpr char separator = ':';
    constexpr std::string_view name = "stockfish";
#endif
    std::string_view dirs(path);
    while (!dirs.empty()) {
        const auto end = dirs.find(separator);
        const auto dir = dirs.substr(0, end);
        dirs.remove_prefix(end == std::string_view::npos ? dirs.size() : end + 1);
        if (!dir.empty()) {
            auto candidate = std::filesystem::path(dir) / name;
            if (is_file(candidate)) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

} // namespace

Result<ProfileStore> open_profiles(const std::string& data_dir) {
    auto profiles = ProfileStore::open(data_dir);
    if (!profiles) {
        return fail(fmt::format("profiles.db in {} {}", data_dir, describe(profiles.error())));
    }
    return std::move(*profiles);
}

Result<std::filesystem::path> find_engine(const std::optional<std::string>& given) {
    if (given) {
        if (!is_file(*given)) {
            return fail(fmt::format("no engine at {}", *given));
        }
        return std::filesystem::path(*given);
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read before any engine thread starts
    if (const char* named = std::getenv("CHESS_COACH_STOCKFISH"); named != nullptr && *named != 0) {
        if (!is_file(named)) {
            return fail(fmt::format("no engine at {} (CHESS_COACH_STOCKFISH)", named));
        }
        return std::filesystem::path(named);
    }
    if (auto found = on_path()) {
        return std::move(*found);
    }
    return fail("no engine: give --stockfish, set CHESS_COACH_STOCKFISH or put stockfish on the "
                "PATH");
}

Result<EngineAnswer> search_through_cache(ProfileStore& profiles, const Position& position,
                                          const EngineQuery& query,
                                          const EngineSettings& settings) {
    if (query.lines < 1) {
        return fail(fmt::format("--lines {} is not at least 1", query.lines));
    }
    if (!(query.seconds > 0.0)) {
        return fail(fmt::format("--seconds {} is not above 0", query.seconds));
    }
    const Key key = position.key();
    if (auto hit = profiles.cache_lookup(key, query.lines, query.min_depth)) {
        return EngineAnswer{.search = std::move(*hit), .cached = true};
    }
    const auto binary = find_engine(settings.stockfish);
    if (!binary) {
        return tl::unexpected(binary.error());
    }
    EngineOptions options;
    options.threads = settings.threads;
    options.hash_mb = settings.hash_mb;
    auto engine = Engine::start(*binary, options);
    if (!engine) {
        return fail(fmt::format("the engine {} {}", binary->string(), describe(engine.error())));
    }
    const auto result = engine->search(position, {.lines = query.lines, .seconds = query.seconds});
    if (!result) {
        return fail(fmt::format("the engine {} {}", engine->name(), describe(result.error())));
    }
    auto stored = profiles.cache_store(key, query.lines, *result);
    if (!stored) {
        return fail(fmt::format("profiles.db {}", describe(stored.error())));
    }
    return EngineAnswer{.search = std::move(*stored), .cached = false};
}

std::string score_text(const SearchLine& line) {
    return line.score_mate ? fmt::format("mate {}", *line.score_mate)
                           : fmt::format("cp {}", line.score_cp.value_or(0));
}

std::string line_text(const Position& position, const SearchLine& line) {
    auto text = fmt::format("depth {} {}", line.depth, score_text(line));
    if (!line.pv.empty()) {
        text += ' ';
        text += pv_san(position, line.pv);
    }
    return text;
}

std::string pv_san(const Position& position, const std::vector<std::string>& pv,
                   std::size_t max_moves) {
    auto current = position;
    std::string text;
    for (std::size_t i = 0; i < pv.size() && i < max_moves; ++i) {
        if (!text.empty()) {
            text += ' ';
        }
        const auto played = current.play_uci(pv.at(i));
        if (!played) {
            text += pv.at(i);
            break;
        }
        text += played->san;
    }
    return text;
}

} // namespace chesscoach::cli

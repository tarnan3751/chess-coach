// `chess-coach-pipeline trim` (trim.hpp).
#include "trim.hpp"

#include "eval_merge.hpp"
#include "game_scanner.hpp"
#include "options.hpp"
#include "zstd_istream.hpp"
#include "zstd_ostream.hpp"

#include <fmt/format.h>

#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <set>
#include <system_error>
#include <utility>

namespace chesscoach::pipeline {

namespace {

// zstd's level and worker threads for the files trim writes.
constexpr int compression_level = 9;
constexpr int compression_workers = 4;

constexpr std::string_view eval_name = "lichess_db_eval.jsonl.zst";

// The tags written, in this order: the ones check_headers reads.
constexpr std::array<std::string_view, 7> written_tags{
    "Event", "Result", "WhiteElo", "BlackElo", "Variant", "SetUp", "FEN",
};
constexpr std::size_t result_tag = 1;

bool is_result(std::string_view token) {
    return token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*";
}

// The move a token holds as the build's parser reads it, or empty for none: a NAG ($n) holds
// none; a move number (12., 12... or ...) before the move is no part of it, glued or not; and a
// token strip_glyphs leaves empty, a glyph alone, is one the scanner ignores.
std::string_view move_of(std::string_view token) {
    if (token.front() == '$') {
        return {};
    }
    std::size_t digits = 0;
    while (digits < token.size() &&
           std::isdigit(static_cast<unsigned char>(token.at(digits))) != 0) {
        ++digits;
    }
    std::size_t dots = digits;
    while (dots < token.size() && token.at(dots) == '.') {
        ++dots;
    }
    if (dots > digits) {
        token.remove_prefix(dots);
    }
    return strip_glyphs(token).empty() ? std::string_view{} : token;
}

struct Tag {
    std::string_view name;
    std::string_view value; // as written, its backslashes kept
};

// A tag line as the build's parser reads one, [Name "value"]: the name up to the first space or
// tab, then a quote, the value up to the first quote a backslash does not escape, a closing
// bracket and the end of the line. nullopt for a line the parser rejects (its closing quote or
// bracket missing) or reads otherwise.
std::optional<Tag> parse_tag(std::string_view line) {
    const std::size_t separator = line.find_first_of(" \t");
    if (separator == std::string_view::npos || separator + 1 >= line.size() ||
        line.at(separator + 1) != '"') {
        return std::nullopt;
    }
    const std::size_t begin = separator + 2;
    bool escaped = false;
    for (std::size_t i = begin; i < line.size(); ++i) {
        const char c = line.at(i);
        if (c == '"' && !escaped) {
            if (line.substr(i + 1) != "]") {
                return std::nullopt;
            }
            return Tag{
                .name = line.substr(1, separator - 1),
                .value = line.substr(begin, i - begin),
            };
        }
        escaped = c == '\\';
    }
    return std::nullopt;
}

// A tag's value as the parser gives it to the header checks: every backslash dropped.
std::string_view unescaped(std::string_view value, std::string& buffer) {
    if (value.find('\\') == std::string_view::npos) {
        return value;
    }
    buffer.clear();
    for (const char c : value) {
        if (c != '\\') {
            buffer += c;
        }
    }
    return buffer;
}

} // namespace

std::optional<TrimKind> trim_kind(const std::filesystem::path& path) {
    if (month_of(path)) {
        return TrimKind::games;
    }
    if (path.filename().string() == eval_name) {
        return TrimKind::evals;
    }
    return std::nullopt;
}

bool trim_keeps(const GameHeaders& headers) {
    const auto checked = check_headers(headers);
    return checked.has_value() || checked.error() == SkipReason::variant ||
           checked.error() == SkipReason::result;
}

std::string trimmed_movetext(std::string_view movetext, std::uint32_t max_ply,
                             std::string_view result) {
    // Comments, the clock and evaluation annotations among them, become spaces.
    std::string text;
    text.reserve(movetext.size());
    for (std::size_t i = 0; i < movetext.size();) {
        if (movetext.at(i) == '{') {
            if (const std::size_t close = movetext.find('}', i); close != std::string_view::npos) {
                text += ' ';
                i = close + 1;
                continue;
            }
        }
        text += movetext.at(i);
        ++i;
    }
    std::string out;
    std::uint32_t plies = 0;
    std::size_t begin = 0;
    while (true) {
        while (begin < text.size() &&
               std::isspace(static_cast<unsigned char>(text.at(begin))) != 0) {
            ++begin;
        }
        if (begin == text.size()) {
            break;
        }
        std::size_t end = begin;
        while (end < text.size() && std::isspace(static_cast<unsigned char>(text.at(end))) == 0) {
            ++end;
        }
        const std::string_view token = std::string_view(text).substr(begin, end - begin);
        begin = end;
        if (is_result(token)) {
            break;
        }
        const std::string_view move = move_of(token);
        if (move.empty()) {
            continue;
        }
        if (plies == max_ply) {
            break;
        }
        if (plies % 2 == 0) {
            out += fmt::format("{}. ", (plies / 2) + 1);
        }
        out += move;
        out += ' ';
        ++plies;
    }
    out += result;
    return out;
}

GameTrimCounts trim_games(std::istream& in, std::ostream& out, std::uint32_t max_ply) {
    GameTrimCounts counts;
    GameHeaders headers;
    std::array<std::optional<std::string>, written_tags.size()> values;
    std::string movetext;
    std::string buffer;
    bool in_game = false;
    bool in_moves = false;
    bool rejected = false; // a tag line the parser rejects
    const auto flush = [&] {
        if (in_game) {
            ++counts.seen;
            // A game whose moves never start, or with a tag line the parser rejects, is malformed
            // to the build: dropped, not given a result.
            if (in_moves && !rejected && trim_keeps(headers)) {
                ++counts.kept;
                for (std::size_t i = 0; i < written_tags.size(); ++i) {
                    if (const auto& value = values.at(i)) {
                        out << '[' << written_tags.at(i) << " \"" << *value << "\"]\n";
                    }
                }
                const std::string result = values.at(result_tag).value_or("*");
                out << '\n' << trimmed_movetext(movetext, max_ply, result) << "\n\n";
            }
        }
        headers.clear();
        values.fill(std::nullopt);
        movetext.clear();
        in_game = false;
        in_moves = false;
        rejected = false;
    };
    std::string line;
    // Once the output has failed, the rest of the input is not read.
    while (out && std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.starts_with('[')) {
            // A game ends where the build's chunker cuts it, and at any line beginning "[" after
            // its moves start, where the parser starts the next.
            if (in_moves || (in_game && line.starts_with("[Event "))) {
                flush();
            }
            in_game = true;
            if (const auto tag = parse_tag(line)) {
                headers.set(tag->name, unescaped(tag->value, buffer));
                for (std::size_t i = 0; i < written_tags.size(); ++i) {
                    if (written_tags.at(i) == tag->name) {
                        values.at(i) = std::string(tag->value);
                    }
                }
            } else {
                rejected = true;
            }
        } else if (!line.empty()) {
            if (!movetext.empty()) {
                movetext += ' ';
            }
            movetext += line;
            in_moves = true;
        } else if (in_game) {
            // A blank line after the tags starts the moves, as it does for the parser, even when
            // none follow: the game is written with its result alone.
            in_moves = true;
        }
    }
    flush();
    return counts;
}

EvalTrimCounts trim_evals(std::istream& in, std::ostream& out) {
    EvalTrimCounts counts;
    std::string line;
    while (out && std::getline(in, line)) {
        ++counts.lines;
        if (const auto trimmed = trimmed_eval_line(line)) {
            out << *trimmed << '\n';
        } else {
            ++counts.passed_through;
            out << line << '\n';
        }
    }
    return counts;
}

tl::expected<void, std::string> run_trim(const TrimOptions& options, std::ostream& report) {
    if (options.inputs.empty()) {
        return tl::unexpected(std::string("no inputs"));
    }
    std::vector<std::pair<std::filesystem::path, TrimKind>> plan;
    std::set<std::filesystem::path> names;
    std::error_code error;
    for (const auto& input : options.inputs) {
        const auto kind = trim_kind(input);
        if (!kind) {
            return tl::unexpected(
                fmt::format("{} is neither a month file (lichess_db_standard_rated_YYYY-MM"
                            ".pgn.zst) nor the evaluation database ({})",
                            input.string(), eval_name));
        }
        if (!names.insert(input.filename()).second) {
            return tl::unexpected(
                fmt::format("two inputs are named {}", input.filename().string()));
        }
        const auto output = options.out_dir / input.filename();
        auto partial = output;
        partial += ".partial";
        if (std::filesystem::exists(output, error) || std::filesystem::exists(partial, error)) {
            return tl::unexpected(fmt::format("{} or {} exists already; trim writes a new file",
                                              output.string(), partial.string()));
        }
        plan.emplace_back(input, *kind);
    }
    std::filesystem::create_directories(options.out_dir, error);
    if (error) {
        return tl::unexpected(fmt::format("{}: {}", options.out_dir.string(), error.message()));
    }
    for (const auto& [input, kind] : plan) {
        const auto started = std::chrono::steady_clock::now();
        const auto output = options.out_dir / input.filename();
        auto partial = output;
        partial += ".partial";
        std::string held;
        {
            ZstdStreambuf source(input);
            std::istream in(&source);
            ZstdOstreambuf sink(partial, compression_level, compression_workers);
            if (!sink.error().empty()) {
                std::filesystem::remove(partial, error);
                return tl::unexpected(sink.error());
            }
            std::ostream out(&sink);
            if (kind == TrimKind::games) {
                const auto counts = trim_games(in, out, options.max_ply);
                held = fmt::format("{} games seen, {} kept", counts.seen, counts.kept);
            } else {
                const auto counts = trim_evals(in, out);
                held = fmt::format("{} lines, {} passed through unchanged", counts.lines,
                                   counts.passed_through);
            }
            std::string failure;
            if (!source.error().empty()) {
                failure = fmt::format("{}: {}", input.string(), source.error());
            } else if (!sink.finish()) {
                failure = fmt::format("{}: {}", partial.string(), sink.error());
            }
            if (!failure.empty()) {
                std::filesystem::remove(partial, error);
                return tl::unexpected(failure);
            }
        }
        std::filesystem::rename(partial, output, error);
        if (error) {
            std::filesystem::remove(partial, error);
            return tl::unexpected(fmt::format("{}: {}", output.string(), error.message()));
        }
        const std::chrono::duration<double> seconds = std::chrono::steady_clock::now() - started;
        report << fmt::format("{}: {}, {} bytes in, {} bytes out, {:.1f} s\n",
                              input.filename().string(), held,
                              std::filesystem::file_size(input, error),
                              std::filesystem::file_size(output, error), seconds.count());
    }
    return {};
}

} // namespace chesscoach::pipeline

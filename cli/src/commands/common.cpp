// What the CLI's commands share (common.hpp).
#include "common.hpp"

#include <fmt/format.h>

#include <ranges>
#include <utility>

namespace chesscoach::cli {

namespace {

std::string_view describe(StoreError error) {
    switch (error) {
    case StoreError::missing_file:
        return "no such file";
    case StoreError::unreadable:
        return "not a readable SQLite database";
    case StoreError::no_meta:
        return "no meta table or schema_version";
    case StoreError::wrong_version:
        return "a schema_version this build does not read";
    case StoreError::schema_mismatch:
        return "its tables do not match the schema";
    }
    return "cannot open";
}

std::string_view describe(FenError error) {
    switch (error) {
    case FenError::malformed:
        return "not a FEN of four or six fields";
    case FenError::kings:
        return "a side does not have exactly one king";
    case FenError::illegal:
        return "not a legal position";
    }
    return "not a FEN";
}

std::string_view describe(MoveError error) {
    switch (error) {
    case MoveError::malformed:
        return "neither UCI nor SAN";
    case MoveError::illegal:
        return "not legal in the position";
    case MoveError::ambiguous:
        return "ambiguous in the position";
    }
    return "not a move";
}

// The words of text separated by spaces, tabs or line ends.
std::vector<std::string_view> words(std::string_view text) {
    std::vector<std::string_view> result;
    constexpr std::string_view separators = " \t\r\n";
    std::size_t begin = text.find_first_not_of(separators);
    while (begin != std::string_view::npos) {
        std::size_t end = text.find_first_of(separators, begin);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        result.push_back(text.substr(begin, end - begin));
        begin = text.find_first_not_of(separators, end);
    }
    return result;
}

} // namespace

tl::unexpected<CommandError> fail(std::string message) {
    return tl::unexpected(CommandError{std::move(message)});
}

Result<OpeningStore> open_store(const std::string& path) {
    auto store = OpeningStore::open(path);
    if (!store) {
        return fail(
            fmt::format("cannot open the opening database {}: {}", path, describe(store.error())));
    }
    return std::move(*store);
}

Result<Resolved> resolve_position(const std::optional<std::string>& fen,
                                  const std::optional<std::string>& moves) {
    if (fen) {
        auto position = Position::from_fen(*fen);
        if (!position) {
            return fail(fmt::format("bad FEN \"{}\": {}", *fen, describe(position.error())));
        }
        Resolved resolved{.position = std::move(*position), .path = {}};
        resolved.path.push_back(resolved.position.key());
        return resolved;
    }
    Resolved resolved{.position = Position::start(), .path = {}};
    resolved.path.push_back(resolved.position.key());
    if (!moves) {
        return resolved;
    }
    int number = 0;
    for (const std::string_view move : words(*moves)) {
        ++number;
        // A UCI move is never also SAN, so a move that is not UCI at all is read as SAN.
        auto played = resolved.position.play_uci(move);
        if (!played && played.error() == MoveError::malformed) {
            played = resolved.position.play_san(move);
        }
        if (!played) {
            return fail(
                fmt::format("bad move {} \"{}\": {}", number, move, describe(played.error())));
        }
        resolved.path.push_back(played->child);
    }
    return resolved;
}

std::optional<OpeningName> deepest_name(const OpeningStore& store, const std::vector<Key>& path) {
    for (const Key key : std::views::reverse(path)) {
        if (auto row = store.position(key); row && row->name) {
            OpeningName name{.eco = row->eco.value_or(""), .name = std::move(*row->name)};
            return name;
        }
    }
    return std::nullopt;
}

std::int64_t percent(std::int64_t part, std::int64_t whole) {
    if (whole <= 0) {
        return 0;
    }
    return ((200 * part) + whole) / (2 * whole);
}

std::string badge_letters(Badges badges) {
    std::string letters;
    if (badges.has(Badge::book)) {
        letters += 'B';
    }
    if (badges.has(Badge::popular)) {
        letters += 'P';
    }
    if (badges.has(Badge::dubious)) {
        letters += 'D';
    }
    return letters.empty() ? "-" : letters;
}

} // namespace chesscoach::cli

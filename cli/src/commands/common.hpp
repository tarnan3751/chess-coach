// What the CLI's commands share (docs/reference/cli.md): the error a command returns, the words of
// a line, finding and opening the opening database, resolving --fen or --moves to a position and
// the path of keys that reached it, the deepest name along that path, and the number formats of the
// output. Reads the
// database only through core's OpeningStore; chess rules only through core's Position.
#pragma once

#include <chesscoach/badges.hpp>
#include <chesscoach/key.hpp>
#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>

#include <tl/expected.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chesscoach::cli {

// A failure the user can act on (a database that does not open, a bad FEN or move): main prints
// the message as one line on stderr and exits with status 2.
struct CommandError {
    std::string message;
};

template <typename T> using Result = tl::expected<T, CommandError>;

// A failed Result carrying the message.
[[nodiscard]] tl::unexpected<CommandError> fail(std::string message);

// What an OpeningStore error says after "cannot open the opening database <path>: ".
[[nodiscard]] std::string_view describe(StoreError error);

// The words of text separated by spaces, tabs or line ends; views into text.
[[nodiscard]] std::vector<std::string_view> words(std::string_view text);

// Opens the opening database read-only through core's store.
[[nodiscard]] Result<OpeningStore> open_store(const std::string& path);

// The opening database a command reads (docs/reference/cli.md, **The opening database**), as core's
// locate_opening_database finds it: db (the --db path, empty when not given) when given, there or
// not, so a missing one is open_store's error; else <data_dir>/openings.db when it is there. An
// error names the file looked for when it is not there, or asks for --db or --data-dir when
// neither was given.
[[nodiscard]] Result<std::string> resolve_db(const std::string& db,
                                             const std::optional<std::string>& data_dir);

// resolve_db, then open_store.
[[nodiscard]] Result<OpeningStore> open_located_store(const std::string& db,
                                                      const std::optional<std::string>& data_dir);

// A position resolved from the command line, with the keys of every position from the one it
// started at to it: the start position and each position after a move of --moves, or the FEN's
// position alone; and the moves of --moves in UCI, none for a FEN.
struct Resolved {
    Position position;
    std::vector<Key> path;
    std::vector<std::string> moves;
};

// The position of --fen, or of --moves played from the start position (each move in UCI or SAN,
// separated by spaces), or the start position when neither is given. At most one of fen and
// moves is set.
[[nodiscard]] Result<Resolved> resolve_position(const std::optional<std::string>& fen,
                                                const std::optional<std::string>& moves);

// The ECO code and name of the last position along the path that has one (docs/database.md
// §10), or nullopt when none has.
struct OpeningName {
    std::string eco;
    std::string name;
};
[[nodiscard]] std::optional<OpeningName> deepest_name(const OpeningStore& store,
                                                      const std::vector<Key>& path);

// The badges as letters in the order B, P, D, or "-" for none (docs/database.md §7.3).
[[nodiscard]] std::string badge_letters(Badges badges);

// part / whole as a whole percentage rounded half up, for 0 <= part <= whole as the store's counts
// are; 0 when whole is 0.
[[nodiscard]] std::int64_t percent(std::int64_t part, std::int64_t whole);

} // namespace chesscoach::cli

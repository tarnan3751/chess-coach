// Putting a staged file in place of another and keeping the one it replaces, with a file at the
// name at every instant (docs/reference/core.md, **Replacing a file**): the swap both imports end
// with, profiles.db's (docs/database.md §12.5) and openings.db's (§6.1). No Qt, no SQLite.
#pragma once

#include <cstdint>
#include <filesystem>

namespace chesscoach {

// How the file replaced is kept as previous: a hard link, falling back to a copy on a file system
// without them; or a copy always, which tests pass to take the fallback.
enum class KeepPrevious : std::uint8_t { link_else_copy, copy };

// Replaces current with staged, keeping current's file as previous. When current is a regular
// file, previous is removed first (a symlink there is removed, never followed, and its mode does
// not matter), then made a hard link to current, or, when the link fails, a copy written to
// previous + ".importing" and renamed onto previous, so a copy failing midway never leaves a
// partial previous; then one rename puts staged onto current, which replaces it in one step on
// APFS and ext4. False when a step fails, and then staged and previous + ".importing" are
// removed and current is the file it was.
[[nodiscard]] bool replace_keeping_previous(const std::filesystem::path& staged,
                                            const std::filesystem::path& current,
                                            const std::filesystem::path& previous,
                                            KeepPrevious keep = KeepPrevious::link_else_copy);

} // namespace chesscoach

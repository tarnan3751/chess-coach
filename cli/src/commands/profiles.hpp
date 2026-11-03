// chess-coach-cli profile, and what repertoire and drill share with it (docs/reference/cli.md,
// **profile**): the profile store of --data-dir, a profile by name, a colour by name, and the
// profile subcommands list, create, remove, export and import. Every read and write goes through
// core's ProfileStore.
#pragma once

#include <chesscoach/position.hpp>
#include <chesscoach/profile_store.hpp>

#include "common.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace chesscoach::cli {

// The profile store of the data directory on the clock, as open_profiles opens it, and the
// profile named, which must exist.
struct OpenProfile {
    ProfileStore store;
    Profile profile;
};
[[nodiscard]] Result<OpenProfile> open_profile(const std::string& data_dir, const std::string& name,
                                               const Clock& clock);

// "white" or "black".
[[nodiscard]] Result<Color> parse_colour(const std::string& text);

struct ProfileOptions {
    std::string data_dir;
    std::string action; // list, create, remove, export or import
    std::string name;   // create, remove
    int tier = 2;       // create
    std::string path;   // export, import
};

// The clock stamps a profile created and runs an import's migrations.
[[nodiscard]] Result<void> run_profile(const ProfileOptions& options, std::ostream& out,
                                       const Clock& clock = system_clock_seconds);

} // namespace chesscoach::cli

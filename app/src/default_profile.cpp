// The profile the app works on (default_profile.hpp).
#include "default_profile.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace chesscoach::app {

std::optional<std::int64_t> default_profile(const ProfileStore& profiles) {
    const std::vector<Profile> all = profiles.profiles();
    const auto named = std::ranges::find(all, std::string("Default"), &Profile::name);
    if (named != all.end()) {
        return named->id;
    }
    if (all.empty()) {
        return std::nullopt;
    }
    return all.front().id;
}

} // namespace chesscoach::app

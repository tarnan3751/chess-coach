// The profile the app works on (docs/reference/app.md, **Spar**, **Profile worker**): the one named
// Default, which the profile store creates with profiles.db, else the first by id. The spar worker
// saves its sessions under it and the profile worker keeps its repertoires and cards.
#pragma once

#include <chesscoach/profile_store.hpp>

#include <cstdint>
#include <optional>

namespace chesscoach::app {

// nullopt when the store holds no profile.
[[nodiscard]] std::optional<std::int64_t> default_profile(const ProfileStore& profiles);

} // namespace chesscoach::app

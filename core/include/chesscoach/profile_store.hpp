// ProfileStore: profiles.db in the data directory (docs/database.md §12), the one way anything
// reads or writes the profiles and the engine cache (docs/reference/core.md, **The profile
// store**). Opened read-write; created at version 1 when absent or zero-length; migrated when
// older, each migration's script in its own transaction that sets schema_version last; refused,
// unmodified, when newer or when it is a non-empty file without meta. Timestamps are Unix seconds
// UTC from an injected clock, so tests are deterministic. Used from one thread at a time. Writes
// return errors as values; a read that fails after a successful open (an I/O error, a file
// corrupted on disk) throws std::runtime_error, as OpeningStore's do.
#pragma once

#include <chesscoach/engine.hpp>
#include <chesscoach/key.hpp>
#include <chesscoach/tier.hpp>

#include <tl/expected.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace chesscoach {

// Unix seconds, UTC.
using Clock = std::function<std::int64_t()>;

// The default clock: the system's.
[[nodiscard]] std::int64_t system_clock_seconds();

// One schema version's script: it takes a file from version - 1 to version. A script may call
// chesscoach_now(), which the store registers on its connection and answers from its clock.
struct Migration {
    int version = 0;
    std::string_view sql;
};

// The real migrations, version 1 first. Version 1 creates meta, profiles and engine_cache, and
// the profile Default at tier 2.
[[nodiscard]] std::span<const Migration> profile_migrations() noexcept;

// The version this build writes: the last of profile_migrations().
inline constexpr int profile_schema_version = 1;

enum class ProfileError : std::uint8_t {
    unwritable,       // the data directory or profiles.db cannot be created or opened
    not_profiles,     // a non-empty file that is not SQLite, or has no meta table or schema_version
    newer_version,    // a schema_version above the last migration's; the file is not modified
    migration_failed, // a script failed; the file stays at the last version that succeeded
    invalid_name,     // an empty name
    duplicate_name,   // a name another profile has
    no_such_profile,  // no profile has the id
    last_profile,     // remove_profile of the only profile
    storage,          // a write failed (disk full, read-only file)
};

struct Profile {
    std::int64_t id = 0;
    std::string name;
    Tier tier{};
    std::int64_t created_at = 0;

    friend bool operator==(const Profile&, const Profile&) = default;
};

// The lines of one cached search, by multipv, with the engine that found them and when.
struct CachedSearch {
    std::vector<SearchLine> lines;
    std::string engine;
    std::int64_t created_at = 0;

    friend bool operator==(const CachedSearch&, const CachedSearch&) = default;
};

class ProfileStore {
  public:
    // profiles.db inside data_dir, which is created when missing. migrations defaults to the real
    // ones; tests pass longer lists to exercise migrating, ordering and a failing script.
    [[nodiscard]] static tl::expected<ProfileStore, ProfileError>
    open(const std::filesystem::path& data_dir, Clock clock = system_clock_seconds,
         std::span<const Migration> scripts = profile_migrations());

    ProfileStore(ProfileStore&& other) noexcept;
    ProfileStore& operator=(ProfileStore&& other) noexcept;
    ProfileStore(const ProfileStore&) = delete;
    ProfileStore& operator=(const ProfileStore&) = delete;
    ~ProfileStore();

    // The file's schema version after open: the last migration's.
    [[nodiscard]] int schema_version() const noexcept;

    // Every profile, by id.
    [[nodiscard]] std::vector<Profile> profiles() const;

    [[nodiscard]] tl::expected<Profile, ProfileError> create_profile(std::string_view name,
                                                                     Tier tier);
    [[nodiscard]] tl::expected<Profile, ProfileError> rename_profile(std::int64_t id,
                                                                     std::string_view name);
    [[nodiscard]] tl::expected<Profile, ProfileError> set_tier(std::int64_t id, Tier tier);
    [[nodiscard]] tl::expected<void, ProfileError> remove_profile(std::int64_t id);

    // §12.2: a hit when at least one row exists for (key, lines) and the least depth among them is
    // at least min_depth. Callers never look up for a limited-strength search.
    [[nodiscard]] std::optional<CachedSearch> cache_lookup(Key key, int lines, int min_depth) const;

    // §12.2: the result's lines replace the stored ones, in one transaction, when nothing is
    // stored or their least depth is at least the stored least depth; otherwise the stored ones
    // are kept. Returns what the cache now holds for (key, lines): the stored lines when they were
    // kept, else the result's. A limited result, a stopped one, or one without lines is not
    // written and comes back as it is.
    [[nodiscard]] tl::expected<CachedSearch, ProfileError> cache_store(Key key, int lines,
                                                                       const SearchResult& result);

  private:
    struct Impl;
    explicit ProfileStore(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace chesscoach

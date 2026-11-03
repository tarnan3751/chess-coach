// The profile store over SQLiteCpp (chesscoach/profile_store.hpp; docs/database.md §12). The
// connection registers chesscoach_now(), answered from the injected clock, before any migration
// runs, so version 1's script stays plain SQL and its Default profile gets a deterministic
// created_at. Every statement prepared on the connection is finalized before the connection moves
// into the store.
#include <chesscoach/profile_store.hpp>

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace chesscoach {

namespace {

constexpr std::string_view version_1 =
    "CREATE TABLE meta (\n"
    "    key TEXT PRIMARY KEY,\n"
    "    value TEXT NOT NULL\n"
    ");\n"
    "CREATE TABLE profiles (\n"
    "    id INTEGER PRIMARY KEY,\n"
    "    name TEXT NOT NULL UNIQUE,\n"
    "    tier INTEGER NOT NULL,\n"
    "    created_at INTEGER NOT NULL\n"
    ");\n"
    "CREATE TABLE engine_cache (\n"
    "    key INTEGER NOT NULL,\n"
    "    lines INTEGER NOT NULL,\n"
    "    line INTEGER NOT NULL,\n"
    "    depth INTEGER NOT NULL,\n"
    "    score_cp INTEGER,\n"
    "    score_mate INTEGER,\n"
    "    pv TEXT NOT NULL,\n"
    "    engine TEXT NOT NULL,\n"
    "    created_at INTEGER NOT NULL,\n"
    "    PRIMARY KEY (key, lines, line)\n"
    ") WITHOUT ROWID;\n"
    "INSERT INTO profiles (name, tier, created_at) VALUES ('Default', 2, chesscoach_now());\n";

constexpr std::array<Migration, 1> migrations{
    {
        {.version = 1, .sql = version_1},
    },
};

// chesscoach_now(): the store's clock, as an SQL function.
void sql_now(sqlite3_context* context, int /*count*/, sqlite3_value** /*values*/) {
    const auto& clock = *static_cast<const Clock*>(sqlite3_user_data(context));
    sqlite3_result_int64(context, clock());
}

// The schema_version meta records, or nullopt when the row is missing or not a positive decimal.
std::optional<int> read_version(const SQLite::Database& db) {
    SQLite::Statement query(db, "SELECT value FROM meta WHERE key = 'schema_version'");
    if (!query.executeStep()) {
        return std::nullopt;
    }
    const auto text = query.getColumn(0).getString();
    if (text.empty() || text.size() > 9 ||
        text.find_first_not_of("0123456789") != std::string::npos || text.front() == '0') {
        return std::nullopt;
    }
    return std::stoi(text);
}

// Runs one migration's script and records its version, in one transaction: either both happen
// or neither does.
void migrate(SQLite::Database& db, const Migration& migration) {
    SQLite::Transaction transaction(db);
    db.exec(std::string(migration.sql));
    SQLite::Statement record(db, "INSERT INTO meta (key, value) VALUES ('schema_version', ?1) "
                                 "ON CONFLICT (key) DO UPDATE SET value = excluded.value");
    record.bind(1, std::to_string(migration.version));
    record.exec();
    transaction.commit();
}

std::vector<std::string> split_pv(const std::string& text) {
    std::vector<std::string> moves;
    std::string_view rest(text);
    while (!rest.empty()) {
        const auto space = rest.find(' ');
        if (space != 0) {
            moves.emplace_back(rest.substr(0, space));
        }
        rest.remove_prefix(space == std::string_view::npos ? rest.size() : space + 1);
    }
    return moves;
}

std::string join_pv(const std::vector<std::string>& moves) {
    std::string text;
    for (const auto& move : moves) {
        if (!text.empty()) {
            text += ' ';
        }
        text += move;
    }
    return text;
}

int least_depth(const std::vector<SearchLine>& lines) {
    return std::ranges::min(lines, {}, &SearchLine::depth).depth;
}

bool is_unique_violation(const SQLite::Exception& error) {
    return error.getExtendedErrorCode() == SQLITE_CONSTRAINT_UNIQUE;
}

} // namespace

std::int64_t system_clock_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::span<const Migration> profile_migrations() noexcept { return migrations; }

struct ProfileStore::Impl {
    Impl(SQLite::Database database, std::unique_ptr<Clock> time, int version)
        : db(std::move(database)), clock(std::move(time)), schema_version(version),
          lookup(db, "SELECT line, depth, score_cp, score_mate, pv, engine, created_at "
                     "FROM engine_cache WHERE key = ?1 AND lines = ?2 ORDER BY line"),
          remove(db, "DELETE FROM engine_cache WHERE key = ?1 AND lines = ?2"),
          insert(db, "INSERT INTO engine_cache "
                     "(key, lines, line, depth, score_cp, score_mate, pv, engine, created_at) "
                     "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)") {}

    SQLite::Database db;
    std::unique_ptr<Clock> clock; // on the heap: chesscoach_now() holds its address
    int schema_version = 0;
    SQLite::Statement lookup;
    SQLite::Statement remove;
    SQLite::Statement insert;

    // The stored lines of (key, lines), whatever their depth.
    std::optional<CachedSearch> stored(Key key, int lines) {
        lookup.reset();
        lookup.bind(1, key_to_sql(key));
        lookup.bind(2, lines);
        std::optional<CachedSearch> found;
        while (lookup.executeStep()) {
            if (!found) {
                found = CachedSearch{
                    .lines = {},
                    .engine = lookup.getColumn(5).getString(),
                    .created_at = lookup.getColumn(6).getInt64(),
                };
            }
            SearchLine line{
                .multipv = lookup.getColumn(0).getInt(),
                .depth = lookup.getColumn(1).getInt(),
                .score_cp = {},
                .score_mate = {},
                .pv = split_pv(lookup.getColumn(4).getString()),
            };
            if (!lookup.getColumn(2).isNull()) {
                line.score_cp = lookup.getColumn(2).getInt();
            }
            if (!lookup.getColumn(3).isNull()) {
                line.score_mate = lookup.getColumn(3).getInt();
            }
            found->lines.push_back(std::move(line));
        }
        lookup.reset();
        return found;
    }

    std::optional<Profile> profile(std::int64_t id) const {
        SQLite::Statement query(db, "SELECT name, tier, created_at FROM profiles WHERE id = ?1");
        query.bind(1, id);
        if (!query.executeStep()) {
            return std::nullopt;
        }
        return Profile{
            .id = id,
            .name = query.getColumn(0).getString(),
            .tier = tier_of_row(id, query.getColumn(1).getInt64()),
            .created_at = query.getColumn(2).getInt64(),
        };
    }

    // The profile with the id, or no_such_profile.
    tl::expected<Profile, ProfileError> existing(std::int64_t id) const {
        if (auto found = profile(id)) {
            return std::move(*found);
        }
        return tl::unexpected(ProfileError::no_such_profile);
    }

    static Tier tier_of_row(std::int64_t id, std::int64_t index) {
        const auto tier = tier_from_index(index);
        if (!tier) {
            throw std::runtime_error("profiles.db: profile " + std::to_string(id) +
                                     " has no tier " + std::to_string(index));
        }
        return *tier;
    }
};

ProfileStore::ProfileStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ProfileStore::ProfileStore(ProfileStore&& other) noexcept = default;
ProfileStore& ProfileStore::operator=(ProfileStore&& other) noexcept = default;
ProfileStore::~ProfileStore() = default;

tl::expected<ProfileStore, ProfileError> ProfileStore::open(const std::filesystem::path& data_dir,
                                                            Clock clock,
                                                            std::span<const Migration> scripts) {
    assert(!scripts.empty() && scripts.front().version == 1);
    std::error_code error;
    std::filesystem::create_directories(data_dir, error);
    if (error) {
        return tl::unexpected(ProfileError::unwritable);
    }
    const auto path = data_dir / "profiles.db";
    const bool fresh =
        !std::filesystem::exists(path, error) || std::filesystem::file_size(path, error) == 0;
    auto time = std::make_unique<Clock>(std::move(clock));
    std::optional<SQLite::Database> db;
    try {
        db.emplace(path,
                   SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE); // NOLINT(bugprone-signed-bitwise)
        db->setBusyTimeout(2000);
        db->exec("PRAGMA foreign_keys = ON");
        db->createFunction("chesscoach_now", 0, false, time.get(), &sql_now);
    } catch (const SQLite::Exception&) {
        return tl::unexpected(ProfileError::unwritable);
    }

    int version = 0;
    if (!fresh) {
        try {
            if (!db->tableExists("meta")) {
                return tl::unexpected(ProfileError::not_profiles);
            }
            const auto stored = read_version(*db);
            if (!stored) {
                return tl::unexpected(ProfileError::not_profiles);
            }
            version = *stored;
        } catch (const SQLite::Exception&) {
            return tl::unexpected(ProfileError::not_profiles);
        }
        if (version > scripts.back().version) {
            return tl::unexpected(ProfileError::newer_version);
        }
    }
    for (const auto& migration : scripts) {
        if (migration.version <= version) {
            continue;
        }
        assert(migration.version == version + 1);
        try {
            migrate(*db, migration);
        } catch (const SQLite::Exception&) {
            return tl::unexpected(ProfileError::migration_failed);
        }
        version = migration.version;
    }
    try {
        return ProfileStore(std::make_unique<Impl>(std::move(*db), std::move(time), version));
    } catch (const SQLite::Exception&) {
        return tl::unexpected(ProfileError::not_profiles);
    }
}

int ProfileStore::schema_version() const noexcept { return impl_->schema_version; }

std::vector<Profile> ProfileStore::profiles() const {
    SQLite::Statement query(impl_->db,
                            "SELECT id, name, tier, created_at FROM profiles ORDER BY id");
    std::vector<Profile> profiles;
    while (query.executeStep()) {
        const auto id = query.getColumn(0).getInt64();
        profiles.push_back(Profile{
            .id = id,
            .name = query.getColumn(1).getString(),
            .tier = Impl::tier_of_row(id, query.getColumn(2).getInt64()),
            .created_at = query.getColumn(3).getInt64(),
        });
    }
    return profiles;
}

tl::expected<Profile, ProfileError> ProfileStore::create_profile(std::string_view name, Tier tier) {
    if (name.empty()) {
        return tl::unexpected(ProfileError::invalid_name);
    }
    try {
        SQLite::Statement insert(
            impl_->db, "INSERT INTO profiles (name, tier, created_at) VALUES (?1, ?2, ?3)");
        insert.bind(1, std::string(name));
        insert.bind(2, tier_index(tier));
        insert.bind(3, (*impl_->clock)());
        insert.exec();
        return impl_->existing(impl_->db.getLastInsertRowid());
    } catch (const SQLite::Exception& error) {
        return tl::unexpected(is_unique_violation(error) ? ProfileError::duplicate_name
                                                         : ProfileError::storage);
    }
}

tl::expected<Profile, ProfileError> ProfileStore::rename_profile(std::int64_t id,
                                                                 std::string_view name) {
    if (name.empty()) {
        return tl::unexpected(ProfileError::invalid_name);
    }
    try {
        SQLite::Statement update(impl_->db, "UPDATE profiles SET name = ?1 WHERE id = ?2");
        update.bind(1, std::string(name));
        update.bind(2, id);
        update.exec();
        return impl_->existing(id);
    } catch (const SQLite::Exception& error) {
        return tl::unexpected(is_unique_violation(error) ? ProfileError::duplicate_name
                                                         : ProfileError::storage);
    }
}

tl::expected<Profile, ProfileError> ProfileStore::set_tier(std::int64_t id, Tier tier) {
    try {
        SQLite::Statement update(impl_->db, "UPDATE profiles SET tier = ?1 WHERE id = ?2");
        update.bind(1, tier_index(tier));
        update.bind(2, id);
        update.exec();
        return impl_->existing(id);
    } catch (const SQLite::Exception&) {
        return tl::unexpected(ProfileError::storage);
    }
}

tl::expected<void, ProfileError> ProfileStore::remove_profile(std::int64_t id) {
    try {
        SQLite::Transaction transaction(impl_->db);
        if (!impl_->profile(id)) {
            return tl::unexpected(ProfileError::no_such_profile);
        }
        if (impl_->db.execAndGet("SELECT COUNT(*) FROM profiles").getInt64() <= 1) {
            return tl::unexpected(ProfileError::last_profile);
        }
        SQLite::Statement remove(impl_->db, "DELETE FROM profiles WHERE id = ?1");
        remove.bind(1, id);
        remove.exec();
        transaction.commit();
        return {};
    } catch (const SQLite::Exception&) {
        return tl::unexpected(ProfileError::storage);
    }
}

std::optional<CachedSearch> ProfileStore::cache_lookup(Key key, int lines, int min_depth) const {
    auto found = impl_->stored(key, lines);
    if (!found || least_depth(found->lines) < min_depth) {
        return std::nullopt;
    }
    return found;
}

tl::expected<CachedSearch, ProfileError> ProfileStore::cache_store(Key key, int lines,
                                                                   const SearchResult& result) {
    CachedSearch searched{
        .lines = result.lines,
        .engine = result.engine_name,
        .created_at = (*impl_->clock)(),
    };
    if (result.limited || result.stopped || result.lines.empty()) {
        return searched;
    }
    try {
        SQLite::Transaction transaction(impl_->db);
        if (auto kept = impl_->stored(key, lines);
            kept && least_depth(searched.lines) < least_depth(kept->lines)) {
            return std::move(*kept);
        }
        auto& remove = impl_->remove;
        remove.reset();
        remove.bind(1, key_to_sql(key));
        remove.bind(2, lines);
        remove.exec();
        auto& insert = impl_->insert;
        for (const auto& line : searched.lines) {
            insert.reset();
            insert.clearBindings();
            insert.bind(1, key_to_sql(key));
            insert.bind(2, lines);
            insert.bind(3, line.multipv);
            insert.bind(4, line.depth);
            if (line.score_cp) {
                insert.bind(5, *line.score_cp);
            }
            if (line.score_mate) {
                insert.bind(6, *line.score_mate);
            }
            insert.bind(7, join_pv(line.pv));
            insert.bind(8, searched.engine);
            insert.bind(9, searched.created_at);
            insert.exec();
        }
        transaction.commit();
        return searched;
    } catch (const SQLite::Exception&) {
        return tl::unexpected(ProfileError::storage);
    }
}

} // namespace chesscoach

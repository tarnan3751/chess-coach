// The profile store over SQLiteCpp (chesscoach/profile_store.hpp; docs/database.md §12). The
// connection registers chesscoach_now(), answered from the injected clock, before any migration
// runs, so version 1's script stays plain SQL and its Default profile gets a deterministic
// created_at. Every statement prepared on the connection is finalized before the connection moves
// into the store.
#include <chesscoach/profile_store.hpp>

#include <SQLiteCpp/Backup.h>
#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
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

// §12.3. A session belongs to a profile and goes with it.
constexpr std::string_view version_2 =
    "CREATE TABLE spar_sessions (\n"
    "    id INTEGER PRIMARY KEY,\n"
    "    profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,\n"
    "    tier INTEGER NOT NULL,\n"
    "    colour INTEGER NOT NULL,\n"
    "    seed INTEGER NOT NULL,\n"
    "    moves TEXT NOT NULL,\n"
    "    interruptions INTEGER NOT NULL,\n"
    "    phase INTEGER NOT NULL,\n"
    "    result TEXT,\n"
    "    started_at INTEGER NOT NULL,\n"
    "    ended_at INTEGER,\n"
    "    end_move INTEGER NOT NULL,\n"
    "    min_games INTEGER NOT NULL,\n"
    "    data_version INTEGER NOT NULL\n"
    ");\n";

// §12.4. A colour is 0 for white and 1 for black; a card's state columns are the scheduler's
// replay of its reviews; nodes, cards and reviews go with their profile, reviews with their card.
constexpr std::string_view version_3 =
    "CREATE TABLE repertoire_nodes (\n"
    "    profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,\n"
    "    colour INTEGER NOT NULL,\n"
    "    key INTEGER NOT NULL,\n"
    "    fen TEXT NOT NULL,\n"
    "    move TEXT NOT NULL,\n"
    "    created_at INTEGER NOT NULL,\n"
    "    PRIMARY KEY (profile_id, colour, key)\n"
    ") WITHOUT ROWID;\n"
    "CREATE TABLE cards (\n"
    "    id INTEGER PRIMARY KEY,\n"
    "    profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,\n"
    "    colour INTEGER NOT NULL,\n"
    "    key INTEGER NOT NULL,\n"
    "    move TEXT NOT NULL,\n"
    "    repetitions INTEGER NOT NULL,\n"
    "    interval_days INTEGER NOT NULL,\n"
    "    ease_milli INTEGER NOT NULL,\n"
    "    due_at INTEGER NOT NULL,\n"
    "    created_at INTEGER NOT NULL,\n"
    "    UNIQUE (profile_id, colour, key)\n"
    ");\n"
    "CREATE TABLE reviews (\n"
    "    id INTEGER PRIMARY KEY,\n"
    "    card_id INTEGER NOT NULL REFERENCES cards(id) ON DELETE CASCADE,\n"
    "    grade INTEGER NOT NULL,\n"
    "    reviewed_at INTEGER NOT NULL\n"
    ");\n"
    "CREATE INDEX cards_due ON cards (profile_id, colour, due_at, key);\n"
    "CREATE INDEX reviews_card ON reviews (card_id, id);\n";

constexpr std::array<Migration, 3> migrations{
    {
        {.version = 1, .sql = version_1},
        {.version = 2, .sql = version_2},
        {.version = 3, .sql = version_3},
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

// How long a connection waits for another's lock before it reports the file busy.
constexpr std::chrono::milliseconds busy_timeout{2000};

// A failure because another connection holds the file locked.
bool is_busy(const SQLite::Exception& error) {
    return error.getErrorCode() == SQLITE_BUSY || error.getErrorCode() == SQLITE_LOCKED;
}

// The schema_version of the profiles.db at path, read through a read-only connection: busy when
// another connection keeps it locked past the busy timeout, not_profiles when it is not SQLite or
// has no meta or no decimal schema_version.
tl::expected<int, ProfileError> stored_version(const std::filesystem::path& path) {
    try {
        SQLite::Database db(path, SQLite::OPEN_READONLY);
        db.setBusyTimeout(static_cast<int>(busy_timeout.count()));
        if (!db.tableExists("meta")) {
            return tl::unexpected(ProfileError::not_profiles);
        }
        const auto version = read_version(db);
        if (!version) {
            return tl::unexpected(ProfileError::not_profiles);
        }
        return *version;
    } catch (const SQLite::Exception& error) {
        return tl::unexpected(is_busy(error) ? ProfileError::busy : ProfileError::not_profiles);
    }
}

// Copies from into to with SQLite's backup API, which must report the copy done: a step that
// finds either file locked is retried until the busy timeout, then busy; any other failure is
// storage.
tl::expected<void, ProfileError> back_up(SQLite::Database& to, SQLite::Database& from) {
    try {
        to.setBusyTimeout(static_cast<int>(busy_timeout.count()));
        from.setBusyTimeout(static_cast<int>(busy_timeout.count()));
        SQLite::Backup backup(to, from);
        const auto until = std::chrono::steady_clock::now() + busy_timeout;
        while (true) {
            const int step = backup.executeStep();
            if (step == SQLITE_DONE) {
                return {};
            }
            if (step != SQLITE_BUSY && step != SQLITE_LOCKED) {
                return tl::unexpected(ProfileError::storage);
            }
            if (std::chrono::steady_clock::now() >= until) {
                return tl::unexpected(ProfileError::busy);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch (const SQLite::Exception& error) {
        return tl::unexpected(is_busy(error) ? ProfileError::busy : ProfileError::storage);
    }
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

constexpr std::string_view spar_columns =
    "id, profile_id, tier, colour, seed, moves, interruptions, phase, result, started_at, "
    "ended_at, end_move, min_games, data_version";

// A spar_sessions row as read with spar_columns.
SparSessionRow spar_row(const SQLite::Statement& query) {
    const auto id = query.getColumn(0).getInt64();
    const auto tier = tier_from_index(query.getColumn(2).getInt64());
    const auto colour = query.getColumn(3).getInt64();
    const auto phase = query.getColumn(7).getInt64();
    if (!tier || colour < 0 || colour > 1 || phase < 0 || phase > 2) {
        throw std::runtime_error("profiles.db: spar session " + std::to_string(id) +
                                 " has a tier, colour or phase out of range");
    }
    SparSessionRow row{
        .id = id,
        .profile_id = query.getColumn(1).getInt64(),
        .tier = *tier,
        .colour = colour == 0 ? Color::white : Color::black,
        .seed = std::bit_cast<std::uint64_t>(query.getColumn(4).getInt64()),
        .moves = split_pv(query.getColumn(5).getString()),
        .interruptions = query.getColumn(6).getInt64(),
        .phase = static_cast<Phase>(phase),
        .result = std::nullopt,
        .started_at = query.getColumn(9).getInt64(),
        .ended_at = std::nullopt,
        .end_move = query.getColumn(11).getInt(),
        .min_games = query.getColumn(12).getInt64(),
        .data_version = query.getColumn(13).getInt64(),
    };
    if (!query.getColumn(8).isNull()) {
        row.result = query.getColumn(8).getString();
    }
    if (!query.getColumn(10).isNull()) {
        row.ended_at = query.getColumn(10).getInt64();
    }
    return row;
}

bool is_unique_violation(const SQLite::Exception& error) {
    return error.getExtendedErrorCode() == SQLITE_CONSTRAINT_UNIQUE;
}

// §12.4's colour column.
int colour_index(Color colour) { return colour == Color::white ? 0 : 1; }

Color colour_of_row(std::int64_t index) {
    if (index != 0 && index != 1) {
        throw std::runtime_error("profiles.db: a colour " + std::to_string(index));
    }
    return index == 0 ? Color::white : Color::black;
}

Grade grade_of_row(std::int64_t value) {
    switch (value) {
    case 1:
        return Grade::again;
    case 3:
        return Grade::hard;
    case 4:
        return Grade::good;
    case 5:
        return Grade::easy;
    default:
        throw std::runtime_error("profiles.db: a grade " + std::to_string(value));
    }
}

// The choice with its FEN normalized, or nullopt when its FEN is not its key's position, the
// colour is not to move there or its move is not legal there.
std::optional<RepertoireChoice> validated(Color colour, const RepertoireChoice& choice) {
    const auto position = Position::from_fen(choice.fen);
    if (!position || position->key() != choice.key || position->side_to_move() != colour) {
        return std::nullopt;
    }
    auto child = *position;
    const auto played = child.play_uci(choice.move);
    if (!played) {
        return std::nullopt;
    }
    return RepertoireChoice{.key = choice.key, .fen = position->fen(), .move = played->uci};
}

constexpr const char* card_columns =
    "id, profile_id, colour, key, move, repetitions, interval_days, ease_milli, due_at, created_at";

Card card_of_row(const SQLite::Statement& row) {
    return Card{
        .id = row.getColumn(0).getInt64(),
        .profile_id = row.getColumn(1).getInt64(),
        .colour = colour_of_row(row.getColumn(2).getInt64()),
        .key = key_from_sql(row.getColumn(3).getInt64()),
        .move = row.getColumn(4).getString(),
        .state =
            CardState{
                .repetitions = row.getColumn(5).getInt(),
                .interval_days = row.getColumn(6).getInt(),
                .ease_milli = row.getColumn(7).getInt(),
                .due_at = row.getColumn(8).getInt64(),
            },
        .created_at = row.getColumn(9).getInt64(),
    };
}

// The card of a node, as a new card made now for the move: a card already there for the node
// keeps its id and loses its reviews. Inside the caller's transaction.
void make_card(const SQLite::Database& db, std::int64_t profile_id, Color colour, Key key,
               const std::string& move, std::int64_t now) {
    const CardState state = new_card(now);
    SQLite::Statement find(db, "SELECT id FROM cards WHERE profile_id = ?1 AND colour = ?2 AND "
                               "key = ?3");
    find.bind(1, profile_id);
    find.bind(2, colour_index(colour));
    find.bind(3, key_to_sql(key));
    if (find.executeStep()) {
        const std::int64_t id = find.getColumn(0).getInt64();
        find.reset();
        SQLite::Statement forget(db, "DELETE FROM reviews WHERE card_id = ?1");
        forget.bind(1, id);
        forget.exec();
        SQLite::Statement reset(db, "UPDATE cards SET move = ?1, repetitions = ?2, "
                                    "interval_days = ?3, ease_milli = ?4, due_at = ?5, "
                                    "created_at = ?6 WHERE id = ?7");
        reset.bind(1, move);
        reset.bind(2, state.repetitions);
        reset.bind(3, state.interval_days);
        reset.bind(4, state.ease_milli);
        reset.bind(5, state.due_at);
        reset.bind(6, now);
        reset.bind(7, id);
        reset.exec();
        return;
    }
    SQLite::Statement insert(db, "INSERT INTO cards (profile_id, colour, key, move, repetitions, "
                                 "interval_days, ease_milli, due_at, created_at) "
                                 "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");
    insert.bind(1, profile_id);
    insert.bind(2, colour_index(colour));
    insert.bind(3, key_to_sql(key));
    insert.bind(4, move);
    insert.bind(5, state.repetitions);
    insert.bind(6, state.interval_days);
    insert.bind(7, state.ease_milli);
    insert.bind(8, state.due_at);
    insert.bind(9, now);
    insert.exec();
}

// The move a node chose, or nullopt when the repertoire does not answer the position.
std::optional<std::string> node_move(const SQLite::Database& db, std::int64_t profile_id,
                                     Color colour, Key key) {
    SQLite::Statement find(db, "SELECT move FROM repertoire_nodes WHERE profile_id = ?1 AND "
                               "colour = ?2 AND key = ?3");
    find.bind(1, profile_id);
    find.bind(2, colour_index(colour));
    find.bind(3, key_to_sql(key));
    if (!find.executeStep()) {
        return std::nullopt;
    }
    return find.getColumn(0).getString();
}

void insert_node(const SQLite::Database& db, std::int64_t profile_id, Color colour,
                 const RepertoireChoice& choice, std::int64_t now) {
    SQLite::Statement insert(db, "INSERT INTO repertoire_nodes (profile_id, colour, key, fen, "
                                 "move, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
    insert.bind(1, profile_id);
    insert.bind(2, colour_index(colour));
    insert.bind(3, key_to_sql(choice.key));
    insert.bind(4, choice.fen);
    insert.bind(5, choice.move);
    insert.bind(6, now);
    insert.exec();
}

bool unsigned_key_before(const Card& a, const Card& b) { return a.key < b.key; }

// SQLite's open flags are ints, combined with | as its API documents.
int read_write_create() {
    return SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE; // NOLINT(bugprone-signed-bitwise)
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
    std::error_code error;
    std::filesystem::create_directories(data_dir, error);
    if (error) {
        return tl::unexpected(ProfileError::unwritable);
    }
    return open_file(data_dir / "profiles.db", std::move(clock), scripts);
}

tl::expected<ProfileStore, ProfileError>
ProfileStore::open_file(const std::filesystem::path& path, Clock clock,
                        std::span<const Migration> scripts) {
    assert(!scripts.empty() && scripts.front().version == 1);
    std::error_code error;
    const bool fresh =
        !std::filesystem::exists(path, error) || std::filesystem::file_size(path, error) == 0;
    auto time = std::make_unique<Clock>(std::move(clock));
    std::optional<SQLite::Database> db;
    try {
        db.emplace(path,
                   SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE); // NOLINT(bugprone-signed-bitwise)
        db->setBusyTimeout(static_cast<int>(busy_timeout.count()));
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
        } catch (const SQLite::Exception& error) {
            return tl::unexpected(is_busy(error) ? ProfileError::busy : ProfileError::not_profiles);
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
        } catch (const SQLite::Exception& error) {
            return tl::unexpected(is_busy(error) ? ProfileError::busy
                                                 : ProfileError::migration_failed);
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

tl::expected<std::int64_t, ProfileError>
ProfileStore::save_spar_session(std::int64_t profile_id, const SparSession& session,
                                std::int64_t started_at, std::optional<std::int64_t> ended_at) {
    try {
        if (!impl_->profile(profile_id)) {
            return tl::unexpected(ProfileError::no_such_profile);
        }
        const Summary summary = session.summary();
        const bool ended = session.phase() == Phase::ended;
        SQLite::Statement insert(
            impl_->db, "INSERT INTO spar_sessions (profile_id, tier, colour, seed, moves, "
                       "interruptions, phase, result, started_at, ended_at, end_move, min_games, "
                       "data_version) "
                       "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)");
        insert.bind(1, profile_id);
        insert.bind(2, tier_index(session.tier()));
        insert.bind(3, session.user_colour() == Color::white ? 0 : 1);
        insert.bind(4, std::bit_cast<std::int64_t>(summary.seed));
        insert.bind(5, join_pv(summary.moves));
        insert.bind(6, static_cast<std::int64_t>(summary.interruptions.size()));
        insert.bind(7, static_cast<int>(session.phase()));
        if (summary.result) {
            insert.bind(8, *summary.result);
        }
        insert.bind(9, started_at);
        if (ended && ended_at) {
            insert.bind(10, *ended_at);
        }
        insert.bind(11, session.config().end_move);
        insert.bind(12, session.config().sampler.min_games);
        insert.bind(13, session.data_version());
        insert.exec();
        return impl_->db.getLastInsertRowid();
    } catch (const SQLite::Exception&) {
        return tl::unexpected(ProfileError::storage);
    }
}

std::vector<SparSessionRow> ProfileStore::spar_sessions(std::int64_t profile_id) const {
    SQLite::Statement query(impl_->db, "SELECT " + std::string(spar_columns) +
                                           " FROM spar_sessions WHERE profile_id = ?1 ORDER BY id");
    query.bind(1, profile_id);
    std::vector<SparSessionRow> rows;
    while (query.executeStep()) {
        rows.push_back(spar_row(query));
    }
    return rows;
}

std::optional<SparSessionRow> ProfileStore::spar_session(std::int64_t id) const {
    SQLite::Statement query(impl_->db, "SELECT " + std::string(spar_columns) +
                                           " FROM spar_sessions WHERE id = ?1");
    query.bind(1, id);
    if (!query.executeStep()) {
        return std::nullopt;
    }
    return spar_row(query);
}

tl::expected<Summary, SparError> replay_spar_session(const OpeningStore& store,
                                                     const SparSessionRow& row) {
    if (opening_data_version(store) != row.data_version) {
        return tl::unexpected(SparError::replay_database_mismatch);
    }
    return SparSession::replay(store, row.tier, row.colour, row.seed, row.moves,
                               SparConfig{
                                   .end_move = row.end_move,
                                   .sampler = {.min_games = row.min_games},
                               });
}

tl::expected<ChoiceChange, ProfileError>
ProfileStore::set_choice(std::int64_t profile_id, Color colour, const RepertoireChoice& choice) {
    const auto valid = validated(colour, choice);
    if (!valid) {
        return tl::unexpected(ProfileError::invalid_choice);
    }
    try {
        SQLite::Transaction transaction(impl_->db);
        if (!impl_->profile(profile_id)) {
            return tl::unexpected(ProfileError::no_such_profile);
        }
        const std::int64_t now = (*impl_->clock)();
        const auto stored = node_move(impl_->db, profile_id, colour, valid->key);
        if (stored && *stored == valid->move) {
            return ChoiceChange::unchanged;
        }
        if (stored) {
            SQLite::Statement update(impl_->db, "UPDATE repertoire_nodes SET fen = ?1, move = ?2, "
                                                "created_at = ?3 WHERE profile_id = ?4 AND "
                                                "colour = ?5 AND key = ?6");
            update.bind(1, valid->fen);
            update.bind(2, valid->move);
            update.bind(3, now);
            update.bind(4, profile_id);
            update.bind(5, colour_index(colour));
            update.bind(6, key_to_sql(valid->key));
            update.exec();
        } else {
            insert_node(impl_->db, profile_id, colour, *valid, now);
        }
        make_card(impl_->db, profile_id, colour, valid->key, valid->move, now);
        transaction.commit();
        return stored ? ChoiceChange::changed : ChoiceChange::added;
    } catch (const SQLite::Exception&) {
        return tl::unexpected(ProfileError::storage);
    }
}

tl::expected<bool, ProfileError> ProfileStore::remove_choice(std::int64_t profile_id, Color colour,
                                                             Key key) {
    try {
        SQLite::Transaction transaction(impl_->db);
        SQLite::Statement node(impl_->db, "DELETE FROM repertoire_nodes WHERE profile_id = ?1 AND "
                                          "colour = ?2 AND key = ?3");
        node.bind(1, profile_id);
        node.bind(2, colour_index(colour));
        node.bind(3, key_to_sql(key));
        const bool removed = node.exec() > 0;
        // The card's reviews go with it (ON DELETE CASCADE).
        SQLite::Statement card(impl_->db, "DELETE FROM cards WHERE profile_id = ?1 AND "
                                          "colour = ?2 AND key = ?3");
        card.bind(1, profile_id);
        card.bind(2, colour_index(colour));
        card.bind(3, key_to_sql(key));
        card.exec();
        transaction.commit();
        return removed;
    } catch (const SQLite::Exception&) {
        return tl::unexpected(ProfileError::storage);
    }
}

std::vector<RepertoireNode> ProfileStore::repertoire_nodes(std::int64_t profile_id,
                                                           Color colour) const {
    SQLite::Statement query(impl_->db, "SELECT key, fen, move, created_at FROM repertoire_nodes "
                                       "WHERE profile_id = ?1 AND colour = ?2");
    query.bind(1, profile_id);
    query.bind(2, colour_index(colour));
    std::vector<RepertoireNode> nodes;
    while (query.executeStep()) {
        nodes.push_back(RepertoireNode{
            .choice =
                RepertoireChoice{
                    .key = key_from_sql(query.getColumn(0).getInt64()),
                    .fen = query.getColumn(1).getString(),
                    .move = query.getColumn(2).getString(),
                },
            .created_at = query.getColumn(3).getInt64(),
        });
    }
    std::ranges::sort(nodes, [](const RepertoireNode& a, const RepertoireNode& b) {
        return a.choice.key < b.choice.key;
    });
    return nodes;
}

Repertoire ProfileStore::repertoire(std::int64_t profile_id, Color colour) const {
    Repertoire chosen;
    for (auto& node : repertoire_nodes(profile_id, colour)) {
        chosen.emplace(node.choice.key, std::move(node.choice.move));
    }
    return chosen;
}

tl::expected<ChoiceImport, ProfileError>
ProfileStore::import_choices(std::int64_t profile_id, Color colour,
                             const std::vector<RepertoireChoice>& choices) {
    try {
        SQLite::Transaction transaction(impl_->db);
        if (!impl_->profile(profile_id)) {
            return tl::unexpected(ProfileError::no_such_profile);
        }
        const std::int64_t now = (*impl_->clock)();
        ChoiceImport counts;
        for (const auto& choice : choices) {
            const auto valid = validated(colour, choice);
            if (!valid) {
                return tl::unexpected(ProfileError::invalid_choice);
            }
            const auto stored = node_move(impl_->db, profile_id, colour, valid->key);
            if (stored) {
                ++(*stored == valid->move ? counts.unchanged : counts.conflicts);
                continue;
            }
            insert_node(impl_->db, profile_id, colour, *valid, now);
            make_card(impl_->db, profile_id, colour, valid->key, valid->move, now);
            ++counts.added;
        }
        transaction.commit();
        return counts;
    } catch (const SQLite::Exception&) {
        return tl::unexpected(ProfileError::storage);
    }
}

tl::expected<CardSync, ProfileError> ProfileStore::sync_cards(std::int64_t profile_id,
                                                              Color colour) {
    try {
        SQLite::Transaction transaction(impl_->db);
        const std::int64_t now = (*impl_->clock)();
        std::unordered_map<Key, std::string> card_moves;
        for (const Card& card : cards(profile_id, colour)) {
            card_moves.emplace(card.key, card.move);
        }
        CardSync counts;
        for (const auto& node : repertoire_nodes(profile_id, colour)) {
            const auto card = card_moves.find(node.choice.key);
            if (card != card_moves.end() && card->second == node.choice.move) {
                continue;
            }
            ++(card == card_moves.end() ? counts.created : counts.reset);
            make_card(impl_->db, profile_id, colour, node.choice.key, node.choice.move, now);
        }
        // Cards whose node is gone, with their reviews (ON DELETE CASCADE).
        SQLite::Statement orphans(impl_->db,
                                  "DELETE FROM cards WHERE profile_id = ?1 AND colour = ?2 AND "
                                  "NOT EXISTS (SELECT 1 FROM repertoire_nodes AS node WHERE "
                                  "node.profile_id = cards.profile_id AND "
                                  "node.colour = cards.colour AND node.key = cards.key)");
        orphans.bind(1, profile_id);
        orphans.bind(2, colour_index(colour));
        counts.deleted = orphans.exec();
        transaction.commit();
        return counts;
    } catch (const SQLite::Exception&) {
        return tl::unexpected(ProfileError::storage);
    }
}

std::vector<Card> ProfileStore::cards(std::int64_t profile_id, Color colour) const {
    SQLite::Statement query(impl_->db, std::string("SELECT ") + card_columns +
                                           " FROM cards WHERE profile_id = ?1 AND colour = ?2");
    query.bind(1, profile_id);
    query.bind(2, colour_index(colour));
    std::vector<Card> found;
    while (query.executeStep()) {
        found.push_back(card_of_row(query));
    }
    std::ranges::sort(found, unsigned_key_before);
    return found;
}

std::vector<Card> ProfileStore::due_cards(std::int64_t profile_id, Color colour,
                                          std::int64_t now) const {
    SQLite::Statement query(impl_->db, std::string("SELECT ") + card_columns +
                                           " FROM cards WHERE profile_id = ?1 AND colour = ?2 "
                                           "AND due_at <= ?3");
    query.bind(1, profile_id);
    query.bind(2, colour_index(colour));
    query.bind(3, now);
    std::vector<Card> found;
    while (query.executeStep()) {
        found.push_back(card_of_row(query));
    }
    std::ranges::sort(found, [](const Card& a, const Card& b) {
        return a.state.due_at != b.state.due_at ? a.state.due_at < b.state.due_at : a.key < b.key;
    });
    return found;
}

tl::expected<Card, ProfileError> ProfileStore::record_review(std::int64_t profile_id, Color colour,
                                                             std::int64_t card_id, Key key,
                                                             Grade grade,
                                                             std::int64_t reviewed_at) {
    try {
        SQLite::Transaction transaction(impl_->db);
        SQLite::Statement find(impl_->db, std::string("SELECT ") + card_columns +
                                              " FROM cards WHERE id = ?1 AND profile_id = ?2 "
                                              "AND colour = ?3 AND key = ?4");
        find.bind(1, card_id);
        find.bind(2, profile_id);
        find.bind(3, colour_index(colour));
        find.bind(4, key_to_sql(key));
        if (!find.executeStep()) {
            return tl::unexpected(ProfileError::no_such_card);
        }
        Card card = card_of_row(find);
        find.reset();
        card.state = review(card.state, grade, reviewed_at);
        SQLite::Statement insert(impl_->db, "INSERT INTO reviews (card_id, grade, reviewed_at) "
                                            "VALUES (?1, ?2, ?3)");
        insert.bind(1, card_id);
        insert.bind(2, static_cast<int>(grade));
        insert.bind(3, reviewed_at);
        insert.exec();
        SQLite::Statement update(impl_->db,
                                 "UPDATE cards SET repetitions = ?1, interval_days = ?2, "
                                 "ease_milli = ?3, due_at = ?4 WHERE id = ?5");
        update.bind(1, card.state.repetitions);
        update.bind(2, card.state.interval_days);
        update.bind(3, card.state.ease_milli);
        update.bind(4, card.state.due_at);
        update.bind(5, card_id);
        update.exec();
        transaction.commit();
        return card;
    } catch (const SQLite::Exception&) {
        return tl::unexpected(ProfileError::storage);
    }
}

std::vector<Review> ProfileStore::card_history(std::int64_t card_id) const {
    SQLite::Statement query(impl_->db, "SELECT grade, reviewed_at FROM reviews WHERE card_id = ?1 "
                                       "ORDER BY id");
    query.bind(1, card_id);
    std::vector<Review> history;
    while (query.executeStep()) {
        history.push_back(Review{
            .grade = grade_of_row(query.getColumn(0).getInt64()),
            .reviewed_at = query.getColumn(1).getInt64(),
        });
    }
    return history;
}

tl::expected<void, ProfileError>
ProfileStore::export_profiles(const std::filesystem::path& data_dir,
                              const std::filesystem::path& destination) {
    std::error_code error;
    const auto source = data_dir / "profiles.db";
    if (!std::filesystem::is_regular_file(source, error) ||
        std::filesystem::file_size(source, error) == 0) {
        return tl::unexpected(ProfileError::not_profiles);
    }
    if (std::filesystem::exists(destination, error)) {
        return tl::unexpected(ProfileError::unwritable);
    }
    auto store = open(data_dir);
    if (!store) {
        return tl::unexpected(store.error());
    }
    std::optional<SQLite::Database> copy;
    try {
        copy.emplace(destination, read_write_create());
    } catch (const SQLite::Exception&) {
        std::filesystem::remove(destination, error);
        return tl::unexpected(ProfileError::unwritable);
    }
    auto copied = back_up(*copy, store->impl_->db);
    copy.reset();
    // The copy holds what the store holds, meta and all, or it is not left behind.
    if (copied && !stored_version(destination)) {
        copied = tl::unexpected(ProfileError::storage);
    }
    if (!copied) {
        std::filesystem::remove(destination, error);
        return tl::unexpected(copied.error());
    }
    return {};
}

tl::expected<void, ProfileError>
ProfileStore::import_profiles(const std::filesystem::path& data_dir,
                              const std::filesystem::path& source, Clock clock) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error)) {
        return tl::unexpected(ProfileError::not_profiles);
    }
    const auto source_version = stored_version(source);
    if (!source_version) {
        return tl::unexpected(source_version.error());
    }
    if (*source_version > profile_schema_version) {
        return tl::unexpected(ProfileError::newer_version);
    }
    const auto current = data_dir / "profiles.db";
    const auto importing = data_dir / "profiles.db.importing";
    const auto previous = data_dir / "profiles.db.previous";
    // The staging file is removed before it is written, so it is never the source.
    if (std::filesystem::equivalent(source, importing, error)) {
        return tl::unexpected(ProfileError::unwritable);
    }

    std::filesystem::create_directories(data_dir, error);
    if (error) {
        return tl::unexpected(ProfileError::unwritable);
    }
    std::filesystem::remove(importing, error);
    const auto refuse = [&](ProfileError reason) -> tl::expected<void, ProfileError> {
        std::error_code ignored;
        std::filesystem::remove(importing, ignored);
        return tl::unexpected(reason);
    };
    {
        std::optional<SQLite::Database> from;
        std::optional<SQLite::Database> copy;
        try {
            from.emplace(source, SQLite::OPEN_READONLY);
            copy.emplace(importing, read_write_create());
        } catch (const SQLite::Exception& failure) {
            return refuse(is_busy(failure) ? ProfileError::busy : ProfileError::unwritable);
        }
        if (const auto copied = back_up(*copy, *from); !copied) {
            return refuse(copied.error());
        }
    }
    // A copy without the source's meta is never opened as a new file.
    if (stored_version(importing) != source_version) {
        return refuse(ProfileError::storage);
    }
    {
        auto migrated = open_file(importing, std::move(clock), profile_migrations());
        if (!migrated) {
            return refuse(migrated.error());
        }
    }

    const bool replacing = std::filesystem::exists(current, error);
    if (replacing) {
        // A rollback journal left beside the current file by a writer that stopped mid-write
        // would be played onto the imported file once that takes the name; opening the current
        // file read-write and reading it rolls the journal back into it first.
        try {
            SQLite::Database db(current, SQLite::OPEN_READWRITE);
            db.setBusyTimeout(static_cast<int>(busy_timeout.count()));
            static_cast<void>(db.execAndGet("SELECT count(*) FROM sqlite_master"));
        } catch (const SQLite::Exception& failure) {
            if (is_busy(failure)) {
                return refuse(ProfileError::busy);
            }
            // A current file that is not a database has no journal of its own to roll back; it
            // is replaced, unless a journal beside it remains (below).
        }
        for (const char* suffix : {"-journal", "-wal"}) {
            auto beside = current;
            beside += suffix;
            if (std::filesystem::exists(beside, error)) {
                return refuse(ProfileError::busy);
            }
        }
        std::filesystem::rename(current, previous, error);
        if (error) {
            return refuse(ProfileError::unwritable);
        }
    }
    std::filesystem::rename(importing, current, error);
    if (error) {
        // A profiles.db always remains: the one this import was to replace goes back.
        std::error_code ignored;
        if (replacing) {
            std::filesystem::rename(previous, current, ignored);
        }
        return refuse(ProfileError::unwritable);
    }
    return {};
}

} // namespace chesscoach

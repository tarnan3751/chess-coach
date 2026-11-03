// ProfileStore: profiles.db in the data directory (docs/database.md §12), the one way anything
// reads or writes the profiles, the engine cache, the spar sessions, the repertoires and their
// cards and reviews (docs/reference/core.md, **The profile store**). Opened read-write; created at
// version 1 when absent or zero-length; migrated when older, each migration's script in its own
// transaction that sets schema_version last; refused, unmodified, when newer or when it is a
// non-empty file without meta. Timestamps are Unix seconds UTC from an injected clock, so tests are
// deterministic. Used from one thread at a time. Writes return errors as values; a read that fails
// after a successful open (an I/O error, a file corrupted on disk) throws std::runtime_error, as
// OpeningStore's do.
#pragma once

#include <chesscoach/engine.hpp>
#include <chesscoach/key.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/repertoire.hpp>
#include <chesscoach/scheduler.hpp>
#include <chesscoach/spar.hpp>
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
// the profile Default at tier 2; version 2 creates spar_sessions (§12.3); version 3 creates
// repertoire_nodes, cards and reviews (§12.4).
[[nodiscard]] std::span<const Migration> profile_migrations() noexcept;

// The version this build writes: the last of profile_migrations().
inline constexpr int profile_schema_version = 3;

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
    invalid_choice,   // a choice whose FEN is not its key's, not its colour's to move, or whose
                      // move is not legal there
    no_such_card,     // no card has the id
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

// A saved spar session (§12.3): enough to list it and to replay it (SparSession::replay).
struct SparSessionRow {
    std::int64_t id = 0;
    std::int64_t profile_id = 0;
    Tier tier{};
    Color colour = Color::white; // the user's
    std::uint64_t seed = 0;
    std::vector<std::string> moves; // UCI, from the start
    std::int64_t interruptions = 0;
    Phase phase = Phase::book; // when saved: ended, or where the session stopped
    std::optional<std::string> result;
    std::int64_t started_at = 0;
    std::optional<std::int64_t> ended_at; // set exactly when phase is ended
    int end_move = 25;                    // the session's SparConfig, which replay needs
    std::int64_t min_games = 10;
    std::int64_t data_version = 0; // the openings.db data_version it played against

    friend bool operator==(const SparSessionRow&, const SparSessionRow&) = default;
};

// §12.4: a position a repertoire answers, and when its choice was made or last changed.
struct RepertoireNode {
    RepertoireChoice choice;
    std::int64_t created_at = 0;

    friend bool operator==(const RepertoireNode&, const RepertoireNode&) = default;
};

// §12.4: the card of a repertoire node; its state is the scheduler's replay of its reviews from
// created_at.
struct Card {
    std::int64_t id = 0;
    std::int64_t profile_id = 0;
    Color colour = Color::white;
    Key key = 0;
    std::string move; // UCI: the answer the card asks for
    CardState state;
    std::int64_t created_at = 0;

    friend bool operator==(const Card&, const Card&) = default;
};

// What set_choice did to the node.
enum class ChoiceChange : std::uint8_t { added, changed, unchanged };

// What import_choices did: choices added, choices already in the repertoire, and choices the
// repertoire answers differently, which it keeps.
struct ChoiceImport {
    int added = 0;
    int unchanged = 0;
    int conflicts = 0;

    friend bool operator==(const ChoiceImport&, const ChoiceImport&) = default;
};

// What sync_cards did: cards made for nodes without one, cards reset to their node's new move,
// and cards deleted, with their reviews, for nodes that are gone.
struct CardSync {
    int created = 0;
    int reset = 0;
    int deleted = 0;

    friend bool operator==(const CardSync&, const CardSync&) = default;
};

// Replays a saved session (SparSession::replay) with the end move and min_games it was played
// with; replay_database_mismatch when the store's data_version is not the one it played against,
// whose book replies may differ.
[[nodiscard]] tl::expected<Summary, SparError> replay_spar_session(const OpeningStore& store,
                                                                   const SparSessionRow& row);

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

    // §12.3: saves the session under the profile, with its phase now (ended, or where it
    // stopped), its end move and min_games, and the data_version of the openings.db it plays
    // against; ended_at is stored only when the session has ended. Returns the session's id;
    // no_such_profile when the profile does not exist, storage when the write fails. Removing a
    // profile removes its sessions.
    [[nodiscard]] tl::expected<std::int64_t, ProfileError>
    save_spar_session(std::int64_t profile_id, const SparSession& session, std::int64_t started_at,
                      std::optional<std::int64_t> ended_at);

    // The profile's saved sessions, by id.
    [[nodiscard]] std::vector<SparSessionRow> spar_sessions(std::int64_t profile_id) const;

    // The saved session with the id, or nullopt.
    [[nodiscard]] std::optional<SparSessionRow> spar_session(std::int64_t id) const;

    // §12.4: sets the profile's choice for the colour at a position, keeping its card in step in
    // the same transaction: a new node gets a new card, due now; a changed move resets the card
    // (its reviews deleted, its state and created_at restarted, its id kept). The choice must be
    // valid (invalid_choice: its FEN parses to its key, the colour is to move there and the move
    // is legal); the FEN is stored normalized.
    [[nodiscard]] tl::expected<ChoiceChange, ProfileError>
    set_choice(std::int64_t profile_id, Color colour, const RepertoireChoice& choice);

    // Removes the choice at the position with its card and the card's reviews; false when there
    // was none.
    [[nodiscard]] tl::expected<bool, ProfileError> remove_choice(std::int64_t profile_id,
                                                                 Color colour, Key key);

    // The colour's repertoire nodes, by key; and the same as the map coverage reads.
    [[nodiscard]] std::vector<RepertoireNode> repertoire_nodes(std::int64_t profile_id,
                                                               Color colour) const;
    [[nodiscard]] Repertoire repertoire(std::int64_t profile_id, Color colour) const;

    // Adds the choices whose positions the repertoire does not answer yet, with their cards, in one
    // transaction; a position it answers is kept, as unchanged or as a conflict.
    [[nodiscard]] tl::expected<ChoiceImport, ProfileError>
    import_choices(std::int64_t profile_id, Color colour,
                   const std::vector<RepertoireChoice>& choices);

    // The idempotent repair that brings the colour's cards in step with its nodes (§12.4).
    [[nodiscard]] tl::expected<CardSync, ProfileError> sync_cards(std::int64_t profile_id,
                                                                  Color colour);

    // The colour's cards, by key; and those due at now, by due_at and then key.
    [[nodiscard]] std::vector<Card> cards(std::int64_t profile_id, Color colour) const;
    [[nodiscard]] std::vector<Card> due_cards(std::int64_t profile_id, Color colour,
                                              std::int64_t now) const;

    // Applies the scheduler's review to the card and records the review, in one transaction.
    [[nodiscard]] tl::expected<Card, ProfileError> record_review(std::int64_t card_id, Grade grade,
                                                                 std::int64_t reviewed_at);

    // The card's reviews in the order they were applied.
    [[nodiscard]] std::vector<Review> card_history(std::int64_t card_id) const;

  private:
    struct Impl;
    explicit ProfileStore(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace chesscoach

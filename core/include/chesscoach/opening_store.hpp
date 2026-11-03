// OpeningStore: the read-only view of openings.db (docs/database.md §6), opened with
// SQLITE_OPEN_READONLY and memory-mapped, its lookup statements prepared once at open; lookups by
// key and tier, every one naming its tier, and the canonical dump (§11). SQLite stays behind a
// pointer, so no public header includes it. A store is used from one thread at a time: its
// prepared statements are not shared. Opening checks the file; a query on a file that fails
// after a successful open (an I/O error, a file corrupted on disk) throws std::runtime_error.
#pragma once

#include <chesscoach/badges.hpp>
#include <chesscoach/key.hpp>
#include <chesscoach/tier.hpp>

#include <tl/expected.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace chesscoach {

enum class StoreError : std::uint8_t {
    missing_file,    // no regular file at the path
    unreadable,      // SQLite cannot open or read it: not a database, or corrupt
    no_meta,         // no meta table, or no schema_version row in it
    wrong_version,   // a schema_version other than opening_schema_version
    schema_mismatch, // the right version, but positions, nodes or moves missing or misshapen
};

// The meta table (§6.1): every row, ordered by key bytewise.
struct Meta {
    std::map<std::string, std::string, std::less<>> entries;

    // The value of a key, or nullptr when the file has no such row.
    [[nodiscard]] const std::string* find(std::string_view key) const;
};

// A positions row (§6.2); an absent value is nullopt.
struct PositionRow {
    Key key = 0;
    std::string fen;
    std::optional<std::string> eco;
    std::optional<std::string> name;
    std::optional<std::int64_t> eval_cp;
    std::optional<std::int64_t> eval_mate;
    std::optional<std::int64_t> eval_depth;
    std::optional<std::string> best_move;

    friend bool operator==(const PositionRow&, const PositionRow&) = default;
};

// A nodes row (§6.3).
struct NodeRow {
    Key key = 0;
    Tier tier{};
    std::int64_t games = 0;
    std::int64_t white = 0;
    std::int64_t draws = 0;
    std::int64_t black = 0;

    friend bool operator==(const NodeRow&, const NodeRow&) = default;
};

// A moves row (§6.4).
struct MoveRow {
    Key key = 0;
    Tier tier{};
    std::string move;
    Key child = 0;
    std::int64_t white = 0;
    std::int64_t draws = 0;
    std::int64_t black = 0;
    Badges badges;

    // The plays of the move at its tier.
    [[nodiscard]] constexpr std::int64_t total() const noexcept { return white + draws + black; }

    friend bool operator==(const MoveRow&, const MoveRow&) = default;
};

// Whole-database counts for the CLI's stats, each array indexed by tier_index.
struct StoreCounts {
    std::array<std::int64_t, tier_count> nodes{};       // nodes rows per tier
    std::array<std::int64_t, tier_count> moves{};       // moves rows per tier
    std::array<std::int64_t, tier_count> start_games{}; // the start position's games per tier
    std::int64_t positions = 0;                         // positions rows
    std::int64_t named = 0;                             // positions rows with a name
    std::int64_t evaluated = 0;                         // positions rows with an evaluation

    friend bool operator==(const StoreCounts&, const StoreCounts&) = default;
};

class OpeningStore {
  public:
    // Opens the file read-only. Refuses a missing file, a file SQLite cannot read, one without
    // meta or its schema_version, a schema_version other than opening_schema_version, and a
    // file whose positions, nodes or moves the lookups cannot read.
    [[nodiscard]] static tl::expected<OpeningStore, StoreError>
    open(const std::filesystem::path& path);

    OpeningStore(OpeningStore&& other) noexcept;
    OpeningStore& operator=(OpeningStore&& other) noexcept;
    OpeningStore(const OpeningStore&) = delete;
    OpeningStore& operator=(const OpeningStore&) = delete;
    ~OpeningStore();

    // The meta table, read once at open.
    [[nodiscard]] const Meta& meta() const noexcept;

    // The positions row of a key, or nullopt when the key has none.
    [[nodiscard]] std::optional<PositionRow> position(Key key) const;

    // The nodes row of a key at a tier, or nullopt when the key is off book there (§7.3).
    [[nodiscard]] std::optional<NodeRow> node(Key key, Tier tier) const;

    // The moves rows of a key at a tier, ordered by move bytewise; empty when there are none.
    [[nodiscard]] std::vector<MoveRow> moves(Key key, Tier tier) const;

    // The counts of StoreCounts, by SQL over the whole file; a tier with no row counts zero.
    [[nodiscard]] StoreCounts counts() const;

    // Writes the canonical dump of §11: every record, in its order, values as stored.
    void dump(std::ostream& out) const;

  private:
    struct Impl;
    explicit OpeningStore(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace chesscoach

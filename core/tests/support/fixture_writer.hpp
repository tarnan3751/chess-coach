// The writer the CLI's and the app's test fixtures share (core_test_fixture): an opening database
// written into core's schema (docs/database.md §6) from rows a fixture spells by hand, each
// position named by the moves (SAN, separated by spaces) that reach it from the start position,
// so that its key is the real one, from core's Position. The rows are each fixture's own
// (cli/tests/fixture.cpp, app/tests/fixture_db.cpp) and differ on purpose: the app's badges follow
// §7.3, the CLI's are set by hand for its scripted session, and only the app's hold a tier-4 tree.
#pragma once

#include <chesscoach/key.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chesscoach::testing {

struct FixturePosition {
    std::string_view moves;
    std::optional<std::string_view> eco;
    std::optional<std::string_view> name;
    std::optional<std::int64_t> eval_cp;
    std::optional<std::int64_t> eval_mate;
    std::optional<std::int64_t> eval_depth;
    std::optional<std::string_view> best_move;
};

struct FixtureNode {
    std::string_view moves;
    int tier;
    std::int64_t white;
    std::int64_t draws;
    std::int64_t black;
};

struct FixtureMove {
    std::string_view moves; // the parent
    int tier;
    std::string_view move;  // UCI
    std::string_view child; // the child's moves from the start
    std::int64_t white;
    std::int64_t draws;
    std::int64_t black;
    int badges;
};

// A fixture's rows of the four tables; meta as key and value.
struct FixtureRows {
    std::vector<FixturePosition> positions;
    std::vector<FixtureNode> nodes;
    std::vector<FixtureMove> moves;
    std::vector<std::pair<std::string_view, std::string_view>> meta;
};

// Rows of the three tables, built by name so that a fixture's tables read as one line per row.
[[nodiscard]] FixturePosition position(std::string_view moves, std::optional<std::string_view> eco,
                                       std::optional<std::string_view> name,
                                       std::optional<std::int64_t> eval_cp,
                                       std::optional<std::int64_t> eval_mate,
                                       std::optional<std::int64_t> eval_depth,
                                       std::optional<std::string_view> best_move);
[[nodiscard]] FixturePosition unevaluated(std::string_view moves,
                                          std::optional<std::string_view> eco,
                                          std::optional<std::string_view> name);
[[nodiscard]] FixtureNode node(std::string_view moves, int tier, std::int64_t white,
                               std::int64_t draws, std::int64_t black);
[[nodiscard]] FixtureMove move(std::string_view moves, int tier, std::string_view uci,
                               std::string_view child, std::int64_t white, std::int64_t draws,
                               std::int64_t black, int badges);

// The key of the position after the moves (SAN, separated by spaces) from the start position.
[[nodiscard]] Key key_after(std::string_view moves);

// Writes the rows to path, which must not exist, in one transaction; the nodes' games are their
// white, draws and black summed.
void write_fixture_db(const std::filesystem::path& path, const FixtureRows& rows);

// A fixture database in a fresh temporary directory, named prefix and a random number, removed
// with the object.
class FixtureDb {
  public:
    FixtureDb(std::string_view prefix, const FixtureRows& rows);
    FixtureDb(const FixtureDb&) = delete;
    FixtureDb& operator=(const FixtureDb&) = delete;
    FixtureDb(FixtureDb&&) = delete;
    FixtureDb& operator=(FixtureDb&&) = delete;
    ~FixtureDb();

    [[nodiscard]] std::string path() const { return file_.string(); }

  private:
    std::filesystem::path directory_;
    std::filesystem::path file_;
};

} // namespace chesscoach::testing

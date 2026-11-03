// Unit tests of chess-coach-cli repertoire, drill and profile (cli; unit), on a scratch data
// directory and, for coverage and gaps, the fixture database, with the clock injected: a line's
// choices added, replaced, listed and removed; coverage and gaps as core's compute_coverage gives
// them; a PGN imported; the drill flow (sync, list, review good, list again with the due date
// moved); the profiles listed, created, removed, exported and imported; and the errors each
// returns.
#include <chesscoach/opening_store.hpp>
#include <chesscoach/position.hpp>
#include <chesscoach/profile_store.hpp>
#include <chesscoach/repertoire.hpp>
#include <chesscoach/tier.hpp>

#include "commands/drill.hpp"
#include "commands/profiles.hpp"
#include "commands/repertoire.hpp"
#include "fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

using chesscoach::Color;
using chesscoach::Position;
using chesscoach::ProfileStore;
using chesscoach::cli::DrillOptions;
using chesscoach::cli::ProfileOptions;
using chesscoach::cli::RepertoireOptions;
using chesscoach::cli::test::FixtureDb;
using chesscoach::cli::test::key_after;

namespace {

// A directory of its own for each test, under the build directory, removed when it ends.
class Scratch {
  public:
    explicit Scratch(std::string_view name)
        : root_(std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / "repertoire" / name) {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    [[nodiscard]] std::string data() const { return (root_ / "data").string(); }
    [[nodiscard]] std::filesystem::path file(std::string_view name) const { return root_ / name; }

  private:
    std::filesystem::path root_;
};

// The tests' clock.
std::int64_t at_1000() { return 1000; }

// A command's output, or its error message prefixed with "error: ".
template <typename Command> std::string output(const Command& command) {
    std::ostringstream out;
    const auto result = command(out);
    return result ? out.str() : "error: " + result.error().message;
}

// The options of a repertoire or drill command on the scratch data directory, white's, Default's.
RepertoireOptions repertoire_on(const Scratch& scratch, std::string action) {
    RepertoireOptions options;
    options.data_dir = scratch.data();
    options.colour = "white";
    options.action = std::move(action);
    return options;
}

DrillOptions drill_on(const Scratch& scratch, std::string action) {
    DrillOptions options;
    options.data_dir = scratch.data();
    options.colour = "white";
    options.action = std::move(action);
    return options;
}

ProfileOptions profile_on(std::string data_dir, std::string action) {
    ProfileOptions options;
    options.data_dir = std::move(data_dir);
    options.action = std::move(action);
    return options;
}

std::string run(const RepertoireOptions& options) {
    return output([&options](std::ostream& out) {
        return chesscoach::cli::run_repertoire(options, out, at_1000);
    });
}

std::string run(const DrillOptions& options) {
    return output([&options](std::ostream& out) {
        return chesscoach::cli::run_drill(options, out, at_1000);
    });
}

std::string run(const ProfileOptions& options) {
    return output([&options](std::ostream& out) {
        return chesscoach::cli::run_profile(options, out, at_1000);
    });
}

// repertoire add of the line for the colour.
std::string add(const Scratch& scratch, std::string moves, std::string colour = "white") {
    RepertoireOptions options = repertoire_on(scratch, "add");
    options.moves = std::move(moves);
    options.colour = std::move(colour);
    return run(options);
}

// The normalized FEN after the moves (SAN, separated by spaces) from the start position.
std::string fen_after(std::string_view moves) {
    auto position = Position::start();
    std::istringstream words{std::string(moves)};
    std::string move;
    while (words >> move) {
        REQUIRE(position.play_san(move).has_value());
    }
    return position.fen();
}

// The line repertoire list prints for the choice at the position after the moves.
std::string listed(std::string_view moves, std::string_view uci, std::string_view san) {
    return fmt::format("{} {} {} {}\n", key_after(moves), uci, san, fen_after(moves));
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

} // namespace

TEST_CASE("repertoire add chooses the colour's moves along a line, replacing, and list shows them",
          "[cli][repertoire]") {
    const Scratch scratch("add");
    CHECK(add(scratch, "e4 e5 Nf3 Nc6 Bb5") == "added: 3\nchanged: 0\nunchanged: 0\n");
    // UCI and SAN mixed; 1. e4 is kept and 2. Nf3 replaced.
    CHECK(add(scratch, "e2e4 e7e5 Nc3") == "added: 0\nchanged: 1\nunchanged: 1\n");
    // By key, unsigned.
    CHECK(run(repertoire_on(scratch, "list")) == listed("e4 e5 Nf3 Nc6", "f1b5", "Bb5") +
                                                     listed("", "e2e4", "e4") +
                                                     listed("e4 e5", "b1c3", "Nc3"));
    // Black's repertoire is apart, and takes only black's moves from the same line.
    CHECK(add(scratch, "e4 e5 Nf3 Nc6", "black") == "added: 2\nchanged: 0\nunchanged: 0\n");
    RepertoireOptions black = repertoire_on(scratch, "list");
    black.colour = "black";
    CHECK(run(black) == listed("e4 e5 Nf3", "b8c6", "Nc6") + listed("e4", "e7e5", "e5"));
    CHECK(add(scratch, "e4 e9") == "error: bad move 2 \"e9\": neither UCI nor SAN");
}

TEST_CASE("repertoire remove removes only the choice at the position the line reaches",
          "[cli][repertoire]") {
    const Scratch scratch("remove");
    REQUIRE(add(scratch, "e4 e5 Nf3") == "added: 2\nchanged: 0\nunchanged: 0\n");
    RepertoireOptions remove = repertoire_on(scratch, "remove");
    CHECK(run(remove) == "removed: 1\n");
    CHECK(run(repertoire_on(scratch, "list")) == listed("e4 e5", "g1f3", "Nf3"));
    CHECK(run(remove) == "removed: 0\n");
    remove.moves = "e4";
    CHECK(run(remove) == "error: the line ends where the other colour is to move");
    // The card went with the choice.
    DrillOptions list = drill_on(scratch, "list");
    list.at = 1000;
    CHECK(run(list) ==
          fmt::format("2 {} g1f3 Nf3 due 1000 {}\n", key_after("e4 e5"), fen_after("e4 e5")));
}

TEST_CASE("repertoire coverage and gaps print core's coverage at the tier", "[cli][repertoire]") {
    const FixtureDb db;
    const Scratch scratch("coverage");
    REQUIRE(add(scratch, "e4") == "added: 1\nchanged: 0\nunchanged: 0\n");
    RepertoireOptions coverage_options = repertoire_on(scratch, "coverage");
    coverage_options.db = db.path();
    coverage_options.tier = 3;
    // 1. e4 answers the start; 1... e5 and 1... c5 are not answered, and the rest is below the
    // mass floor.
    CHECK(run(coverage_options) ==
          fmt::format("covered: 8.3%\nvisited: 5\ngaps: 2\n50.0% e4 e5 | {}\n41.7% e4 c5 | {}\n",
                      fen_after("e4 e5"), fen_after("e4 c5")));
    RepertoireOptions gaps = coverage_options;
    gaps.action = "gaps";
    gaps.limit = 1;
    CHECK(run(gaps) == fmt::format("50.0% e4 e5 | {}\n", fen_after("e4 e5")));

    // The same numbers as compute_coverage on the store's repertoire.
    auto openings = chesscoach::OpeningStore::open(db.path());
    REQUIRE(openings.has_value());
    auto profiles = ProfileStore::open(scratch.data());
    REQUIRE(profiles.has_value());
    const auto coverage =
        chesscoach::compute_coverage(*openings, profiles->repertoire(1, Color::white), Color::white,
                                     chesscoach::Tier::r1600_1799, chesscoach::CoverageConfig{});
    REQUIRE(coverage.gaps.size() == 2);
    CHECK(fmt::format("{:.1f}%", coverage.covered / coverage.total * 100.0) == "8.3%");
    CHECK(coverage.positions_visited == 5);
    CHECK(coverage.gaps.at(0).fen == fen_after("e4 e5"));
    CHECK(fmt::format("{:.1f}%", coverage.gaps.at(0).mass / coverage.total * 100.0) == "50.0%");
    CHECK(coverage.gaps.at(1).fen == fen_after("e4 c5"));

    RepertoireOptions no_db = coverage_options;
    no_db.db.clear();
    CHECK(run(no_db) == "error: repertoire coverage needs --db");
    gaps.tier = 7;
    CHECK(run(gaps) == "error: tier 7 is not one of 0 to 6");
}

TEST_CASE("repertoire import adds a PGN's games and variations", "[cli][repertoire]") {
    const Scratch scratch("import");
    REQUIRE(add(scratch, "d4") == "added: 1\nchanged: 0\nunchanged: 0\n");
    const auto pgn = scratch.file("lines.pgn");
    write_text(pgn, "[Event \"Mine\"]\n\n1. e4 e5 2. Nf3 (2. Nc3 Nf6) 2... Nc6 3. Bb5 *\n\n"
                    "[Event \"Also mine\"]\n\n1. e4 c5 2. Nf3 d6 3. Qx9 *\n");
    RepertoireOptions import = repertoire_on(scratch, "import");
    import.pgn = pgn.string();
    // 1. e4 conflicts with 1. d4 in the repertoire, which it keeps; 2. Nc3 conflicts with 2. Nf3
    // in the file, whose first choice wins.
    CHECK(run(import) == "games: 2\nvariations: 1\nchoices: 4\nadded: 3\nunchanged: 0\n"
                         "conflicts in the file: 1\nconflicts with the repertoire: 1\n"
                         "unparsable: 1\n");
    CHECK(run(repertoire_on(scratch, "list")) ==
          listed("e4 e5 Nf3 Nc6", "f1b5", "Bb5") + listed("", "d2d4", "d4") +
              listed("e4 c5", "g1f3", "Nf3") + listed("e4 e5", "g1f3", "Nf3"));
    import.pgn = scratch.file("missing.pgn").string();
    CHECK(run(import).starts_with("error: cannot read "));
}

TEST_CASE("drill syncs, lists the cards due, and a good review moves the due date",
          "[cli][drill]") {
    const Scratch scratch("drill");
    REQUIRE(add(scratch, "e4 e5 Nf3") == "added: 2\nchanged: 0\nunchanged: 0\n");
    const auto e4 = [](std::int64_t due) {
        return fmt::format("1 {} e2e4 e4 due {} {}\n", key_after(""), due, fen_after(""));
    };
    const std::string nf3 =
        fmt::format("2 {} g1f3 Nf3 due 1000 {}\n", key_after("e4 e5"), fen_after("e4 e5"));
    CHECK(run(drill_on(scratch, "sync")) == "created: 0\nreset: 0\ndeleted: 0\n");
    DrillOptions list = drill_on(scratch, "list");
    list.at = 999;
    CHECK(run(list).empty());
    // Due at once, when they were made, by due time and then key; the clock's time without --at.
    CHECK(run(drill_on(scratch, "list")) == e4(1000) + nf3);
    DrillOptions review = drill_on(scratch, "review");
    review.card = 1;
    review.grade = "good";
    review.at = 2000;
    CHECK(run(review) == "card 1: repetitions 1 interval 1 ease 2500 due 88400\n");
    list.at = 2000;
    CHECK(run(list) == nf3);
    list.at = 88400;
    CHECK(run(list) == nf3 + e4(88400));

    review.grade = "meh";
    CHECK(run(review) == "error: grade \"meh\" is not again, hard, good or easy");
    review.grade = "good";
    review.colour = "black";
    CHECK(run(review) == "error: no card 1 in Default's black repertoire");
    list.profile = "Nobody";
    CHECK(run(list).starts_with("error: no profile named \"Nobody\" in "));
    list.profile = "Default";
    list.colour = "green";
    CHECK(run(list) == "error: colour \"green\" is neither white nor black");
}

TEST_CASE("profile lists, creates and removes profiles, and exports and imports them",
          "[cli][profile]") {
    const Scratch scratch("profiles");
    const std::string data = scratch.data();
    CHECK(run(profile_on(data, "list")) == "1 Default tier 2 (1400-1599)\n");
    ProfileOptions create = profile_on(data, "create");
    create.name = "Club";
    create.tier = 4;
    CHECK(run(create) == "created: 2 Club tier 4\n");
    CHECK(run(create) == "error: profile \"Club\": profiles.db already has a profile of that name");
    create.name = "Seven";
    create.tier = 7;
    CHECK(run(create) == "error: tier 7 is not one of 0 to 6");
    RepertoireOptions club = repertoire_on(scratch, "add");
    club.profile = "Club";
    club.moves = "e4";
    REQUIRE(run(club) == "added: 1\nchanged: 0\nunchanged: 0\n");

    const std::string exported = scratch.file("export.db").string();
    ProfileOptions nothing = profile_on(scratch.file("empty").string(), "export");
    nothing.path = exported;
    CHECK(run(nothing) ==
          "error: " + scratch.file("empty").string() + " has no profiles.db to export");
    CHECK_FALSE(std::filesystem::exists(scratch.file("empty")));
    ProfileOptions to = profile_on(data, "export");
    to.path = exported;
    CHECK(run(to) == "exported: " + exported + "\n");
    CHECK(run(to) == "error: " + exported + " exists or cannot be written");

    // Imported into another data directory, whose own file is kept as profiles.db.previous.
    const std::string other = scratch.file("other").string();
    CHECK(run(profile_on(other, "list")) == "1 Default tier 2 (1400-1599)\n");
    ProfileOptions from = profile_on(other, "import");
    from.path = exported;
    CHECK(run(from) == "imported: " + exported + "\n");
    CHECK(std::filesystem::exists(std::filesystem::path(other) / "profiles.db.previous"));
    CHECK(run(profile_on(other, "list")) ==
          "1 Default tier 2 (1400-1599)\n2 Club tier 4 (1800-1999)\n");
    RepertoireOptions imported = repertoire_on(scratch, "list");
    imported.data_dir = other;
    imported.profile = "Club";
    CHECK(run(imported) == listed("", "e2e4", "e4"));

    const std::string notes = scratch.file("notes.txt").string();
    write_text(notes, "not a database\n");
    from.path = notes;
    CHECK(run(from) == "error: " + notes + " is not a profiles.db");

    ProfileOptions remove = profile_on(data, "remove");
    remove.name = "Club";
    CHECK(run(remove) == "removed: Club\n");
    CHECK(run(remove) == "error: no profile named \"Club\"");
    remove.name = "Default";
    CHECK(run(remove) == "error: profile \"Default\": profiles.db keeps its last profile");
    CHECK(run(profile_on(data, "list")) == "1 Default tier 2 (1400-1599)\n");
}

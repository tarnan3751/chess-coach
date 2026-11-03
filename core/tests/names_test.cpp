// Unit tests for chesscoach/names.hpp (core; unit): the five chess-openings files committed under
// data/chess-openings/ load by docs/database.md §10's rule, every line replaying, the first line
// to reach a position naming it; and a missing or misshapen file is refused.
#include <chesscoach/names.hpp>
#include <chesscoach/position.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string_view>

using chesscoach::Key;
using chesscoach::load_names;
using chesscoach::NameErrorKind;
using chesscoach::Opening;
using chesscoach::Position;

namespace {

std::filesystem::path tsv_dir() {
    return std::filesystem::path(CHESSCOACH_DATA_DIR) / "chess-openings";
}

Key key_after(std::initializer_list<std::string_view> moves) {
    auto position = Position::start();
    for (const auto san : moves) {
        REQUIRE(position.play_san(san).has_value());
    }
    return position.key();
}

// A scratch directory of its own for each test, under the build directory (so two lanes or two
// presets never share one), removed when the test ends.
class ScratchDir {
  public:
    explicit ScratchDir(std::string_view name)
        : path_(std::filesystem::path(CHESSCOACH_TEST_SCRATCH_DIR) / name) {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    ScratchDir(ScratchDir&&) = delete;
    ScratchDir& operator=(ScratchDir&&) = delete;
    ~ScratchDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void write(const std::filesystem::path& file, std::string_view text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}

} // namespace

TEST_CASE("The five chess-openings files load with every line replaying", "[names]") {
    const auto table = load_names(tsv_dir());
    REQUIRE(table.has_value());
    CHECK(table->failures() == 0);
    CHECK(table->lines() == 3865);
    CHECK(table->size() == 3865);
    CHECK(table->collisions() == 0); // the files name each position once
    CHECK(table->size() + table->collisions() + table->failures() == table->lines());
}

TEST_CASE("A line names the position its moves reach", "[names]") {
    const auto table = load_names(tsv_dir());
    REQUIRE(table.has_value());

    const auto* sicilian = table->find(key_after({"e4", "c5"}));
    REQUIRE(sicilian != nullptr);
    CHECK(*sicilian == Opening{.eco = "B20", .name = "Sicilian Defense"});

    const auto* amar = table->find(key_after({"Nh3"}));
    REQUIRE(amar != nullptr);
    CHECK(*amar == Opening{.eco = "A00", .name = "Amar Opening"});

    // Castling replays from the TSV's O-O.
    const auto* gent = table->find(
        key_after({"Nh3", "d5", "g3", "e5", "f4", "Bxh3", "Bxh3", "exf4", "O-O", "fxg3", "hxg3"}));
    REQUIRE(gent != nullptr);
    CHECK(gent->name == "Amar Opening: Paris Gambit, Gent Gambit");

    // The start position and a position no line ends on have no name.
    CHECK(table->find(Position::start().key()) == nullptr);
    CHECK(table->find(key_after({"h4", "h5", "Rh3"})) == nullptr);
}

TEST_CASE("Names match by position, whatever the move order", "[names]") {
    const auto table = load_names(tsv_dir());
    REQUIRE(table.has_value());
    const auto* one = table->find(key_after({"e4", "e5", "Nf3", "Nc6"}));
    const auto* other = table->find(key_after({"Nf3", "Nc6", "e4", "e5"}));
    REQUIRE(one != nullptr);
    CHECK(one == other);
}

TEST_CASE("The first line to reach a position wins, and later ones are collisions", "[names]") {
    const ScratchDir dir("names-first-wins");
    write(dir.path() / "a.tsv", "eco\tname\tpgn\n"
                                "A00\tFirst\t1. e4 e5 2. Nf3 Nc6\n"
                                "A00\tSecond\t1. Nf3 Nc6 2. e4 e5\n"
                                "A01\tBroken\t1. e4 e5 2. Ke3\n"
                                "A02\tNo moves\t\n"
                                "A03\tFour\tfields\t1. e4\n"
                                "A04\tNot SAN\t1. e2e4\n");
    write(dir.path() / "b.tsv", "eco\tname\tpgn\n"
                                "B00\tThird\t1. e4 e5 2. Nf3 Nc6\n");
    for (const std::string_view file : {"c.tsv", "d.tsv", "e.tsv"}) {
        write(dir.path() / file, "eco\tname\tpgn\n");
    }
    const auto table = load_names(dir.path());
    REQUIRE(table.has_value());
    CHECK(table->lines() == 7);
    CHECK(table->size() == 2); // First, and No moves, which names the start position
    CHECK(table->collisions() == 2);
    CHECK(table->failures() == 3);
    const auto* first = table->find(key_after({"e4", "e5", "Nf3", "Nc6"}));
    REQUIRE(first != nullptr);
    CHECK(*first == Opening{.eco = "A00", .name = "First"});
}

TEST_CASE("load_names refuses a missing file or a wrong header", "[names]") {
    const ScratchDir dir("names-refusals");
    auto missing = load_names(dir.path());
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().kind == NameErrorKind::missing_file);
    CHECK(missing.error().file == "a.tsv");

    for (const std::string_view file : {"a.tsv", "b.tsv", "c.tsv", "d.tsv"}) {
        write(dir.path() / file, "eco\tname\tpgn\n");
    }
    missing = load_names(dir.path());
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().kind == NameErrorKind::missing_file);
    CHECK(missing.error().file == "e.tsv");

    write(dir.path() / "e.tsv", "eco,name,pgn\n");
    const auto header = load_names(dir.path());
    REQUIRE_FALSE(header.has_value());
    CHECK(header.error().kind == NameErrorKind::bad_header);
    CHECK(header.error().file == "e.tsv");

    write(dir.path() / "e.tsv", "");
    const auto empty = load_names(dir.path());
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().kind == NameErrorKind::unreadable);

    CHECK(load_names(dir.path() / "nowhere").error().kind == NameErrorKind::missing_file);
}

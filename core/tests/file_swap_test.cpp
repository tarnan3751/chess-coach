// Unit tests for chesscoach/file_swap.hpp (core; unit): the swap both imports end with, on files
// in a scratch directory: a first swap and a replacing one, with previous a hard link to the file
// it was; the copy fallback, forced by its parameter; a read-only previous and a symlink at
// previous, which are replaced and never written through; and a final rename that fails, a
// directory in the staged file's place, leaving current as it was and no staging file behind.
#include <chesscoach/file_swap.hpp>

#include "scratch_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

using chesscoach::KeepPrevious;
using chesscoach::replace_keeping_previous;
using chesscoach::testing::ScratchDir;

namespace {

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string read_text(const std::filesystem::path& path) {
    const std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

TEST_CASE("A swap puts the staged file in place and keeps the one it replaces", "[swap]") {
    const ScratchDir scratch("file_swap", "success");
    const auto staged = scratch.path("current.db.importing");
    const auto current = scratch.path("current.db");
    const auto previous = scratch.path("current.db.previous");
    // Nothing to keep the first time.
    write_text(staged, "first");
    REQUIRE(replace_keeping_previous(staged, current, previous));
    CHECK(read_text(current) == "first");
    CHECK_FALSE(std::filesystem::exists(staged));
    CHECK_FALSE(std::filesystem::exists(previous));
    // Then the file replaced is kept as a hard link: the same file as a witness linked to it
    // before the swap, not a copy of it.
    write_text(staged, "second");
    const auto witness = scratch.path("witness");
    std::filesystem::create_hard_link(current, witness);
    REQUIRE(replace_keeping_previous(staged, current, previous));
    CHECK(read_text(current) == "second");
    CHECK(read_text(previous) == "first");
    CHECK(std::filesystem::equivalent(previous, witness));
    CHECK_FALSE(std::filesystem::exists(staged));
}

TEST_CASE("Without a hard link, the file replaced is kept as a complete copy", "[swap]") {
    const ScratchDir scratch("file_swap", "copy");
    const auto staged = scratch.path("current.db.importing");
    const auto current = scratch.path("current.db");
    const auto previous = scratch.path("current.db.previous");
    write_text(current, "old");
    write_text(previous, "older");
    write_text(staged, "new");
    const auto witness = scratch.path("witness");
    std::filesystem::create_hard_link(current, witness);
    REQUIRE(replace_keeping_previous(staged, current, previous, KeepPrevious::copy));
    CHECK(read_text(current) == "new");
    CHECK(read_text(previous) == "old");
    CHECK_FALSE(std::filesystem::equivalent(previous, witness));
    CHECK_FALSE(std::filesystem::exists(staged));
    CHECK_FALSE(std::filesystem::exists(scratch.path("current.db.previous.importing")));
}

TEST_CASE("A read-only previous or a symlink there is replaced, never written through", "[swap]") {
    const ScratchDir scratch("file_swap", "previous");
    const auto staged = scratch.path("current.db.importing");
    const auto current = scratch.path("current.db");
    const auto previous = scratch.path("current.db.previous");
    for (const KeepPrevious keep : {KeepPrevious::link_else_copy, KeepPrevious::copy}) {
        // A read-only previous, as a read-only file it was made from leaves.
        write_text(current, "old");
        write_text(previous, "older");
        std::filesystem::permissions(previous, std::filesystem::perms::owner_read,
                                     std::filesystem::perm_options::replace);
        write_text(staged, "new");
        REQUIRE(replace_keeping_previous(staged, current, previous, keep));
        CHECK(read_text(current) == "new");
        CHECK(read_text(previous) == "old");
        std::filesystem::permissions(previous, std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add);
        // A symlink at previous to a file elsewhere: the link goes, the file it names stays.
        const auto elsewhere = scratch.path("elsewhere.txt");
        write_text(elsewhere, "not mine");
        std::filesystem::remove(previous);
        std::filesystem::create_symlink(elsewhere, previous);
        write_text(staged, "newer");
        REQUIRE(replace_keeping_previous(staged, current, previous, keep));
        CHECK(read_text(current) == "newer");
        CHECK_FALSE(std::filesystem::is_symlink(previous));
        CHECK(read_text(previous) == "new");
        CHECK(read_text(elsewhere) == "not mine");
    }
}

TEST_CASE("A final rename that fails leaves current as it was and no staging file", "[swap]") {
    const ScratchDir scratch("file_swap", "fails");
    const auto staged = scratch.path("current.db.importing");
    const auto current = scratch.path("current.db");
    const auto previous = scratch.path("current.db.previous");
    for (const KeepPrevious keep : {KeepPrevious::link_else_copy, KeepPrevious::copy}) {
        write_text(current, "old");
        // A directory in the staged file's place: no rename puts it onto a file.
        std::filesystem::create_directories(staged);
        CHECK_FALSE(replace_keeping_previous(staged, current, previous, keep));
        CHECK(read_text(current) == "old");
        CHECK_FALSE(std::filesystem::exists(staged));
        CHECK_FALSE(std::filesystem::exists(scratch.path("current.db.previous.importing")));
    }
}

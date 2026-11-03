// cli_write_fixture <path>: writes the CLI tests' fixture database (fixture.hpp) to a file that
// does not exist yet, for driving chess-coach-cli and the golden runner by hand. Not a test.
#include "fixture.hpp"

#include <fmt/format.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <span>

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() != 2) {
        fmt::print(stderr, "usage: cli_write_fixture <path>\n");
        return 2;
    }
    try {
        const std::filesystem::path path(args.back());
        if (std::filesystem::exists(path)) {
            fmt::print(stderr, "cli_write_fixture: {} exists\n", path.string());
            return 2;
        }
        chesscoach::cli::test::write_fixture_db(path);
        return 0;
    } catch (const std::exception& error) {
        fmt::print(stderr, "cli_write_fixture: {}\n", error.what());
        return 1;
    }
}

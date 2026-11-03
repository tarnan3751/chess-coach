// chess-coach-cli: the entry point. Phase 0 parses the command line only: --version, --help,
// and the help when run without arguments. An exception that reaches main is reported on stderr,
// with exit status 1.
#include <chesscoach/version.hpp>

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include <cstdio>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    try {
        CLI::App app{"Queries the opening database and the engine from the terminal.",
                     "chess-coach-cli"};
        app.set_version_flag("--version", "chess-coach-cli " +
                                              chesscoach::to_string(chesscoach::current_version()));
        argv = app.ensure_utf8(argv);
        if (argc <= 1) {
            fmt::print("{}", app.help());
            return 0;
        }
        CLI11_PARSE(app, argc, argv);
        return 0;
    } catch (const std::exception& error) {
        fmt::print(stderr, "chess-coach-cli: {}\n", error.what());
        return 1;
    }
}

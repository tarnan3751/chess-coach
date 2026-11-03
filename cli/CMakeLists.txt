# cli: chess-coach-cli, the terminal front end over core's queries (docs/reference/cli.md). The
# commands are a library the executable and the tests share; main.cpp only wires them to the
# command line. Links core and never Qt: chesscoach_assert_no_qt fails the configure when the
# link closure of a target here reaches Qt.
add_library(cli_commands STATIC
  src/commands/common.cpp
  src/commands/common.hpp
  src/commands/dump.cpp
  src/commands/dump.hpp
  src/commands/explore.cpp
  src/commands/explore.hpp
  src/commands/name.cpp
  src/commands/name.hpp
  src/commands/stats.cpp
  src/commands/stats.hpp)
add_library(chesscoach::cli_commands ALIAS cli_commands)
target_include_directories(cli_commands PUBLIC src)
target_link_libraries(cli_commands PUBLIC chesscoach::core PRIVATE fmt::fmt chesscoach::warnings)
chesscoach_assert_no_qt(cli_commands)

add_executable(cli
  src/main.cpp)
set_target_properties(cli PROPERTIES OUTPUT_NAME chess-coach-cli)
target_link_libraries(cli PRIVATE chesscoach::cli_commands CLI11::CLI11 chesscoach::warnings)
chesscoach_assert_no_qt(cli)

if(BUILD_TESTING)
  find_package(SQLiteCpp CONFIG REQUIRED)
  add_library(cli_test_fixture STATIC
    tests/fixture.cpp
    tests/fixture.hpp)
  target_include_directories(cli_test_fixture PUBLIC tests)
  target_link_libraries(cli_test_fixture PUBLIC chesscoach::core PRIVATE SQLiteCpp
                                                                          chesscoach::warnings)
  chesscoach_assert_no_qt(cli_test_fixture)

  chesscoach_add_test(cli_commands_test MODULE cli KIND unit
                      SOURCES tests/commands_test.cpp
                      LIBS chesscoach::cli_commands cli_test_fixture fmt::fmt)
  chesscoach_assert_no_qt(cli_commands_test)

  # Writes the fixture database for driving the CLI and the golden runner by hand; not a test.
  add_executable(cli_write_fixture tests/write_fixture.cpp)
  target_link_libraries(cli_write_fixture PRIVATE cli_test_fixture fmt::fmt chesscoach::warnings)
  chesscoach_assert_no_qt(cli_write_fixture)

  add_test(NAME cli_exit_status
           COMMAND "${CMAKE_COMMAND}" "-DCLI=$<TARGET_FILE:cli>"
                   "-DMISSING_DB=${CMAKE_CURRENT_BINARY_DIR}/no-such-openings.db"
                   -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/exit_status.cmake")
  set_tests_properties(cli_exit_status PROPERTIES LABELS "cli;unit")
endif()

# cli: chess-coach-cli, the terminal front end over core's queries (CLAUDE.md, **Where things
# live**). Links core and never Qt: chesscoach_assert_no_qt fails the configure when its link
# closure reaches Qt.
add_executable(cli
  src/main.cpp)
set_target_properties(cli PROPERTIES OUTPUT_NAME chess-coach-cli)
target_link_libraries(cli PRIVATE chesscoach::core CLI11::CLI11 chesscoach::warnings)
chesscoach_assert_no_qt(cli)

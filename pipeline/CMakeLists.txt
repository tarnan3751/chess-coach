# pipeline: chess-coach-pipeline, which builds the tiered opening database from the Lichess
# dumps (CLAUDE.md, **pipeline/ streams and never stores**). Links core and never Qt:
# chesscoach_assert_no_qt fails the configure when its link closure reaches Qt.
add_executable(pipeline
  src/main.cpp)
set_target_properties(pipeline PROPERTIES OUTPUT_NAME chess-coach-pipeline)
target_link_libraries(pipeline PRIVATE chesscoach::core CLI11::CLI11 chesscoach::warnings)
chesscoach_assert_no_qt(pipeline)

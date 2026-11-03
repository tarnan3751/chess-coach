# pipeline: chess-coach-pipeline, which builds the tiered opening database from the Lichess
# dumps (CLAUDE.md, **pipeline/ streams and never stores**; docs/reference/pipeline.md). Links
# core and never Qt: chesscoach_assert_no_qt fails the configure when a link closure here
# reaches Qt. pipeline_lib holds every source but main.cpp, so the tests link what the
# executable runs. chess-library's header comes in as a system include for the PGN parser
# (game_scanner.cpp), found by core's find_path.
find_package(zstd CONFIG REQUIRED)

add_library(pipeline_lib STATIC
  src/aggregate.cpp
  src/aggregate.hpp
  src/chunker.cpp
  src/chunker.hpp
  src/game_filter.cpp
  src/game_filter.hpp
  src/game_scanner.cpp
  src/game_scanner.hpp
  src/move_code.cpp
  src/move_code.hpp
  src/sketch.cpp
  src/sketch.hpp
  src/work_pool.hpp
  src/zstd_istream.cpp
  src/zstd_istream.hpp)
target_include_directories(pipeline_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(pipeline_lib SYSTEM PRIVATE "${CHESSCOACH_CHESS_LIBRARY_INCLUDE_DIR}")
target_link_libraries(pipeline_lib
  PUBLIC chesscoach::core zstd::libzstd
  PRIVATE chesscoach::warnings)
chesscoach_assert_no_qt(pipeline_lib)

add_executable(pipeline
  src/main.cpp)
set_target_properties(pipeline PROPERTIES OUTPUT_NAME chess-coach-pipeline)
target_link_libraries(pipeline PRIVATE chesscoach::core CLI11::CLI11 chesscoach::warnings)
chesscoach_assert_no_qt(pipeline)

if(BUILD_TESTING)
  set(pipeline_test_definitions
      CHESSCOACH_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-scratch")
  chesscoach_add_test(pipeline_unit_test MODULE pipeline KIND unit
                      SOURCES tests/chunker_test.cpp
                              tests/game_filter_test.cpp
                              tests/game_scanner_test.cpp
                              tests/move_code_test.cpp
                              tests/sketch_test.cpp
                              tests/test_files.hpp
                              tests/work_pool_test.cpp
                              tests/zstd_istream_test.cpp
                      LIBS pipeline_lib)
  target_compile_definitions(pipeline_unit_test PRIVATE ${pipeline_test_definitions})
  chesscoach_add_test(pipeline_property_test MODULE pipeline KIND property
                      SOURCES tests/chunker_property_test.cpp
                              tests/sketch_property_test.cpp
                      LIBS pipeline_lib rapidcheck)
  chesscoach_assert_no_qt(pipeline_unit_test)
  chesscoach_assert_no_qt(pipeline_property_test)
endif()

# core: the library pipeline, cli and app link (CLAUDE.md, **Architecture rules**). Exact,
# deterministic, no I/O in the pure parts, and no Qt: chesscoach_assert_no_qt fails the configure
# when the link closure of a target here reaches Qt.
configure_file("${PROJECT_SOURCE_DIR}/cmake/version_info.hpp.in"
               "${PROJECT_BINARY_DIR}/generated/chesscoach/version_info.hpp" @ONLY)

add_library(core STATIC
  include/chesscoach/badges.hpp
  include/chesscoach/engine.hpp
  include/chesscoach/key.hpp
  include/chesscoach/names.hpp
  include/chesscoach/opening_schema.hpp
  include/chesscoach/opening_store.hpp
  include/chesscoach/position.hpp
  include/chesscoach/profile_store.hpp
  include/chesscoach/random.hpp
  include/chesscoach/sampler.hpp
  include/chesscoach/score.hpp
  include/chesscoach/spar.hpp
  include/chesscoach/tier.hpp
  include/chesscoach/version.hpp
  src/board_key.cpp
  src/board_key.hpp
  src/engine.cpp
  src/names.cpp
  src/opening_schema.cpp
  src/opening_store.cpp
  src/position.cpp
  src/profile_store.cpp
  src/sampler.cpp
  src/score.cpp
  src/spar.cpp
  src/tier.cpp
  src/version.cpp
  "${PROJECT_BINARY_DIR}/generated/chesscoach/version_info.hpp")
add_library(chesscoach::core ALIAS core)
target_include_directories(core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
                                       "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>")
find_package(SQLiteCpp CONFIG REQUIRED)
find_package(reproc++ CONFIG REQUIRED)
target_link_libraries(core PUBLIC fmt::fmt tl::expected
                           PRIVATE SQLiteCpp reproc++ chesscoach::warnings)
# chess-library is one header that warns under our flags, so it comes in as a system include, and
# only from core/src (never from a public header) and the one test that uses it as an oracle.
find_path(CHESSCOACH_CHESS_LIBRARY_INCLUDE_DIR chess-library/chess.hpp REQUIRED)
target_include_directories(core SYSTEM PRIVATE "${CHESSCOACH_CHESS_LIBRARY_INCLUDE_DIR}")
chesscoach_assert_no_qt(core)

if(BUILD_TESTING)
  # fake-uci: the scripted UCI engine core_engine_test drives; an executable, not a test.
  add_executable(fake-uci tests/fake_uci.cpp)
  target_link_libraries(fake-uci PRIVATE chesscoach::core chesscoach::warnings)
  chesscoach_assert_no_qt(fake-uci)

  chesscoach_add_test(core_badges_test MODULE core KIND unit
                      SOURCES tests/badges_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_engine_test MODULE core KIND unit
                      SOURCES tests/engine_test.cpp
                      LIBS chesscoach::core)
  add_dependencies(core_engine_test fake-uci)
  target_compile_definitions(core_engine_test PRIVATE
                             CHESSCOACH_FAKE_UCI="$<TARGET_FILE:fake-uci>"
                             CHESSCOACH_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")
  chesscoach_add_test(core_engine_stockfish_test MODULE core KIND engine
                      SOURCES tests/engine_stockfish_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_key_test MODULE core KIND unit
                      SOURCES tests/key_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_key_property_test MODULE core KIND property
                      SOURCES tests/key_property_test.cpp tests/random_games.hpp
                      LIBS chesscoach::core rapidcheck)
  # The library's own hash is this test's oracle.
  target_include_directories(core_key_property_test SYSTEM PRIVATE
                             "${CHESSCOACH_CHESS_LIBRARY_INCLUDE_DIR}")
  chesscoach_add_test(core_names_test MODULE core KIND unit
                      SOURCES tests/names_test.cpp
                      LIBS chesscoach::core)
  target_compile_definitions(core_names_test PRIVATE
                             CHESSCOACH_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
                             CHESSCOACH_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")
  chesscoach_add_test(core_opening_store_test MODULE core KIND unit
                      SOURCES tests/opening_store_test.cpp
                      LIBS chesscoach::core SQLiteCpp)
  target_compile_definitions(core_opening_store_test PRIVATE
                             CHESSCOACH_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")
  chesscoach_add_test(core_position_test MODULE core KIND unit
                      SOURCES tests/position_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_position_property_test MODULE core KIND property
                      SOURCES tests/position_property_test.cpp tests/random_games.hpp
                      LIBS chesscoach::core rapidcheck)
  chesscoach_add_test(core_profile_store_test MODULE core KIND unit
                      SOURCES tests/profile_store_test.cpp
                      LIBS chesscoach::core SQLiteCpp)
  target_compile_definitions(core_profile_store_test PRIVATE
                             CHESSCOACH_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
                             CHESSCOACH_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")
  chesscoach_add_test(core_random_test MODULE core KIND unit
                      SOURCES tests/random_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_sampler_test MODULE core KIND unit
                      SOURCES tests/sampler_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_sampler_property_test MODULE core KIND property
                      SOURCES tests/sampler_property_test.cpp
                      LIBS chesscoach::core rapidcheck)
  chesscoach_add_test(core_score_test MODULE core KIND unit
                      SOURCES tests/score_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_score_property_test MODULE core KIND property
                      SOURCES tests/score_property_test.cpp
                      LIBS chesscoach::core rapidcheck)
  chesscoach_add_test(core_spar_test MODULE core KIND unit
                      SOURCES tests/spar_test.cpp tests/spar_fixture.hpp
                      LIBS chesscoach::core SQLiteCpp)
  target_compile_definitions(core_spar_test PRIVATE
                             CHESSCOACH_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")
  chesscoach_add_test(core_spar_property_test MODULE core KIND property
                      SOURCES tests/spar_property_test.cpp tests/spar_fixture.hpp
                      LIBS chesscoach::core SQLiteCpp rapidcheck)
  target_compile_definitions(core_spar_property_test PRIVATE
                             CHESSCOACH_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")
  chesscoach_add_test(core_tier_test MODULE core KIND unit
                      SOURCES tests/tier_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_version_test MODULE core KIND unit
                      SOURCES tests/version_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_version_property_test MODULE core KIND property
                      SOURCES tests/version_property_test.cpp
                      LIBS chesscoach::core rapidcheck)
  chesscoach_assert_no_qt(core_badges_test)
  chesscoach_assert_no_qt(core_engine_test)
  chesscoach_assert_no_qt(core_engine_stockfish_test)
  chesscoach_assert_no_qt(core_key_test)
  chesscoach_assert_no_qt(core_key_property_test)
  chesscoach_assert_no_qt(core_names_test)
  chesscoach_assert_no_qt(core_opening_store_test)
  chesscoach_assert_no_qt(core_position_test)
  chesscoach_assert_no_qt(core_position_property_test)
  chesscoach_assert_no_qt(core_profile_store_test)
  chesscoach_assert_no_qt(core_random_test)
  chesscoach_assert_no_qt(core_sampler_test)
  chesscoach_assert_no_qt(core_sampler_property_test)
  chesscoach_assert_no_qt(core_score_test)
  chesscoach_assert_no_qt(core_score_property_test)
  chesscoach_assert_no_qt(core_spar_test)
  chesscoach_assert_no_qt(core_spar_property_test)
  chesscoach_assert_no_qt(core_tier_test)
  chesscoach_assert_no_qt(core_version_test)
  chesscoach_assert_no_qt(core_version_property_test)
endif()

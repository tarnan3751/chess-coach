# core: the library pipeline, cli and app link (CLAUDE.md, **Architecture rules**). Exact,
# deterministic, no I/O in the pure parts, and no Qt: chesscoach_assert_no_qt fails the configure
# when the link closure of a target here reaches Qt.
configure_file("${PROJECT_SOURCE_DIR}/cmake/version_info.hpp.in"
               "${PROJECT_BINARY_DIR}/generated/chesscoach/version_info.hpp" @ONLY)

add_library(core STATIC
  include/chesscoach/key.hpp
  include/chesscoach/position.hpp
  include/chesscoach/version.hpp
  src/board_key.cpp
  src/board_key.hpp
  src/position.cpp
  src/version.cpp
  "${PROJECT_BINARY_DIR}/generated/chesscoach/version_info.hpp")
add_library(chesscoach::core ALIAS core)
target_include_directories(core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
                                       "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>")
target_link_libraries(core PUBLIC fmt::fmt tl::expected PRIVATE chesscoach::warnings)
# chess-library is one header that warns under our flags, so it comes in as a system include, and
# only from core/src (never from a public header) and the one test that uses it as an oracle.
find_path(CHESSCOACH_CHESS_LIBRARY_INCLUDE_DIR chess-library/chess.hpp REQUIRED)
target_include_directories(core SYSTEM PRIVATE "${CHESSCOACH_CHESS_LIBRARY_INCLUDE_DIR}")
chesscoach_assert_no_qt(core)

if(BUILD_TESTING)
  chesscoach_add_test(core_key_test MODULE core KIND unit
                      SOURCES tests/key_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_key_property_test MODULE core KIND property
                      SOURCES tests/key_property_test.cpp tests/random_games.hpp
                      LIBS chesscoach::core rapidcheck)
  # The library's own hash is this test's oracle.
  target_include_directories(core_key_property_test SYSTEM PRIVATE
                             "${CHESSCOACH_CHESS_LIBRARY_INCLUDE_DIR}")
  chesscoach_add_test(core_position_test MODULE core KIND unit
                      SOURCES tests/position_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_position_property_test MODULE core KIND property
                      SOURCES tests/position_property_test.cpp tests/random_games.hpp
                      LIBS chesscoach::core rapidcheck)
  chesscoach_add_test(core_version_test MODULE core KIND unit
                      SOURCES tests/version_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_version_property_test MODULE core KIND property
                      SOURCES tests/version_property_test.cpp
                      LIBS chesscoach::core rapidcheck)
  chesscoach_assert_no_qt(core_key_test)
  chesscoach_assert_no_qt(core_key_property_test)
  chesscoach_assert_no_qt(core_position_test)
  chesscoach_assert_no_qt(core_position_property_test)
  chesscoach_assert_no_qt(core_version_test)
  chesscoach_assert_no_qt(core_version_property_test)
endif()

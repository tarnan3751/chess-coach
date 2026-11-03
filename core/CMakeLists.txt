# core: the library pipeline, cli and app link (CLAUDE.md, **Architecture rules**). Exact,
# deterministic, no I/O in the pure parts, and no Qt: chesscoach_assert_no_qt fails the configure
# when the link closure of a target here reaches Qt.
configure_file("${PROJECT_SOURCE_DIR}/cmake/version_info.hpp.in"
               "${PROJECT_BINARY_DIR}/generated/chesscoach/version_info.hpp" @ONLY)

add_library(core STATIC
  include/chesscoach/key.hpp
  include/chesscoach/version.hpp
  src/version.cpp
  "${PROJECT_BINARY_DIR}/generated/chesscoach/version_info.hpp")
add_library(chesscoach::core ALIAS core)
target_include_directories(core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
                                       "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>")
target_link_libraries(core PUBLIC fmt::fmt tl::expected PRIVATE chesscoach::warnings)
chesscoach_assert_no_qt(core)

if(BUILD_TESTING)
  chesscoach_add_test(core_key_test MODULE core KIND unit
                      SOURCES tests/key_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_version_test MODULE core KIND unit
                      SOURCES tests/version_test.cpp
                      LIBS chesscoach::core)
  chesscoach_add_test(core_version_property_test MODULE core KIND property
                      SOURCES tests/version_property_test.cpp
                      LIBS chesscoach::core rapidcheck)
  chesscoach_assert_no_qt(core_key_test)
  chesscoach_assert_no_qt(core_version_test)
  chesscoach_assert_no_qt(core_version_property_test)
endif()

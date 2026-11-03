# chess-coach: the root of the build. Four modules (core, pipeline, cli, app), each a target
# with an explicit source list; dependencies from vcpkg in manifest mode and Qt from the system
# (CLAUDE.md, **Stack**; docs/reference/testing-tools.md). Configure only through a preset:
#   cmake --preset dev && cmake --build --preset dev
cmake_minimum_required(VERSION 3.28)

# The vcpkg toolchain, chosen before project() so the manifest's ports install under
# build/<preset>/vcpkg_installed/: VCPKG_ROOT when set, else ~/vcpkg.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "The vcpkg toolchain")
  elseif(EXISTS "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "The vcpkg toolchain")
  else()
    message(FATAL_ERROR "vcpkg not found: set VCPKG_ROOT to a bootstrapped vcpkg clone, or "
                        "clone and bootstrap one at ~/vcpkg (README.md, **Building and running**)")
  endif()
endif()

# Qt comes from Homebrew on macOS (from apt on Linux, on the default prefix), never from vcpkg.
if(APPLE AND NOT DEFINED Qt6_DIR)
  execute_process(
    COMMAND brew --prefix qt
    OUTPUT_VARIABLE chesscoach_qt_prefix
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE chesscoach_brew_result)
  if(chesscoach_brew_result EQUAL 0 AND IS_DIRECTORY "${chesscoach_qt_prefix}")
    list(APPEND CMAKE_PREFIX_PATH "${chesscoach_qt_prefix}")
  endif()
endif()

# A sanitized build on macOS (the gate preset) compiles with Homebrew's LLVM clang when it is
# installed and no compiler was given: Apple clang 15's AddressSanitizer runtime crashes before
# main on macOS 26. Homebrew's llvm is found through `brew --prefix llvm`, or at the Apple
# silicon or the Intel prefix when brew is not on the PATH. The dev and release presets stay on
# Apple clang (docs/reference/testing-tools.md, **Presets**).
if(CHESS_COACH_SANITIZE
   AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin"
   AND NOT DEFINED CMAKE_CXX_COMPILER
   AND NOT DEFINED ENV{CXX})
  execute_process(
    COMMAND brew --prefix llvm
    OUTPUT_VARIABLE chesscoach_llvm_prefix
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE chesscoach_brew_result)
  if(NOT chesscoach_brew_result EQUAL 0)
    set(chesscoach_llvm_prefix "")
    foreach(prefix IN ITEMS /opt/homebrew/opt/llvm /usr/local/opt/llvm)
      if(EXISTS "${prefix}/bin/clang++")
        set(chesscoach_llvm_prefix "${prefix}")
        break()
      endif()
    endforeach()
  endif()
  if(chesscoach_llvm_prefix AND EXISTS "${chesscoach_llvm_prefix}/bin/clang++")
    set(CMAKE_CXX_COMPILER "${chesscoach_llvm_prefix}/bin/clang++")
    set(CMAKE_C_COMPILER "${chesscoach_llvm_prefix}/bin/clang")
    message(STATUS "CHESS_COACH_SANITIZE on macOS: compiling with ${CMAKE_CXX_COMPILER}, because "
                   "Apple clang 15's AddressSanitizer runtime crashes on macOS 26")
  endif()
endif()

project(chess-coach VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The tests, on by default. enable_testing() rather than include(CTest), whose dashboard targets
# and submit URL have no place in a build that never uses the network.
option(BUILD_TESTING "Build the tests" ON)
if(BUILD_TESTING)
  enable_testing()
endif()
include(cmake/warnings.cmake)
include(cmake/sanitizers.cmake)
include(cmake/boundary.cmake)

find_package(fmt CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
if(BUILD_TESTING)
  include(cmake/testing.cmake)
endif()

add_subdirectory(core)
add_subdirectory(pipeline)
add_subdirectory(cli)
add_subdirectory(app)

# The macOS package: cmake --build --preset release --target package (cmake/packaging.cmake).
if(APPLE)
  include(cmake/packaging.cmake)
endif()

if(BUILD_TESTING)
  # The module boundary and the no-network rule over the sources, the same patterns as
  # tools/gates.sh's boundary step.
  add_test(NAME boundary
           COMMAND "${CMAKE_COMMAND}" "-DCHESSCOACH_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                   -P "${PROJECT_SOURCE_DIR}/cmake/boundary_check.cmake")
  set_tests_properties(boundary PROPERTIES LABELS "cmake;unit")
  # Every test, Catch2 names with brackets included, carries a module and a kind label.
  add_test(NAME labels
           COMMAND "${CMAKE_COMMAND}" "-DCTEST=${CMAKE_CTEST_COMMAND}"
                   "-DBUILD_DIR=${PROJECT_BINARY_DIR}"
                   -P "${PROJECT_SOURCE_DIR}/cmake/labels_check.cmake")
  set_tests_properties(labels PROPERTIES LABELS "cmake;unit")
endif()

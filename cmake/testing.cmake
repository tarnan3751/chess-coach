# chesscoach_add_test: every test target of ours, labelled with its module and its kind so that
# ctest -L selects them (tools/run_tests.sh, tools/gates.sh; docs/reference/testing-tools.md,
# **Tests**).
#
#   chesscoach_add_test(<target> MODULE <core|pipeline|cli|app>
#                       KIND <unit|property|engine|ui|bench>
#                       SOURCES <source>... [LIBS <library>...])
#
# A Catch2 kind (every kind but ui) links Catch2WithMain, and catch_discover_tests registers
# each TEST_CASE as a ctest test labelled "<module>;<kind>"; a KIND property test runs with
# RC_PARAMS set to CHESSCOACH_RC_PARAMS (below), so each RapidCheck property checks two thousand
# cases from a fixed seed. KIND ui is a Qt Test executable: it
# links Qt6::Test, is one ctest test labelled "<module>;ui", and runs under the offscreen
# platform, so it needs no display. Every test target links chesscoach::warnings.
include_guard(GLOBAL)

# The RapidCheck configuration of every KIND property test: two thousand cases from a fixed seed,
# since every randomized test takes an explicit seed (PLAN.md §5.5), or the value of the
# environment variable CHESS_COACH_RC_PARAMS, verbatim, when it is set at configure time.
if(DEFINED ENV{CHESS_COACH_RC_PARAMS})
  set(CHESSCOACH_RC_PARAMS "$ENV{CHESS_COACH_RC_PARAMS}")
else()
  set(CHESSCOACH_RC_PARAMS "max_success=2000 seed=20261103")
endif()

find_package(Catch2 3 CONFIG REQUIRED)
find_package(rapidcheck CONFIG REQUIRED)
include(Catch)

function(chesscoach_add_test target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "MODULE;KIND" "SOURCES;LIBS")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "chesscoach_add_test(${target}): unexpected arguments ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT arg_MODULE IN_LIST CHESSCOACH_TEST_MODULES)
    message(FATAL_ERROR "chesscoach_add_test(${target}): MODULE must be one of ${CHESSCOACH_TEST_MODULES}")
  endif()
  if(NOT arg_KIND IN_LIST CHESSCOACH_TEST_KINDS)
    message(FATAL_ERROR "chesscoach_add_test(${target}): KIND must be one of ${CHESSCOACH_TEST_KINDS}")
  endif()
  if(NOT arg_SOURCES)
    message(FATAL_ERROR "chesscoach_add_test(${target}): no SOURCES")
  endif()

  add_executable(${target} ${arg_SOURCES})
  target_link_libraries(${target} PRIVATE chesscoach::warnings ${arg_LIBS})
  if(arg_KIND STREQUAL "ui")
    target_link_libraries(${target} PRIVATE Qt6::Test)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "${arg_MODULE};ui"
                                              ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  else()
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain)
    catch_discover_tests(${target} TEST_LIST ${target}_TESTS)
    # catch_discover_tests's PROPERTIES loses the semicolon of a list value (LABELS "a;b" arrives
    # as LABELS a b), so the labels are set by a script ctest includes after the discovered
    # tests, over the list of their names that catch_discover_tests defines.
    # A property test's RapidCheck configuration rides on the same script.
    set(labels_script "${CMAKE_CURRENT_BINARY_DIR}/${target}_labels.cmake")
    set(environment "")
    if(arg_KIND STREQUAL "property")
      set(environment " ENVIRONMENT \"RC_PARAMS=${CHESSCOACH_RC_PARAMS}\"")
    endif()
    file(WRITE "${labels_script}"
         "if(${target}_TESTS)\n"
         "  set_tests_properties(\${${target}_TESTS} PROPERTIES LABELS \"${arg_MODULE};${arg_KIND}\"${environment})\n"
         "endif()\n")
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${labels_script}")
  endif()
endfunction()

set(CHESSCOACH_TEST_MODULES core pipeline cli app)
set(CHESSCOACH_TEST_KINDS unit property engine ui bench)

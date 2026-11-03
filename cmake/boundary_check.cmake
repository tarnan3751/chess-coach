# The module boundary and the no-network rule over the sources (CLAUDE.md, **Module
# boundaries**): the ctest test "boundary" (labels cmake;unit), run as
#   cmake -DCHESSCOACH_SOURCE_DIR=<root> -P cmake/boundary_check.cmake
# It applies the patterns of tools/gates.sh's boundary step to the same files, line by line, and
# fails listing every match:
#   - no Qt include in a C++ source, and no Qt target in a CMakeLists.txt or *.cmake, under core/,
#     pipeline/ or cli/;
#   - no network include in a C++ source, and no network target in a CMakeLists.txt or *.cmake,
#     under core/, pipeline/, cli/, app/ or cmake/;
#   - no network port in vcpkg.json.
# CMake's regular expressions have no [[:space:]] class, so the patterns spell it [ \t]; the
# network names are written with one letter bracketed (lib[c]url), which matches the same text,
# so that this file, under cmake/, does not match the gate's own grep.
cmake_minimum_required(VERSION 3.28)

if(NOT CHESSCOACH_SOURCE_DIR)
  message(FATAL_ERROR "usage: cmake -DCHESSCOACH_SOURCE_DIR=<root> -P cmake/boundary_check.cmake")
endif()

set(space "[ \t]")
set(include_start "^${space}*#${space}*include${space}*[<\"]")
set(qt_include "${include_start}(Q[A-Z][A-Za-z0-9]*|Qt[A-Za-z]*/)")
set(qt_target "Qt6?::")
set(network_include "${include_start}(curl/|http[l]ib|boost/asio|boost/beast|sys/socket\\.h|netinet/|arpa/inet|winsock|QtNetwork/|QNetwork|QTcp|QUdp|QSsl)")
set(network_target "Qt6?::Network|CURL[:]:|lib[c]url|http[l]ib|Boost::[a]sio|Boost::[b]east|OpenSSL[:]:")
set(network_port "\"(curl|cpp-http[l]ib|boost-asio|boost-beast|openssl|qtnetwork|qtbase)\"")

set(sources "*.cpp" "*.hpp" "*.h")
set(build_files "CMakeLists.txt" "*.cmake")
set(findings "")

# scan(<regex> DIRS <dir>... NAMES <glob>...): appends "<file>: <line>" to findings for every
# line matching <regex> in a file named by one of the globs, in the directories and below them.
function(scan regex)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "DIRS;NAMES")
  set(files "")
  foreach(dir IN LISTS arg_DIRS)
    if(NOT IS_DIRECTORY "${CHESSCOACH_SOURCE_DIR}/${dir}")
      continue()
    endif()
    foreach(name IN LISTS arg_NAMES)
      file(GLOB_RECURSE found LIST_DIRECTORIES false "${CHESSCOACH_SOURCE_DIR}/${dir}/${name}")
      list(APPEND files ${found})
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES files)
  list(SORT files)
  foreach(file IN LISTS files)
    file(STRINGS "${file}" hits REGEX "${regex}")
    file(RELATIVE_PATH relative "${CHESSCOACH_SOURCE_DIR}" "${file}")
    foreach(hit IN LISTS hits)
      list(APPEND findings "${relative}: ${hit}")
    endforeach()
  endforeach()
  set(findings "${findings}" PARENT_SCOPE)
endfunction()

scan("${qt_include}" DIRS core pipeline cli NAMES ${sources})
scan("${qt_target}" DIRS core pipeline cli NAMES ${build_files})
scan("${network_include}" DIRS core pipeline cli app cmake NAMES ${sources})
scan("${network_target}" DIRS core pipeline cli app cmake NAMES ${build_files})
if(EXISTS "${CHESSCOACH_SOURCE_DIR}/vcpkg.json")
  file(STRINGS "${CHESSCOACH_SOURCE_DIR}/vcpkg.json" hits REGEX "${network_port}")
  foreach(hit IN LISTS hits)
    list(APPEND findings "vcpkg.json: ${hit}")
  endforeach()
endif()

if(findings)
  list(LENGTH findings count)
  list(JOIN findings "\n  " listing)
  message(FATAL_ERROR "boundary: ${count} reference(s) to Qt outside app/ or to a network "
                      "library (CLAUDE.md, **Module boundaries**):\n  ${listing}")
endif()
message(STATUS "boundary: no Qt in core, pipeline or cli; no network anywhere")

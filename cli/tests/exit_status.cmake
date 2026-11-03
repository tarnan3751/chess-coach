# The CLI's exit statuses (cli; unit), run as
#   cmake -DCLI=<chess-coach-cli> -DMISSING_DB=<path that does not exist> -P exit_status.cmake
# Every rejected command line and every command error prints exactly one line on stderr, nothing
# on stdout, and exits 2; --version, --help and no arguments exit 0. No case needs a database:
# the ones that open one name a file that does not exist.
cmake_minimum_required(VERSION 3.28)

if(NOT CLI OR NOT MISSING_DB)
  message(FATAL_ERROR "usage: cmake -DCLI=<chess-coach-cli> -DMISSING_DB=<path> -P exit_status.cmake")
endif()

set(failures "")

# expect(<status> <args>...): runs the CLI with the args and checks the status and the output.
function(expect status)
  execute_process(COMMAND "${CLI}" ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out
                  ERROR_VARIABLE err)
  list(JOIN ARGN " " shown)
  if(NOT result EQUAL status)
    list(APPEND failures "'${shown}': exit ${result}, expected ${status}")
  elseif(status EQUAL 2)
    string(REGEX MATCHALL "\n" ends "${err}")
    list(LENGTH ends lines)
    if(NOT lines EQUAL 1 OR NOT err MATCHES "^chess-coach-cli: [^\n]+\n$" OR NOT out STREQUAL "")
      list(APPEND failures "'${shown}': expected one line on stderr and nothing on stdout, got "
                           "stderr '${err}' and stdout '${out}'")
    endif()
  endif()
  set(failures "${failures}" PARENT_SCOPE)
endfunction()

expect(0)
expect(0 --version)
expect(0 explore --help)
expect(2 frobnicate)
expect(2 --frobnicate)
expect(2 dump)
expect(2 dump --db "${MISSING_DB}")
expect(2 stats --db "${MISSING_DB}")
expect(2 explore --tier 3)
expect(2 explore --db "${MISSING_DB}")
expect(2 explore --db "${MISSING_DB}" --tier 3)
expect(2 explore --db "${MISSING_DB}" --tier 7)
expect(2 explore --db "${MISSING_DB}" --tier three)
expect(2 explore --db "${MISSING_DB}" --tier 3 --fen "not a fen")
expect(2 explore --db "${MISSING_DB}" --tier 3 --moves "e4 e9")
expect(2 explore --db "${MISSING_DB}" --tier 3 --moves "e4" --fen "8/8/8/8/8/8/8/8 w - -")
expect(2 name --db "${MISSING_DB}")
expect(2 name --db "${MISSING_DB}" --moves "e4")
expect(2 name --db "${MISSING_DB}" --fen "not a fen")

if(failures)
  list(JOIN failures "\n  " listing)
  message(FATAL_ERROR "exit statuses:\n  ${listing}")
endif()

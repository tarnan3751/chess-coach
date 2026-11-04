# The pipeline's exit statuses (pipeline; unit), run as
#   cmake -DPIPELINE=<chess-coach-pipeline> -DSCRATCH=<directory> -P exit_status.cmake
# docs/reference/pipeline.md, **Running it** and **Trim**: --version, --help and no arguments exit
# 0; a command line CLI11 rejects, a refused option and an unreadable input exit 1. No case builds
# a database: each is refused before the first pass; no case of trim leaves a file behind.
cmake_minimum_required(VERSION 3.28)

if(NOT PIPELINE OR NOT SCRATCH)
  message(FATAL_ERROR "usage: cmake -DPIPELINE=<chess-coach-pipeline> -DSCRATCH=<dir> -P exit_status.cmake")
endif()

file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}")
set(month "${SCRATCH}/lichess_db_standard_rated_2013-01.pgn.zst")
file(WRITE "${month}" "not zstd")
set(misnamed "${SCRATCH}/games.pgn.zst")
file(WRITE "${misnamed}" "not zstd")
set(failures "")

# expect(<status> <args>...): runs the pipeline with the args and checks the status.
function(expect status)
  execute_process(COMMAND "${PIPELINE}" ${ARGN} RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
  list(JOIN ARGN " " shown)
  if(NOT result EQUAL status)
    list(APPEND failures "'${shown}': exit ${result}, expected ${status}")
  endif()
  set(failures "${failures}" PARENT_SCOPE)
endfunction()

expect(0)
expect(0 --version)
expect(0 build --help)
expect(1 frobnicate)
expect(1 build)
expect(1 build --in "${month}")
expect(1 build --in "${SCRATCH}/missing.pgn.zst" --out "${SCRATCH}/out.db")
expect(1 build --in "${month}" --out "${SCRATCH}/out.db" --min-games 0)
expect(1 build --in "${month}" --out "${SCRATCH}/out.db" --sketch-mib 3)
expect(1 build --in "${month}" --out "${SCRATCH}/out.db" --sketch-mib 32768)
expect(1 build --in "${misnamed}" --out "${SCRATCH}/out.db")
expect(1 build --in "${month}" --out "${SCRATCH}/out.db" --sketch-mib 1)
expect(0 trim --help)
expect(1 trim)
expect(1 trim --out-dir "${SCRATCH}/trimmed")
expect(1 trim --out-dir "${SCRATCH}/trimmed" "${SCRATCH}/missing.pgn.zst")
expect(1 trim --out-dir "${SCRATCH}/trimmed" "${misnamed}")
expect(1 trim --out-dir "${SCRATCH}/trimmed" --max-ply 1001 "${month}")
expect(1 trim --out-dir "${SCRATCH}" "${month}")
expect(1 trim --out-dir "${SCRATCH}/trimmed" "${month}")
if(EXISTS "${SCRATCH}/trimmed/lichess_db_standard_rated_2013-01.pgn.zst"
   OR EXISTS "${SCRATCH}/trimmed/lichess_db_standard_rated_2013-01.pgn.zst.partial")
  list(APPEND failures "trim left a file behind for an input that is not zstd")
endif()

if(failures)
  list(JOIN failures "\n  " listing)
  message(FATAL_ERROR "exit statuses:\n  ${listing}")
endif()

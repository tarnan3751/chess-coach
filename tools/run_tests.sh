#!/usr/bin/env bash
# Runs the test suite in the dev preset, configuring and building it first.
#   tools/run_tests.sh                       every test but the benchmarks
#   tools/run_tests.sh core                  one module's tests (the ctest label core)
#   tools/run_tests.sh core 'Zobrist'        one module, tests whose name matches a regex (ctest -R)
#   tools/run_tests.sh --bench               the benchmarks (the label bench), in the release preset
# Every test carries two ctest labels: its module (core, pipeline, cli, app) and its kind (unit,
# property, engine, ui, bench); the engine tests need a Stockfish binary (CHESS_COACH_STOCKFISH or
# stockfish on the PATH) and skip without one, which the full gate turns into a failure
# (docs/reference/testing-tools.md, **Tests**).
# Writes build/<preset>/ in the tree it runs in, so an agent runs it only in its clone.
# When the tests fail, ctest's logs of this run (LastTest.log, LastTestsFailed.log, with every
# failing test's output and a RapidCheck property's seed) are kept in
# build/gate-failures/<UTC timestamp>-<pid>/<preset>/, whose path it prints: report it, and the
# failing seed, before running anything again.
set -uo pipefail
cd "$(dirname "$0")/.."

if [ ! -f CMakeLists.txt ]; then
  echo "run_tests: no CMakeLists.txt yet (Phase 0 has not landed)" >&2
  exit 2
fi

# keep_failures <preset>: as in tools/gates.sh.
keep_failures() {
  local preset="$1" src dest
  src="build/$preset/Testing/Temporary"
  [ -s "$src/LastTestsFailed.log" ] || return 0
  dest="build/gate-failures/$(date -u +%Y%m%dT%H%M%SZ)-$$/$preset"
  mkdir -p "$dest" && cp "$src"/LastTest*.log "$dest/" 2>/dev/null
  echo "$dest"
}

preset=dev
selection=(-LE bench)
if [ "${1:-}" = "--bench" ]; then
  preset=release
  selection=(-L bench)
  shift
fi
module="${1:-}"
pattern="${2:-}"
if [ -n "$module" ]; then
  [ -f "$module/CMakeLists.txt" ] || { echo "run_tests: no module $module" >&2; exit 2; }
  selection+=(-L "^$module\$")
  [ -z "$pattern" ] || selection+=(-R "$pattern")
fi

cmake --preset "$preset" >/dev/null && cmake --build --preset "$preset" || exit $?
ctest --preset "$preset" "${selection[@]}" --output-on-failure
status=$?
if [ $status -ne 0 ]; then
  kept="$(keep_failures "$preset")"
  if [ -n "$kept" ]; then
    echo "run_tests: logs kept in $kept"
  fi
fi
exit $status

#!/usr/bin/env bash
# The gate: everything a change must pass before it is reported, reviewed or landed
# (CLAUDE.md, **Agent workflow**). Runs, in order:
#   1. layout      the four modules the map names exist (core pipeline cli app), each with a
#                  CMakeLists.txt, and the root CMakeLists.txt, CMakePresets.json and vcpkg.json
#   2. format      tools/format.sh --check: clang-format over every C++ source, no diff allowed
#   3. build       cmake --preset gate && cmake --build --preset gate   (every target compiled
#                  with warnings as errors, under ASan and UBSan)
#   4. test        ctest --preset gate -LE bench   (every test but the benchmarks: unit, property,
#                  engine against Stockfish (CHESS_COACH_STOCKFISH or stockfish on the PATH; the
#                  full gate fails when there is none), ui under the offscreen platform; the
#                  output of every failure)
#   5. bench       cmake --preset release && cmake --build --preset release && ctest --preset
#                  release -L bench   (the wall-clock budgets, after the tests and only when they
#                  passed, reported with the load average; the budgets assume an idle development
#                  machine, so a failure under load is first run again on an idle one)
#   6. tidy        run-clang-tidy over the gate build's compile_commands.json for core, pipeline,
#                  cli and app
#   7. boundary    no Qt include or Qt target under core/, pipeline/ or cli/; no network include,
#                  target or port anywhere
#   8. corpus      tools/run_corpus.sh: the sample dump through the pipeline and the golden CLI
#                  queries, checked against their expected output (skipped until Phase 1 lands it)
# The presets dev, gate and release (configure, build and test presets of those names) are
# defined in CMakePresets.json (docs/reference/testing-tools.md).
#
#   tools/gates.sh [--quick]     (or GATE_QUICK=1 tools/gates.sh)
#
# --quick is the per-commit profile: layout, format, every target compiled in the dev preset (no
# sanitizers), the unit tests only (ctest -L unit), and the boundary; it skips the property,
# engine and UI tests, the benchmarks, clang-tidy and the corpus, and its summary line says
# "(quick)". Without it the gate is the full one above, which reviews and landing run.
# Prints one line per gate, the failing gate's output in full, and exits non-zero when any gate
# fails. When the tests fail, ctest's logs of this run (LastTest.log and LastTestsFailed.log, which
# hold every failing test's output and a RapidCheck property's seed) are kept in
# build/gate-failures/<UTC timestamp>-<pid>/<preset>/, whose path the failure line names: report
# it, and the failing seed, before running anything again. Before Phase 0 lands there is no
# CMakeLists.txt; the gate says so and passes, so a docs-only series can land on the empty
# repository.
# Every configure and build writes build/<preset>/ in the tree it runs in, which is why the gate
# runs only in a lane's clone, never in the coordinator's main tree by an agent.
set -uo pipefail

cd "$(dirname "$0")/.."

quick="${GATE_QUICK:-0}"
for arg in "$@"; do
  case "$arg" in
    --quick) quick=1 ;;
    *) echo "usage: tools/gates.sh [--quick]" >&2; exit 2 ;;
  esac
done
[ "$quick" = 1 ] || quick=0
profile=""
[ "$quick" = 0 ] || profile=" (quick)"

if [ ! -f CMakeLists.txt ]; then
  echo "gate: no CMakeLists.txt yet (Phase 0 has not landed); nothing to check"
  echo "gates: all passed$profile"
  exit 0
fi

# Homebrew's llvm keg holds clang-tidy and run-clang-tidy without linking them into the PATH.
for p in /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin; do
  [ -d "$p" ] && PATH="$PATH:$p"
done
export PATH

scratch="$(mktemp -d "${TMPDIR:-/tmp}/chess-coach-gates.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT
failed=0

# keep_failures <preset>: copies ctest's logs of the preset's build directory (LastTest.log, with
# every test's output, and LastTestsFailed.log) into build/gate-failures/<UTC timestamp>-<pid>/
# <preset>/ at the tree root when a failing-tests log exists, so the next run, which rewrites
# them, cannot lose them; prints that directory, or nothing when there was nothing to keep.
# Nothing ever deletes these directories but removing build/.
keep_failures() {
  local preset="$1" src dest
  src="build/$preset/Testing/Temporary"
  [ -s "$src/LastTestsFailed.log" ] || return 0
  dest="build/gate-failures/$(date -u +%Y%m%dT%H%M%SZ)-$$/$preset"
  mkdir -p "$dest" && cp "$src"/LastTest*.log "$dest/" 2>/dev/null
  echo "$dest"
}

# report <name> <status> <detail> [<output-file>]: a status other than ok or skipped fails the gate
report() {
  local name="$1" status="$2" detail="$3" output="${4:-}"
  printf 'gate %-9s %s%s\n' "$name" "$status" "${detail:+ ($detail)}"
  if [ "$status" != "ok" ] && [ "$status" != "skipped" ]; then
    failed=1
    if [ -n "$output" ] && [ -s "$output" ]; then
      sed 's/^/    /' "$output" | tail -200
    fi
  fi
}

# test_count <ctest-output>: the N of ctest's "tests passed, M tests failed out of N" line.
test_count() {
  sed -nE 's/.*out of ([0-9]+).*/\1/p' "$1" | tail -1
}

# 1. Layout ---------------------------------------------------------------------------
missing=""
for m in core pipeline cli app; do
  [ -f "$m/CMakeLists.txt" ] || missing="$missing $m"
done
for f in CMakePresets.json vcpkg.json; do
  [ -f "$f" ] || missing="$missing $f"
done
if [ -z "$missing" ]; then
  report layout ok "core pipeline cli app"
else
  echo "missing:$missing" >"$scratch/layout.txt"
  report layout FAILED "missing:$missing" "$scratch/layout.txt"
fi

# 2. Format ---------------------------------------------------------------------------
if tools/format.sh --check >"$scratch/format.txt" 2>&1; then
  report format ok ""
else
  report format FAILED "run tools/format.sh" "$scratch/format.txt"
fi

# 3. Build ----------------------------------------------------------------------------
if [ "$quick" = 1 ]; then
  preset=dev
else
  preset=gate
fi
build_failed=0
if cmake --preset "$preset" >"$scratch/build.txt" 2>&1 && cmake --build --preset "$preset" >>"$scratch/build.txt" 2>&1; then
  report build ok "preset $preset"
else
  build_failed=1
  report build FAILED "preset $preset" "$scratch/build.txt"
fi

# 4. Tests ----------------------------------------------------------------------------
tests_failed=0
if [ $build_failed -ne 0 ]; then
  tests_failed=1
  report test skipped "the build failed"
else
  if [ "$quick" = 1 ]; then
    selection=(-L unit)
  else
    selection=(-LE bench)
  fi
  if ctest --preset "$preset" "${selection[@]}" --output-on-failure >"$scratch/test.txt" 2>&1; then
    engine_note=""
    if [ "$quick" = 0 ] && [ -z "${CHESS_COACH_STOCKFISH:-}" ] && ! command -v stockfish >/dev/null; then
      tests_failed=1
      report test FAILED "no Stockfish: set CHESS_COACH_STOCKFISH or install stockfish, so the engine tests can run" "$scratch/test.txt"
    else
      report test ok "$(test_count "$scratch/test.txt") tests"
    fi
  elif grep -q 'No tests were found' "$scratch/test.txt"; then
    report test ok "no tests match yet"
  else
    tests_failed=1
    kept="$(keep_failures "$preset")"
    report test FAILED "${kept:+logs kept in $kept}" "$scratch/test.txt"
  fi
fi

# 5. Benchmarks -----------------------------------------------------------------------
# load_average: the system's load averages over 1, 5 and 15 minutes, or "unknown" where the
# platform does not say.
load_average() {
  local l
  l="$(uptime 2>/dev/null | sed -nE 's/.*load averages?: *//p' | tr -s ', ' ' ')"
  echo "${l:-unknown}"
}
if [ "$quick" = 1 ]; then
  report bench skipped "quick profile"
elif [ $tests_failed -ne 0 ]; then
  report bench skipped "the tests did not pass"
else
  if cmake --preset release >"$scratch/bench.txt" 2>&1 && cmake --build --preset release >>"$scratch/bench.txt" 2>&1 \
    && ctest --preset release -L bench --output-on-failure >>"$scratch/bench.txt" 2>&1; then
    report bench ok "$(test_count "$scratch/bench.txt") benchmarks, load average $(load_average)"
  elif grep -q 'No tests were found' "$scratch/bench.txt"; then
    report bench ok "no benchmarks yet"
  else
    kept="$(keep_failures release)"
    report bench FAILED "load average $(load_average); the budgets assume an idle development machine, so run it again on one before reading a regression${kept:+; logs kept in $kept}" "$scratch/bench.txt"
  fi
fi

# 6. clang-tidy -----------------------------------------------------------------------
if [ "$quick" = 1 ]; then
  report tidy skipped "quick profile"
elif [ $build_failed -ne 0 ]; then
  report tidy skipped "the build failed"
elif ! command -v run-clang-tidy >/dev/null; then
  echo "run-clang-tidy not found on the PATH nor under Homebrew's llvm (brew install llvm)" >"$scratch/tidy.txt"
  report tidy FAILED "no run-clang-tidy" "$scratch/tidy.txt"
elif run-clang-tidy -p "build/$preset" -quiet "$PWD/(core|pipeline|cli|app)/" >"$scratch/tidy.txt" 2>&1; then
  report tidy ok ""
else
  report tidy FAILED "" "$scratch/tidy.txt"
fi

# 7. Module boundary and the no-network rule ---------------------------------------------
# core, pipeline and cli must not include or link Qt, and nothing may include or link a network
# library; the CMake build enforces this too, this is the belt to its braces and runs even when
# the build above failed.
: >"$scratch/boundary.txt"
for d in core pipeline cli; do
  [ -d "$d" ] || continue
  grep -rnE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](Q[A-Z][A-Za-z0-9]*|Qt[A-Za-z]*/)' "$d" \
    --include='*.cpp' --include='*.hpp' --include='*.h' >>"$scratch/boundary.txt" || true
  grep -rnE 'Qt6?::' "$d" --include='CMakeLists.txt' --include='*.cmake' >>"$scratch/boundary.txt" || true
done
for d in core pipeline cli app cmake; do
  [ -d "$d" ] || continue
  grep -rnE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](curl/|httplib|boost/asio|boost/beast|sys/socket\.h|netinet/|arpa/inet|winsock|QtNetwork/|QNetwork|QTcp|QUdp|QSsl)' "$d" \
    --include='*.cpp' --include='*.hpp' --include='*.h' >>"$scratch/boundary.txt" || true
  grep -rnE 'Qt6?::Network|CURL::|libcurl|httplib|Boost::asio|Boost::beast|OpenSSL::' "$d" \
    --include='CMakeLists.txt' --include='*.cmake' >>"$scratch/boundary.txt" || true
done
[ -f vcpkg.json ] && grep -nE '"(curl|cpp-httplib|boost-asio|boost-beast|openssl|qtnetwork|qtbase)"' vcpkg.json >>"$scratch/boundary.txt" || true
if [ ! -s "$scratch/boundary.txt" ]; then
  report boundary ok "no Qt in core, pipeline or cli; no network anywhere"
else
  report boundary FAILED "$(wc -l <"$scratch/boundary.txt" | tr -d ' ') reference(s)" "$scratch/boundary.txt"
fi

# 8. Golden corpora -------------------------------------------------------------------
if [ "$quick" = 1 ]; then
  report corpus skipped "quick profile"
elif [ ! -x tools/run_corpus.sh ]; then
  report corpus skipped "no runner yet (Phase 1)"
elif [ $build_failed -ne 0 ]; then
  report corpus skipped "the build failed"
elif tools/run_corpus.sh >"$scratch/corpus.txt" 2>&1; then
  report corpus ok "$(tail -1 "$scratch/corpus.txt")"
else
  report corpus FAILED "" "$scratch/corpus.txt"
fi

if [ $failed -eq 0 ]; then
  echo "gates: all passed$profile"
else
  echo "gates: FAILED$profile"
fi
exit $failed

#!/usr/bin/env bash
# The UI harness (docs/reference/app.md, **UI harness**): configures and builds the dev preset,
# runs the UI tests (ctest -L ui) under the offscreen platform, and lists the screenshots they
# saved in build/dev/ui-screenshots/, one <test>-<state>.png per state, removed before the run so
# that the list is this run's. The tests open no window on the display and touch no data
# directory: every window they build is given a scratch --data-dir path it never opens.
#
#   tools/ui_tests.sh            every UI test
#   tools/ui_tests.sh <regex>    the UI tests whose name matches (ctest -R)
#
# The full gate runs the same tests in the gate preset (tools/gates.sh). Writes build/dev/ in the
# tree it runs in, so an agent runs it only in its own clone. Exits with ctest's status.
set -uo pipefail
cd "$(dirname "$0")/.."

if [ ! -f CMakeLists.txt ]; then
  echo "ui_tests: no CMakeLists.txt" >&2
  exit 2
fi
[ $# -le 1 ] || { echo "usage: tools/ui_tests.sh [<regex>]" >&2; exit 2; }

selection=(-L ui)
[ -z "${1:-}" ] || selection+=(-R "$1")

screenshots="build/dev/ui-screenshots"
cmake --preset dev >/dev/null && cmake --build --preset dev || exit $?
rm -rf "$screenshots"
ctest --preset dev "${selection[@]}" --output-on-failure
status=$?

if [ -d "$screenshots" ] && [ -n "$(ls -A "$screenshots")" ]; then
  echo "ui_tests: screenshots in $screenshots:"
  ls -1 "$screenshots" | sed 's/^/  /'
else
  echo "ui_tests: no screenshots"
fi
exit $status

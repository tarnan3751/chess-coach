#!/usr/bin/env bash
# The CLI's golden queries (docs/reference/cli.md, **Golden queries**): runs chess-coach-cli once
# for every cli/tests/golden/<name>.args against the database given and compares its stdout,
# byte for byte, with <name>.out.
#
#   cli/tests/run_golden.sh [--update] <db> [<chess-coach-cli>]
#
# A .args file holds the command line without --db, one argument per line (so an argument may
# hold spaces, as --moves does); the runner appends --db <db>. When <name>.head exists it holds a
# line count N, and only the first N lines of the output are compared (the dump's head). The CLI
# defaults to build/dev/cli/chess-coach-cli. Every query must exit 0. --update rewrites each .out
# from the current output instead of comparing, for regenerating the expected output against a
# new database (the sample corpus's); review the diff before committing it.
# Prints the difference for every query that differs, then one summary line; exits 1 when any
# query differs or fails, 2 on a usage error. Writes nothing outside the system's temporary
# directory except, with --update, the .out files.
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"

usage() {
  echo "usage: cli/tests/run_golden.sh [--update] <db> [<chess-coach-cli>]" >&2
  exit 2
}

update=0
if [ "${1:-}" = "--update" ]; then
  update=1
  shift
fi
[ $# -ge 1 ] && [ $# -le 2 ] || usage
db="$1"
cli="${2:-$root/build/dev/cli/chess-coach-cli}"
[ -f "$db" ] || { echo "run_golden: no database $db" >&2; exit 2; }
[ -x "$cli" ] || { echo "run_golden: no executable $cli (build the dev preset first)" >&2; exit 2; }

scratch="$(mktemp -d "${TMPDIR:-/tmp}/chess-coach-golden.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT

queries=0
failed=0
for args_file in "$here"/golden/*.args; do
  [ -f "$args_file" ] || continue
  name="$(basename "$args_file" .args)"
  queries=$((queries + 1))
  args=()
  while IFS= read -r line || [ -n "$line" ]; do
    args+=("$line")
  done <"$args_file"
  actual="$scratch/$name.out"
  if ! "$cli" "${args[@]}" --db "$db" >"$scratch/$name.full" 2>"$scratch/$name.err"; then
    echo "golden $name: chess-coach-cli failed:"
    sed 's/^/    /' "$scratch/$name.err"
    failed=$((failed + 1))
    continue
  fi
  if [ -f "$here/golden/$name.head" ]; then
    head -n "$(tr -d '[:space:]' <"$here/golden/$name.head")" "$scratch/$name.full" >"$actual"
  else
    mv "$scratch/$name.full" "$actual"
  fi
  expected="$here/golden/$name.out"
  if [ "$update" -eq 1 ]; then
    cp "$actual" "$expected"
  elif [ ! -f "$expected" ]; then
    echo "golden $name: no $name.out"
    failed=$((failed + 1))
  elif ! cmp -s "$expected" "$actual"; then
    echo "golden $name: differs from $name.out"
    diff -u "$expected" "$actual" | head -60 | sed 's/^/    /'
    failed=$((failed + 1))
  fi
done

if [ "$queries" -eq 0 ]; then
  echo "golden: no queries under $here/golden" >&2
  exit 2
fi
if [ "$update" -eq 1 ]; then
  echo "golden: $queries queries written"
  [ "$failed" -eq 0 ] || exit 1
  exit 0
fi
if [ "$failed" -eq 0 ]; then
  echo "golden: $queries queries match"
  exit 0
fi
echo "golden: $failed of $queries queries differ or fail"
exit 1

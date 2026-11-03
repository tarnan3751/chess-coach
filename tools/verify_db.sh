#!/usr/bin/env bash
# Verifies an opening database before it is published (docs/reference/pipeline.md, **Release
# database**; data/release/README.md), one line per check:
#   - integrity: sqlite3's PRAGMA integrity_check, and the store opening it (chess-coach-cli
#     stats, which refuses a schema it does not understand);
#   - meta: every key of docs/database.md §6.1 present, schema_version 1, data_version, min_games
#     and max_ply decimal, months well formed, ascending and each once, tiers as §4 states,
#     games_admitted the sum of the admitted_tier_ keys and games_skipped the sum of the skipped_
#     keys;
#   - the invariants of §6.4 by SQL: every node's games are white + draws + black and at least
#     min_games; every move has a total of at least one and a node for its (key, tier); every
#     node's moves sum, column by column, to at most the node's; the start position's games at a
#     tier are at least that tier's admitted games when those reach min_games; and §6.2's rule
#     that positions holds exactly the keys with a node;
#   - the canonical dump's sha256 (docs/database.md §11), written, when every check before it
#     passed, to <db>.sha256 in shasum's format for a file named <db>.dump, so
#     `chess-coach-cli dump --db <db> > <db>.dump; shasum -c <db>.sha256` checks it;
#   - the file's size and its verdict under the delivery rule (PLAN.md §12 item 2, docs/
#     database.md §8): under 500 MB bundle it, from 500 MB to 1 GB the owner's call, over 1 GB a
#     separate download (and over 2 GB too large for a GitHub release asset), MB being 10^6 bytes.
# With --rebuild it then builds the database again from the inputs and knobs its report names
# (<db>.report.txt, as tools/build_release_db.sh writes it) into <db>.rebuild.db and compares the
# two canonical dumps byte for byte, the reproducibility criterion of §11; the rebuild is removed
# when they are equal and kept for inspection when they are not. It takes as long as the build.
#
#   tools/verify_db.sh <db> [--rebuild]
#
# Needs sqlite3 on the PATH (macOS has it; apt install sqlite3), and configures and builds the
# release preset's chess-coach-cli and, for --rebuild, chess-coach-pipeline. Reads <db>, writes
# <db>.sha256, and with --rebuild <db>.rebuild.db and its report. Exits non-zero when a check
# fails.
set -uo pipefail
cd "$(dirname "$0")/.."
root=$(pwd -P)

usage() {
  echo "usage: tools/verify_db.sh <db> [--rebuild]" >&2
  exit 2
}
[ $# -ge 1 ] && [ $# -le 2 ] || usage
db=$1
rebuild=0
if [ $# -eq 2 ]; then
  [ "$2" = --rebuild ] || usage
  rebuild=1
fi
[ -f "$db" ] || { echo "verify_db: no file $db" >&2; exit 2; }
db="$(cd "$(dirname "$db")" && pwd -P)/$(basename "$db")"
command -v sqlite3 >/dev/null || { echo "verify_db: sqlite3 not found on the PATH" >&2; exit 2; }
sha256() { if command -v shasum >/dev/null; then shasum -a 256; else sha256sum; fi; }
file_size() { if [ "$(uname -s)" = Darwin ]; then stat -L -f %z "$1"; else stat -L -c %s "$1"; fi; }

targets=(--target cli)
[ $rebuild -eq 0 ] || targets+=(--target pipeline)
log=$(mktemp) || exit 1
if ! { cmake --preset release && cmake --build --preset release "${targets[@]}"; } >"$log" 2>&1; then
  tail -20 "$log" >&2
  rm -f "$log"
  echo "verify_db: building the release preset failed" >&2
  exit 1
fi
rm -f "$log"
cli="$root/build/release/cli/chess-coach-cli"
pipeline="$root/build/release/pipeline/chess-coach-pipeline"

failed=0
check() { # check <name> <empty for ok, else what is wrong>
  if [ -z "$2" ]; then
    echo "verify: $1 ok"
  else
    echo "verify: $1 FAILED: $2"
    failed=1
  fi
}
query() { sqlite3 -readonly -batch -noheader "$db" "$1"; }
is_number() { [[ "$1" =~ ^[0-9]+$ ]]; }

# Integrity.
integrity=$(query "PRAGMA integrity_check;" 2>&1)
check integrity "$([ "$integrity" = ok ] || echo "$integrity" | head -3 | tr '\n' ' ')"
if stats=$("$cli" stats --db "$db" 2>&1); then
  check store ""
else
  check store "$(printf '%s\n' "$stats" | head -1)"
fi

# meta.
meta() { query "SELECT value FROM meta WHERE key = '$1';"; }
problems=""
keys="schema_version data_version months min_games max_ply tiers built_at pipeline_version eval_db
openings_tsv games_admitted games_skipped"
for tier in 0 1 2 3 4 5 6; do keys="$keys admitted_tier_$tier"; done
skipped_keys="skipped_malformed skipped_unrated skipped_speed skipped_variant skipped_rating
skipped_result skipped_moves"
for key in $keys $skipped_keys; do
  [ "$(query "SELECT COUNT(*) FROM meta WHERE key = '$key';")" = 1 ] || problems="$problems no $key;"
done
[ "$(meta schema_version)" = 1 ] || problems="$problems schema_version is not 1;"
for key in data_version min_games max_ply games_admitted games_skipped; do
  is_number "$(meta "$key")" || problems="$problems $key is not decimal;"
done
months=$(meta months)
[[ "$months" =~ ^[0-9]{4}-[0-9]{2}(,[0-9]{4}-[0-9]{2})*$ ]] || problems="$problems months is malformed;"
sorted=$(printf '%s\n' "$months" | tr ',' '\n' | LC_ALL=C sort -u | paste -sd, -)
[ "$sorted" = "$months" ] || problems="$problems months is not ascending and unique;"
[ "$(meta tiers)" = 1200,1400,1600,1800,2000,2200 ] || problems="$problems tiers is not §4's;"
admitted=0
for tier in 0 1 2 3 4 5 6; do
  value=$(meta "admitted_tier_$tier")
  is_number "$value" || { problems="$problems admitted_tier_$tier is not decimal;"; value=0; }
  admitted=$((admitted + value))
done
[ "$admitted" = "$(meta games_admitted)" ] ||
  problems="$problems games_admitted $(meta games_admitted) is not the tiers' sum $admitted;"
skipped=0
for key in $skipped_keys; do
  value=$(meta "$key")
  is_number "$value" || { problems="$problems $key is not decimal;"; value=0; }
  skipped=$((skipped + value))
done
[ "$skipped" = "$(meta games_skipped)" ] ||
  problems="$problems games_skipped $(meta games_skipped) is not the counters' sum $skipped;"
check meta "$problems"

# The invariants.
min_games=$(meta min_games)
is_number "$min_games" || min_games=0
problems=""
count() { # count <what is wrong> <SQL counting the rows that break it>
  local n
  n=$(query "$2" 2>&1)
  [ "$n" = 0 ] || problems="$problems $n $1;"
}
count "nodes whose games are not their columns' sum or are under min_games" \
  "SELECT COUNT(*) FROM nodes WHERE games <> white + draws + black OR games < $min_games;"
count "moves with a total under one" "SELECT COUNT(*) FROM moves WHERE white + draws + black < 1;"
count "moves without a node" "SELECT COUNT(*) FROM moves AS m WHERE NOT EXISTS
  (SELECT 1 FROM nodes AS n WHERE n.key = m.key AND n.tier = m.tier);"
count "nodes whose moves sum past them" "SELECT COUNT(*) FROM nodes AS n JOIN
  (SELECT key, tier, SUM(white) AS w, SUM(draws) AS d, SUM(black) AS b FROM moves
   GROUP BY key, tier) AS s ON s.key = n.key AND s.tier = n.tier
  WHERE s.w > n.white OR s.d > n.draws OR s.b > n.black;"
count "nodes without a position" "SELECT COUNT(*) FROM nodes AS n WHERE NOT EXISTS
  (SELECT 1 FROM positions AS p WHERE p.key = n.key);"
count "positions without a node" "SELECT COUNT(*) FROM positions AS p WHERE NOT EXISTS
  (SELECT 1 FROM nodes AS n WHERE n.key = p.key);"
for tier in 0 1 2 3 4 5 6; do
  start=$(printf '%s\n' "$stats" | sed -n "s/^tier $tier (.*, \([0-9]*\) games at the start\$/\1/p")
  admitted_tier=$(meta "admitted_tier_$tier")
  if is_number "$admitted_tier" && [ "$admitted_tier" -ge "$min_games" ] &&
    ! { is_number "$start" && [ "$start" -ge "$admitted_tier" ]; }; then
    problems="$problems the start position has ${start:-no} games at tier $tier, under its $admitted_tier admitted;"
  fi
done
check invariants "$problems"

# The dump's sha256 and the size.
name=$(basename "$db")
digest=$("$cli" dump --db "$db" | sha256 | awk '{print $1}')
if [ ${#digest} -eq 64 ] && [ $failed -ne 0 ]; then
  echo "verify: dump sha256 $digest (not written: a check failed)"
elif [ ${#digest} -eq 64 ]; then
  printf '%s  %s\n' "$digest" "$name.dump" >"$db.sha256"
  check "dump sha256 $digest (in $name.sha256)" ""
else
  check "dump sha256" "the dump could not be hashed"
fi
bytes=$(file_size "$db")
mb=$(awk -v b="$bytes" 'BEGIN { printf "%.1f", b / 1e6 }')
if [ "$bytes" -lt 500000000 ]; then
  verdict="bundle it in the installer"
elif [ "$bytes" -le 1000000000 ]; then
  verdict="the owner's call: between 500 MB and 1 GB"
elif [ "$bytes" -le 2000000000 ]; then
  verdict="a separate download the app imports"
else
  verdict="a separate download, but over GitHub's 2 GB asset limit"
fi
echo "verify: size $bytes bytes ($mb MB): $verdict (PLAN.md §12 item 2)"

if [ $failed -ne 0 ]; then
  echo "verify_db: FAILED"
  exit 1
fi
[ $rebuild -eq 1 ] || { echo "verify_db: all checks passed"; exit 0; }

# The rebuild from the report's inputs and knobs.
report="$db.report.txt"
[ -f "$report" ] || { echo "verify_db: --rebuild needs the build's report, $report" >&2; exit 1; }
field() { awk -F '\t' -v k="$1" '$1 == k { print $2 }' "$report"; }
inputs=()
while IFS= read -r input; do
  inputs+=("$input")
done < <(field input)
[ ${#inputs[@]} -gt 0 ] || { echo "verify_db: $report names no input" >&2; exit 1; }
again="$db.rebuild.db"
[ ! -e "$again" ] && [ ! -e "$again.partial" ] || { echo "verify_db: $again exists already" >&2; exit 1; }
args=(build --in "${inputs[@]}" --out "$again" --min-games "$(meta min_games)"
  --max-ply "$(meta max_ply)" --data-version "$(meta data_version)"
  --report "$again.report.txt")
[ "$(field eval)" = none ] || args+=(--eval "$(field eval)")
args+=(--names "$(field names)")
sketch_bytes=$(field sketch_bytes)
! is_number "$sketch_bytes" || [ "$sketch_bytes" -lt 1048576 ] ||
  args+=(--sketch-mib $((sketch_bytes / 1048576)))
! is_number "$(field threads)" || args+=(--threads "$(field threads)")
echo "verify: rebuilding from ${#inputs[@]} input(s) into $again"
SECONDS=0
"$pipeline" "${args[@]}" >/dev/null || { echo "verify: rebuild FAILED: the build failed"; exit 1; }
if cmp -s <("$cli" dump --db "$db") <("$cli" dump --db "$again"); then
  echo "verify: rebuild ok: the dumps are byte-equal (rebuilt in ${SECONDS} s)"
  rm -f "$again" "$again.report.txt"
  echo "verify_db: all checks passed, reproducible"
else
  echo "verify: rebuild FAILED: the dumps differ; $again is kept"
  exit 1
fi

#!/usr/bin/env bash
# Builds a release opening database (docs/reference/pipeline.md, **Release database**;
# data/release/README.md): the monthly dumps in <data-dir>, the evaluation database beside them
# (lichess_db_eval.jsonl.zst) and the committed data/chess-openings, at the release knobs of
# docs/database.md §8 unless told otherwise, through the release preset's chess-coach-pipeline,
# which it configures and builds first.
#
#   tools/build_release_db.sh <data-dir> <out.db> --data-version N [--months YYYY-MM,...]
#                             [--threads T] [--sketch-mib M] [--min-games G] [--max-ply P]
#
# The months are the files lichess_db_standard_rated_<YYYY-MM>.pgn.zst in <data-dir>: the ones
# --months names, or every one there, in ascending order. A month named twice, a named month
# with no file, an existing <out.db> or report, and a missing evaluation file are refused before
# anything is built. --min-games is 100 and --max-ply 30 unless given; --data-version is required
# (0 for a development build, a positive number for a publication, docs/database.md §6.1).
#
# Without --sketch-mib the sketch is sized by the sizing rule of pipeline.md, **The sketch**:
# the visits are estimated at one per ten compressed bytes (a recent month: about a hundred
# million games of some thirty visits in about 31 GB), and the size is the smallest power of
# two from 1 MiB that keeps them at min_games / 16 per counter or fewer, a row holding bytes / 4
# counters when min_games is at most 255 and bytes / 8 above, up to the pipeline's 16 GiB. The
# choice and the predicted visits per counter are printed before the build, and a prediction
# above min_games / 16 at 16 GiB says so. A trimmed month (chess-coach-pipeline trim,
# pipeline.md, **Trim**) holds about ten times the visits per compressed byte, so the estimate
# undersizes the sketch for one: pass --sketch-mib, sized for the months untrimmed.
#
# The pipeline's report goes to <out.db>.report.txt; the script then prints its key lines and the
# elapsed time. Check the result with tools/verify_db.sh <out.db>.
#
# A build of recent months reads tens of gigabytes per month and holds every core for hours, so
# it is the coordinator's or the owner's, on a quiet machine, and never a lane's (CLAUDE.md,
# **Lanes**): a lane runs it only on the sample corpus. Writes build/release/ in the tree it runs
# in, <out.db> and its report. Exits non-zero on a refusal or a failed build.
set -uo pipefail
cd "$(dirname "$0")/.."
root=$(pwd -P)

usage() {
  echo "usage: tools/build_release_db.sh <data-dir> <out.db> --data-version N [--months YYYY-MM,...]" >&2
  echo "                                 [--threads T] [--sketch-mib M] [--min-games G] [--max-ply P]" >&2
  exit 2
}
fail() {
  echo "build_release_db: $*" >&2
  exit 1
}
is_number() { [[ "$1" =~ ^[0-9]+$ ]]; }
file_size() { if [ "$(uname -s)" = Darwin ]; then stat -L -f %z "$1"; else stat -L -c %s "$1"; fi; }

[ $# -ge 2 ] || usage
data_dir=$1
out=$2
shift 2
data_version=""
months=""
threads=""
sketch_mib=""
min_games=100
max_ply=30
while [ $# -gt 0 ]; do
  [ $# -ge 2 ] || usage
  case "$1" in
    --data-version) data_version=$2 ;;
    --months) months=$2 ;;
    --threads) threads=$2 ;;
    --sketch-mib) sketch_mib=$2 ;;
    --min-games) min_games=$2 ;;
    --max-ply) max_ply=$2 ;;
    *) usage ;;
  esac
  shift 2
done
is_number "$data_version" || fail "--data-version N is required, a decimal integer"
is_number "$min_games" && [ "$min_games" -ge 1 ] && [ "$min_games" -le 65535 ] ||
  fail "--min-games must be between 1 and 65535"
is_number "$max_ply" || fail "--max-ply must be a decimal integer"
[ -z "$threads" ] || is_number "$threads" || fail "--threads must be a decimal integer"
[ -z "$sketch_mib" ] || is_number "$sketch_mib" || fail "--sketch-mib must be a decimal integer"

[ -d "$data_dir" ] || fail "no data directory $data_dir"
data_dir=$(cd "$data_dir" && pwd -P)
out_dir=$(dirname "$out")
[ -d "$out_dir" ] || fail "no directory $out_dir for $out"
out="$(cd "$out_dir" && pwd -P)/$(basename "$out")"
report="$out.report.txt"
[ ! -e "$out" ] && [ ! -e "$out.partial" ] || fail "$out or $out.partial exists already"
[ ! -e "$report" ] || fail "$report exists already"

# The months, ascending, each once, each with its file.
prefix=lichess_db_standard_rated_
if [ -z "$months" ]; then
  months=$(cd "$data_dir" && ls ${prefix}*.pgn.zst 2>/dev/null |
    sed -n "s/^${prefix}\([0-9][0-9][0-9][0-9]-[0-9][0-9]\)\.pgn\.zst\$/\1/p" | LC_ALL=C sort |
    paste -sd, -)
  [ -n "$months" ] || fail "no ${prefix}YYYY-MM.pgn.zst in $data_dir"
fi
month_list=$(printf '%s\n' "$months" | tr ',' '\n' | LC_ALL=C sort)
for month in $month_list; do
  [[ "$month" =~ ^[0-9]{4}-(0[1-9]|1[0-2])$ ]] || fail "$month is not a month (YYYY-MM)"
done
duplicate=$(printf '%s\n' "$month_list" | uniq -d | head -1)
[ -z "$duplicate" ] || fail "the month $duplicate is named twice"
inputs=()
compressed=0
for month in $month_list; do
  file="$data_dir/${prefix}${month}.pgn.zst"
  [ -f "$file" ] || fail "no dump for $month: $file"
  inputs+=("$file")
  compressed=$((compressed + $(file_size "$file")))
done
eval_file="$data_dir/lichess_db_eval.jsonl.zst"
[ -f "$eval_file" ] || fail "no evaluation database: $eval_file"
names_dir="$root/data/chess-openings"
[ -d "$names_dir" ] || fail "no $names_dir"

# The sketch's size by the sizing rule, unless given.
counter_bytes=1
[ "$min_games" -le 255 ] || counter_bytes=2
visits=$((compressed / 10))
if [ -z "$sketch_mib" ]; then
  # width >= visits * 16 / min_games, bytes = width * 4 rows * counter_bytes.
  needed=$(((visits * 16 / min_games + 1) * 4 * counter_bytes))
  sketch_mib=1
  while [ $((sketch_mib * 1048576)) -lt "$needed" ] && [ "$sketch_mib" -lt 16384 ]; do
    sketch_mib=$((sketch_mib * 2))
  done
  chosen="chosen by the sizing rule"
else
  chosen="as given"
fi
width=$((sketch_mib * 1048576 / (4 * counter_bytes)))
load=$(awk -v v="$visits" -v w="$width" 'BEGIN { printf "%.1f", v / w }')
echo "build_release_db: months $months, $compressed compressed bytes, about $visits visits"
echo "build_release_db: sketch $sketch_mib MiB ($chosen), $((counter_bytes * 8))-bit counters," \
  "width $width, about $load visits per counter (min_games $min_games)"
if awk -v l="$load" -v g="$min_games" 'BEGIN { exit !(l > g / 16) }'; then
  echo "build_release_db: the predicted load is above min_games / 16 per counter: pass two will" \
    "count more false positives (pipeline.md, **The sketch**)"
fi

log=$(mktemp) || exit 1
if ! { cmake --preset release && cmake --build --preset release --target pipeline; } >"$log" 2>&1
then
  tail -20 "$log" >&2
  rm -f "$log"
  fail "building the release preset's chess-coach-pipeline failed"
fi
rm -f "$log"
pipeline="$root/build/release/pipeline/chess-coach-pipeline"
args=(build --in "${inputs[@]}" --out "$out" --eval "$eval_file" --names "$names_dir"
  --min-games "$min_games" --max-ply "$max_ply" --data-version "$data_version"
  --sketch-mib "$sketch_mib" --report "$report")
[ -z "$threads" ] || args+=(--threads "$threads")
SECONDS=0
"$pipeline" "${args[@]}" || fail "chess-coach-pipeline build failed"
elapsed=$SECONDS

for key in output months min_games max_ply data_version eval_db games_read games_admitted \
  games_skipped sketch_bytes sketch_width candidates nodes moves positions positions_evaluated \
  positions_named pass_one_seconds pass_two_seconds total_seconds games_per_second \
  peak_rss_bytes; do
  awk -F '\t' -v k="$key" '$1 == k { printf "  %-20s %s\n", $1, $2 }' "$report"
done
printf 'build_release_db: built %s in %dh %02dm %02ds; report %s\n' "$out" $((elapsed / 3600)) \
  $((elapsed % 3600 / 60)) $((elapsed % 60)) "$report"

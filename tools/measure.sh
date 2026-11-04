#!/usr/bin/env bash
# The measurements of Phase 8 (docs/reference/testing-tools.md, **Measuring**; PLAN.md §11,
# 2026-11-04: no performance change without a number before and after). Builds the release
# preset, then measures, each section over --runs runs, and prints one markdown table of every
# metric's median and spread (min to max) with its method beside it:
#   startup   build/release/app/app_measure startup, one fresh process per run, the engine off
#             and on (one discarded run first: a freshly linked binary's first start pays the
#             system's signature check)
#   explore   app_measure explore: one process of --runs repetitions of each step, the engine off;
#             then --runs processes of two with the engine on (a search, then the cache's answer)
#   practice  app_measure practice: --runs processes of one game each, with the engine
#   board     app_measure board: one process of --runs repetitions, at a device pixel ratio of 1
#             and of 2 (QT_SCALE_FACTOR=2, as on a Retina display)
#   repertoire  app_measure repertoire: one process, --runs windows at each coverage depth (10 and
#             20 plies) over ten saved moves, entering the screen, changing colour, saving a move
#   repertoire_large  app_measure repertoire_large: the same over white's answer to every gap to 20
#             plies (a few hundred moves, built from the database) but one held out to save
#   drill     app_measure drill: one process, --runs windows, each a drill of five cards synced back
#   memory    app_measure memory: --runs processes, each one session of Explore steps, practice
#             games and a drill, the resident size read along it
#   pipeline  build/release/pipeline/pipeline_bench_test, the sample built --runs times
#   cli       chess-coach-cli explore (the start; after 1. e4 c5 2. Nf3 d6) and stats, --runs
#             times each after a discarded run, timed by perl's Time::HiRes around the process
#
#   tools/measure.sh [--db <file>] [--engine <binary>] [--runs <n>] [--tier <0-6>] [--out <file>]
#                    [--no-sounds] [<section>...]
# Sections: startup explore practice board repertoire repertoire_large drill memory pipeline cli
# (all of them by default).
#
# The app's runs are silent, so their moves leave out the sound the app plays as the board shows a
# move (on macOS, its post to the sound queue and the play there). On macOS each of startup,
# explore, practice, board, repertoire, drill and memory then runs again with app_measure --sounds,
# whose metrics end in ".sounds": startup and explore with the engine off, memory once. Those runs
# play the sounds audibly through NSSound, whose volume is the app's, out of the measurement's
# reach, so --no-sounds leaves them out. Elsewhere the app plays no sound and there is no such pass.
#
# --db is the opening database, read only (the release database for the baseline); without it the
# sample is built through the pipeline into a scratch directory. --engine is the Stockfish for the
# engine-on runs, else CHESS_COACH_STOCKFISH, else stockfish on the PATH; without one the engine-on
# runs and practice are skipped. --runs is 5, --tier the app's default, 2. --out writes the table
# to a file as well. The app runs offscreen (QT_QPA_PLATFORM, offscreen unless set), each run on a
# scratch data directory of its own, never the owner's; the engines at one thread and 16 MB of
# hash. Before measuring it waits until the one-minute load average is under MEASURE_MAX_LOAD
# (2.5), checking every 15 s for up to MEASURE_LOAD_WAIT seconds (600), then measures anyway and
# says so; the table's header gives the load before and after, and a before and an after compare
# only when they were measured at a similar load. The file cache is warm: nothing here can
# empty it. Writes build/release/ in the tree it runs in, so an agent runs it only in its clone.
set -uo pipefail
cd "$(dirname "$0")/.."

db=""
engine="${CHESS_COACH_STOCKFISH:-}"
runs=5
tier=2
out=""
sounds=1
[ "$(uname)" = Darwin ] || sounds=0 # the app plays no sound elsewhere
sections=()
while [ $# -gt 0 ]; do
  case "$1" in
    --db) db="${2:-}"; shift 2 ;;
    --engine) engine="${2:-}"; shift 2 ;;
    --runs) runs="${2:-}"; shift 2 ;;
    --tier) tier="${2:-}"; shift 2 ;;
    --out) out="${2:-}"; shift 2 ;;
    --no-sounds) sounds=0; shift ;;
    startup|explore|practice|board|repertoire|repertoire_large|drill|memory|pipeline|cli)
      sections+=("$1")
      shift
      ;;
    *)
      echo "usage: tools/measure.sh [--db <file>] [--engine <binary>] [--runs <n>] [--tier <0-6>]" \
        "[--out <file>] [--no-sounds]" \
        "[startup|explore|practice|board|repertoire|repertoire_large|drill|memory|pipeline|cli]..." >&2
      exit 2
      ;;
  esac
done
case "$runs" in ''|*[!0-9]*|0) echo "measure: --runs must be a positive number" >&2; exit 2 ;; esac
[ ${#sections[@]} -gt 0 ] ||
  sections=(startup explore practice board repertoire repertoire_large drill memory pipeline cli)
if [ -z "$engine" ]; then
  engine="$(command -v stockfish || true)"
fi
if [ -n "$db" ] && [ ! -r "$db" ]; then
  echo "measure: no readable database at $db" >&2
  exit 2
fi
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

if ! cmake --preset release >/dev/null 2>&1 || ! cmake --build --preset release >/dev/null; then
  echo "measure: the release build failed" >&2
  exit 1
fi
measure=build/release/app/app_measure
cli=build/release/cli/chess-coach-cli
bench=build/release/pipeline/pipeline_bench_test

scratch="$(mktemp -d "${TMPDIR:-/tmp}/chess-coach-measure.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT
samples="$scratch/samples.tsv"
: >"$samples"

if [ -z "$db" ]; then
  db="$scratch/sample.db"
  if ! build/release/pipeline/chess-coach-pipeline build \
      --in pipeline/tests/sample/lichess_db_standard_rated_2013-01.pgn.zst \
      --eval pipeline/tests/sample/eval/lichess_db_eval.jsonl.zst --out "$db" \
      --min-games 2 --max-ply 20 --data-version 0 >"$scratch/sample.log" 2>&1; then
    echo "measure: the sample build failed" >&2
    cat "$scratch/sample.log" >&2
    exit 1
  fi
fi

# load: the one-minute load average, or 0 where uptime does not give it.
load() {
  uptime 2>/dev/null | sed -nE 's/.*load averages?: *([0-9.]+).*/\1/p' | tr -d ',' | grep . || echo 0
}
max_load="${MEASURE_MAX_LOAD:-2.5}"
waited=0
load_note=""
while awk -v l="$(load)" -v m="$max_load" 'BEGIN { exit !(l >= m) }'; do
  if [ "$waited" -ge "${MEASURE_LOAD_WAIT:-600}" ]; then
    load_note=" (measured above the limit of $max_load after waiting ${waited} s)"
    break
  fi
  echo "measure: load $(load), waiting for it to fall under $max_load" >&2
  sleep 15
  waited=$((waited + 15))
done
load_before="$(load)"

# app <args>...: one app_measure process, its lines kept; a failure stops the measurement.
app() {
  if ! "$measure" "$@" >>"$samples" 2>"$scratch/app.err"; then
    echo "measure: app_measure $* failed" >&2
    cat "$scratch/app.err" >&2
    exit 1
  fi
}

# timed <metric> <method> <command>...: the command's wall time in milliseconds, after one
# discarded run; the command's output goes to /dev/null, the sample alone into the samples.
timed() {
  local metric="$1" method="$2" i
  shift 2
  "$@" >/dev/null 2>&1 || { echo "measure: $* failed" >&2; exit 1; }
  printf 'method\t%s\tms\t%s\n' "$metric" "$method" >>"$samples"
  for ((i = 0; i < runs; i++)); do
    perl -MTime::HiRes=time -e '$m = shift; open(my $samples, ">&", \*STDOUT) or exit 1;
      open(STDOUT, ">", "/dev/null") or exit 1; $t = time; system(@ARGV) == 0 or exit 1;
      printf $samples "sample\t%s\t%.2f\n", $m, (time - $t) * 1000' "$metric" "$@" \
      >>"$samples" 2>/dev/null </dev/null || { echo "measure: $* failed" >&2; exit 1; }
  done
}

# run_section <section> [--sounds]: the section's runs, silent or with the app's sounds.
skipped=""
run_section() {
  local section="$1" sound="${2:-}" i
  echo "measure: $section${sound:+ with the sounds}" >&2
  case "$section" in
    startup)
      "$measure" startup --db "$db" --tier "$tier" $sound >/dev/null 2>&1
      for ((i = 0; i < runs; i++)); do app startup --db "$db" --tier "$tier" $sound; done
      if [ -n "$engine" ] && [ -z "$sound" ]; then
        for ((i = 0; i < runs; i++)); do app startup --db "$db" --tier "$tier" --engine "$engine"; done
      fi
      ;;
    explore)
      app explore --db "$db" --tier "$tier" --runs "$runs" $sound
      if [ -n "$engine" ] && [ -z "$sound" ]; then
        for ((i = 0; i < runs; i++)); do
          app explore --db "$db" --tier "$tier" --runs 2 --engine "$engine"
        done
      fi
      ;;
    practice)
      if [ -z "$engine" ]; then
        [ -n "$sound" ] || skipped="$skipped practice" # named once, not again for the sounds pass
        return
      fi
      for ((i = 0; i < runs; i++)); do
        app practice --db "$db" --tier "$tier" --runs 1 --engine "$engine" $sound
      done
      ;;
    board)
      app board --db "$db" --tier "$tier" --runs "$runs" $sound
      QT_SCALE_FACTOR=2 app board --db "$db" --tier "$tier" --runs "$runs" $sound
      ;;
    repertoire)
      app repertoire --db "$db" --tier "$tier" --runs "$runs" $sound
      ;;
    repertoire_large)
      app repertoire_large --db "$db" --tier "$tier" --runs "$runs"
      ;;
    drill)
      app drill --db "$db" --tier "$tier" --runs "$runs" $sound
      ;;
    memory)
      local memory_runs="$runs"
      [ -z "$sound" ] || memory_runs=1
      for ((i = 0; i < memory_runs; i++)); do
        app memory --db "$db" --tier "$tier" ${engine:+--engine "$engine"} $sound
      done
      ;;
    pipeline)
      printf 'method\tpipeline.sample_build\tms\t%s\n' \
        "pipeline_bench_test: the sample built in-process at the golden knobs, four threads" \
        >>"$samples"
      printf 'method\tpipeline.games_per_second\tgames/s\t%s\n' "the same build's throughput" \
        >>"$samples"
      printf 'method\tpipeline.peak_rss\tMB\t%s\n' "the same process's peak resident size" \
        >>"$samples"
      for ((i = 0; i < runs; i++)); do
        line="$("$bench" 2>&1 | grep 'pipeline bench:')" || { echo "measure: $bench failed" >&2; exit 1; }
        echo "$line" | awk '{
          printf("sample\tpipeline.sample_build\t%.2f\n", $6 * 1000)
          printf("sample\tpipeline.games_per_second\t%s\n", $8)
          printf("sample\tpipeline.peak_rss\t%.2f\n", $12 / 1048576) }' >>"$samples"
      done
      ;;
    cli)
      timed cli.explore_start \
        "chess-coach-cli explore of the start position, a process from launch to exit" \
        "$cli" explore --db "$db" --tier "$tier"
      timed cli.explore_sicilian \
        "chess-coach-cli explore after 1. e4 c5 2. Nf3 d6, the same" \
        "$cli" explore --db "$db" --tier "$tier" --moves "e4 c5 Nf3 d6"
      timed cli.stats "chess-coach-cli stats, the same" "$cli" stats --db "$db"
      ;;
  esac
}

for section in "${sections[@]}"; do
  run_section "$section"
  case " startup explore practice board repertoire drill memory " in
    *" $section "*) [ "$sounds" = 0 ] || run_section "$section" --sounds ;;
  esac
done
load_after="$(load)"
sounds_note="off"
[ "$sounds" = 0 ] || sounds_note="a second pass with them on, its metrics ending in .sounds, audible"

# The header, then one row per metric in the order the metrics first appeared.
{
  echo "Measured $(date '+%Y-%m-%d %H:%M') on commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)$(git diff --quiet HEAD 2>/dev/null || echo ' (with local changes)')."
  if [ "$(uname)" = Darwin ]; then
    echo "Machine: $(sysctl -n machdep.cpu.brand_string), $(sysctl -n hw.ncpu) cores," \
      "$(( $(sysctl -n hw.memsize) / 1073741824 )) GB, macOS $(sw_vers -productVersion)."
  else
    echo "Machine: $(uname -srm), $(getconf _NPROCESSORS_ONLN) cores."
  fi
  echo "Database: $db ($(awk -v b="$(wc -c <"$db")" 'BEGIN { printf("%.1f", b / 1048576) }') MB)," \
    "tier $tier. Engine: ${engine:-none}." \
    "Platform: $QT_QPA_PLATFORM. Runs: $runs. Load average (1 min): $load_before before," \
    "$load_after after$load_note.${skipped:+ Skipped, without an engine:$skipped.}" \
    "Sounds: $sounds_note."
  echo
  echo "| Metric | Median | Min – max | n | Unit | Method |"
  echo "|---|---:|---:|---:|---|---|"
  awk -F'\t' '
    $1 == "method" && !($2 in unit) { order[++count] = $2; unit[$2] = $3; how[$2] = $4 }
    $1 == "sample" { n[$2]++; value[$2, n[$2]] = $3 + 0 }
    END {
      for (i = 1; i <= count; i++) {
        m = order[i]
        if (!(m in n)) continue
        k = n[m]
        for (a = 1; a <= k; a++) sorted[a] = value[m, a]
        for (a = 2; a <= k; a++) {
          v = sorted[a]
          for (b = a - 1; b >= 1 && sorted[b] > v; b--) sorted[b + 1] = sorted[b]
          sorted[b + 1] = v
        }
        median = (k % 2) ? sorted[(k + 1) / 2] : (sorted[k / 2] + sorted[k / 2 + 1]) / 2
        printf("| `%s` | %.1f | %.1f – %.1f | %d | %s | %s |\n", m, median, sorted[1], sorted[k],
               k, unit[m], how[m])
      }
    }' "$samples"
} | if [ -n "$out" ]; then tee "$out"; else cat; fi

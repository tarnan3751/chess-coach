#!/usr/bin/env bash
# Regenerates the sample corpus under pipeline/tests/sample/ (docs/reference/pipeline.md, **The
# sample corpus**). Tier A to run: the golden dump is the pipeline's expected output, so it is
# regenerated only under a brief that says so, and the diff is reviewed.
#   tools/make_sample.sh <data-dir>
# <data-dir> holds the Lichess downloads lichess_db_standard_rated_2013-01.pgn.zst and
# lichess_db_eval.jsonl.zst (read only). The script:
#   1. cuts the month's first 2,000 games, recompressed with zstd -19, keeping the month's name
#      (docs/database.md §6.1 refuses an input not named as a month);
#   2. builds them at the golden knobs without evaluations, and keeps the lines of the
#      evaluation database's first 5,000,000 whose FEN is exactly a surviving position's
#      (zstdcat | head | awk over the dump's position FENs), recompressed with zstd -19;
#   3. builds the sample at the golden knobs (--min-games 2 --max-ply 20 --data-version 0) with
#      that slice and the committed names, and writes chess-coach-cli dump of it as golden.dump.
# Builds the dev preset first; writes build/dev/ and build/dev/make-sample/ in the tree it runs in.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ $# -ne 1 ] || [ ! -d "$1" ]; then
  echo "usage: tools/make_sample.sh <data-dir>" >&2
  exit 2
fi
data="$1"
cmake --preset dev >/dev/null
cmake --build --preset dev >/dev/null

pipeline=build/dev/pipeline/chess-coach-pipeline
cli=build/dev/cli/chess-coach-cli
sample=pipeline/tests/sample
work=build/dev/make-sample
knobs=(--min-games 2 --max-ply 20 --data-version 0)
rm -rf "$work"
mkdir -p "$work" "$sample/eval"

{ zstdcat "$data/lichess_db_standard_rated_2013-01.pgn.zst" 2>/dev/null || true; } |
  awk '/^\[Event /{games++} games > 2000 {exit} {print}' >"$work/sample.pgn"
zstd -q -19 -f "$work/sample.pgn" -o "$sample/lichess_db_standard_rated_2013-01.pgn.zst"

"$pipeline" build --in "$sample/lichess_db_standard_rated_2013-01.pgn.zst" \
  --out "$work/positions.db" "${knobs[@]}" >/dev/null 2>&1
"$cli" dump --db "$work/positions.db" | awk -F'\t' '$1 == "position" { print $3 }' >"$work/fens.txt"
# A line begins {"fen":"<fen>", so with " as the separator its fourth field is the FEN.
{ zstdcat "$data/lichess_db_eval.jsonl.zst" 2>/dev/null || true; } | head -n 5000000 |
  awk -F'"' 'NR == FNR { fens[$0] = 1; next } $2 == "fen" && ($4 in fens)' "$work/fens.txt" - \
    >"$work/eval.jsonl"
zstd -q -19 -f "$work/eval.jsonl" -o "$sample/eval/lichess_db_eval.jsonl.zst"

"$pipeline" build --in "$sample/lichess_db_standard_rated_2013-01.pgn.zst" \
  --eval "$sample/eval/lichess_db_eval.jsonl.zst" --out "$work/sample.db" "${knobs[@]}" >/dev/null 2>&1
"$cli" dump --db "$work/sample.db" >"$sample/golden.dump"
echo "make_sample: $(wc -l <"$work/fens.txt" | tr -d ' ') positions, $(wc -l <"$work/eval.jsonl" | tr -d ' ') evaluation lines, $(wc -l <"$sample/golden.dump" | tr -d ' ') dump lines"

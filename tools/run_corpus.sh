#!/usr/bin/env bash
# The golden corpora, the gate's corpus step (CLAUDE.md, **Commands**; docs/reference/pipeline.md,
# **The sample corpus**). Configures and builds the dev preset, then:
#   1. builds pipeline/tests/sample/ with the dev preset's chess-coach-pipeline at the golden knobs
#      (--min-games 2 --max-ply 20 --data-version 0, the sample's evaluation slice and the
#      committed opening names), into build/dev/corpus/;
#   2. dumps it with chess-coach-cli dump and compares the dump byte for byte with
#      pipeline/tests/sample/golden.dump;
#   3. runs cli/tests/run_golden.sh when it exists (the CLI's golden queries).
# Exits non-zero on any difference, with the head of the diff; the last line says what passed.
# Writes build/dev/ in the tree it runs in, so an agent runs it only in its clone.
# Regenerating the sample and its golden dump is tier A, under a brief that says so:
#   tools/make_sample.sh <directory holding the Lichess downloads>
set -uo pipefail
cd "$(dirname "$0")/.."

if ! cmake --preset dev >/dev/null || ! cmake --build --preset dev >/dev/null; then
  echo "run_corpus: the dev build failed" >&2
  exit 1
fi

pipeline=build/dev/pipeline/chess-coach-pipeline
cli=build/dev/cli/chess-coach-cli
sample=pipeline/tests/sample
out=build/dev/corpus
rm -rf "$out"
mkdir -p "$out"

if ! "$pipeline" build --in "$sample/lichess_db_standard_rated_2013-01.pgn.zst" \
    --eval "$sample/eval/lichess_db_eval.jsonl.zst" --out "$out/sample.db" \
    --min-games 2 --max-ply 20 --data-version 0 --report "$out/sample.report" 2>"$out/sample.log"; then
  echo "run_corpus: the sample build failed" >&2
  cat "$out/sample.log" >&2
  exit 1
fi
if ! "$cli" dump --db "$out/sample.db" >"$out/sample.dump"; then
  echo "run_corpus: chess-coach-cli dump failed" >&2
  exit 1
fi
if ! cmp -s "$out/sample.dump" "$sample/golden.dump"; then
  echo "run_corpus: the sample's dump differs from $sample/golden.dump" >&2
  diff "$sample/golden.dump" "$out/sample.dump" | head -40 >&2
  exit 1
fi
lines=$(wc -l <"$sample/golden.dump" | tr -d ' ')
echo "corpus: the sample reproduces golden.dump ($lines lines)"

if [ -x cli/tests/run_golden.sh ]; then
  if ! cli/tests/run_golden.sh "$out/sample.db"; then
    echo "run_corpus: cli/tests/run_golden.sh failed" >&2
    exit 1
  fi
  echo "corpus: the sample reproduces golden.dump ($lines lines); the CLI's golden queries pass"
fi

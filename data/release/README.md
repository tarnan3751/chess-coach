# Release databases

A release of chess-coach comes with an opening database, `openings.db`, built by the pipeline
from the Lichess monthly dumps and their evaluation database. The file is never committed: this
directory holds only how one is produced and verified, and the record of every one published.

## Producing one

On a quiet machine, never in an agent's lane (a recent month is about 31 GB compressed and holds
every core for hours), with the dumps downloaded into one directory:

```
tools/build_release_db.sh ~/Development/chess-coach-data openings.db --data-version N \
  --months 2026-10
tools/verify_db.sh openings.db --rebuild
```

`build_release_db.sh` builds at the release knobs (`min_games` 100, `max_ply` 30;
`docs/database.md` §8) from the months named, or every month file in the directory, with the
directory's `lichess_db_eval.jsonl.zst` and the committed `data/chess-openings`, and writes the
pipeline's report beside the database as `openings.db.report.txt`. `verify_db.sh` checks the
file's integrity, its `meta` and the counting invariants, writes the sha256 of its canonical dump
to `openings.db.sha256`, gives the size's verdict under the delivery rule, and with `--rebuild`
builds it a second time from the same inputs and compares the two dumps byte for byte. A
database is published only when every check passes and the rebuild is equal.
`docs/reference/pipeline.md`, **Release database**, describes both scripts.

The plan's release target is twelve months (`PLAN.md` §6.6). A month's dump must be on disk while
it is read, and today's disk holds one recent month at a time beside the evaluation database, so
a database of more months waits on the owner's decision about storage.

## Its data version

`meta`'s `data_version` (`docs/database.md` §6.1) is `0` for every development build and a
positive number for a published database: 1 for the first, one more for each publication after
it, whatever the app's version. The About dialog shows it beside the app's version, and a spar
session saved in a profile records the data version it was played against.

## Where the file goes

Not into the repository. Under the delivery rule (`PLAN.md` §12 item 2, `docs/database.md` §8) a
database under 500 MB is bundled into the installer (configure the release preset with
`-DCHESS_COACH_BUNDLE_DB=<file>`; `docs/reference/testing-tools.md`, **Packaging**); one over
1 GB is a separate download, attached to the GitHub release as an asset (at most 2 GB); between
the two, the owner decides. `verify_db.sh` prints the verdict.

## Published databases

One row per published database, filled in when it is published. The sha256 is the canonical
dump's, from `openings.db.sha256`: two files with the same dump are the same database, whenever
and wherever they were built.

| Data version | Months | Knobs (`min_games`, `max_ply`) | Size | Dump sha256 | Built on |
|---|---|---|---|---|---|

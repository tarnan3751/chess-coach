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
it is read, and today's disk holds one recent month at a time beside the evaluation database. So
the release inputs may be trimmed files: `chess-coach-pipeline trim --out-dir <dir> <in>...`
(`docs/reference/pipeline.md`, **Trim**) writes each month file and the evaluation database
under its own name, holding only what the build reads. A trimmed month builds, at a `max_ply` no
greater than the trim's, the database of its dump but for `meta`'s skip counts (`games_skipped`
and `skipped_*`: the games trim drops never reach the build), and the trimmed evaluation database
builds the same database exactly. A trimmed month needs `--sketch-mib`, sized for the month
untrimmed (`build_release_db.sh`'s sizing note). Since a trimmed file and its dump share a name,
a published database names its inputs by checksum: the sha256 of every file it was built from,
trimmed or not, in its row's inputs column.

## Its data version

`meta`'s `data_version` (`docs/database.md` §6.1) is `0` for every development build and a
positive number for a published database: 1 for the first, one more for each publication after
it, whatever the app's version. The About dialog shows it beside the app's version, and a spar
session saved in a profile records the data version it was played against.

## Where the file goes

Not into the repository. A published database is the asset of a GitHub release of its own, tagged
`data-vN` for its data version N and named `openings-vN.db`: the first is `data-v1` with
`openings-v1.db`. Under the delivery rule (`PLAN.md` §12 item 2, `docs/database.md` §8) a database
under 500 MB is bundled into the installer; one over 1 GB is a separate download, attached to the
GitHub release as an asset (at most 2 GB); between the two, the owner decides. `verify_db.sh`
prints the verdict.

`bundled.txt` here names the database a release bundles, in three lines: the data release's tag,
the asset's name, and the sha256 of the file itself (`shasum -a 256`, not the canonical dump's of
the table below). The release workflow reads it, downloads the asset with `gh release download`,
fails when the file's sha256 is not that one, and packages it with `tools/package.sh --db <file>`
as `Contents/Resources/openings.db` (`docs/reference/testing-tools.md`, **Packaging** and
**Release**). This is a download of a published artefact by CI, as its brew and vcpkg installs
are, not a download in the build, which stays offline. To bundle a new database: publish it as
`data-vN`, fill its row below, and change the three lines of `bundled.txt` to it.

## Published databases

One row per published database, filled in when it is published. The sha256 is the canonical
dump's, from `openings.db.sha256`: two files with the same dump are the same database, whenever
and wherever they were built. The inputs are each file the build read, by name and sha256 (`shasum
-a 256`), the evaluation database among them, and whether it was trimmed, with the trim's
`--max-ply` for a trimmed month: the file does not record it, and it bounds the `max_ply` of every
later build from that month.

| Data version | Months | Knobs (`min_games`, `max_ply`) | Size | Dump sha256 | Inputs (sha256) | Built on |
|---|---|---|---|---|---|---|
| 1 | 2026-10 | 100, 30 | 299,954,176 bytes (286 MiB) | `2b21481bd37ef7f958176711e974711ab2caf123dbeab4d5b02fcb4d29c4dbd6` | `lichess_db_standard_rated_2026-10.pgn.zst` `451f7baa823a9a4414410e1703ce806e0292e7a9f6875dd425d1eed6654b4527` (trimmed, `--max-ply` 30); `lichess_db_eval.jsonl.zst` `38726b82eac7388eed79c4bf122bd67c1162459fc2a49da760225f2676b68487` (trimmed) | 2026-11-04T03:46Z, pipeline 0.1.0, rebuilt byte-equal; published as `openings-v1.db` of the GitHub release `data-v1` |

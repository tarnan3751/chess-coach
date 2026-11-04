# Pipeline

How `chess-coach-pipeline` builds `openings.db` from the Lichess dumps. The contract it meets
(the admitted games, the tiers, the key, the schema, the counting rule, the badges, the knobs,
the evaluation merge, the names, the dump) is `docs/database.md`; this file says how. The rules
it obeys are in `CLAUDE.md`, **`pipeline/` streams and never stores**. It is a development tool,
run from a build tree; the app reads the file it writes.

| Source (`pipeline/src/`) | Holds |
|---|---|
| `main.cpp` | the `build` and `trim` command lines (CLI11) |
| `options` | the build's options; the month-file checks of the inputs |
| `build` | `run_build`: the passes and the stages after them, in order |
| `zstd_istream` | `ZstdStreambuf`, streaming decompression of one file |
| `zstd_ostream` | `ZstdOstreambuf`, streaming compression into one file, for `trim` |
| `chunker` | `Chunker` and `split_games`: the stream cut into batches of whole games or lines |
| `work_pool` | `run_pool`: one producer, a bounded queue, the workers |
| `game_filter` | the §3 header checks and `SkipCounts` |
| `game_scanner` | `GameScanner`: one game's text to its keys and moves |
| `move_code` | a UCI move packed into 16 bits |
| `sketch` | `VisitSketch`, pass one's count-min sketch |
| `aggregate` | pass two's exact counts, `BatchCounts` and the sharded `Aggregate` |
| `tree` | the survivors in memory, between pass two and the writer |
| `eval_merge` | the §9 merge |
| `badges` | the §7 badges and their thresholds |
| `writer` | the SQLite writer over core's schema |
| `report` | the report lines and the peak resident size |
| `trim` | `run_trim`: the inputs reduced to what the build reads (**Trim**) |

## Running it

```
chess-coach-pipeline build --in <lichess_db_standard_rated_YYYY-MM.pgn.zst>... --out <openings.db>
    [--eval <lichess_db_eval.jsonl.zst>] [--names <dir>|none] [--min-games N] [--max-ply N]
    [--threads N] [--data-version N] [--report <file>] [--sketch-mib N]
```

| Option | Default | Meaning |
|---|---|---|
| `--in` | required | One or more month files, distinct months, named as §6.1 requires |
| `--out` | required | The database; refused when it or `<out>.partial` exists |
| `--eval` | none | The evaluation database; without it no position is evaluated, so no move is Book or Dubious |
| `--names` | the source tree's `data/chess-openings` | The TSV directory, compiled in from the build tree; `none` names nothing |
| `--min-games` | 100 | §8's knob, 1 to 65535 |
| `--max-ply` | 30 | §8's knob |
| `--threads` | the hardware's | Worker threads; the output does not depend on it |
| `--data-version` | 0 | meta's `data_version` |
| `--report` | stdout | Where the report goes |
| `--sketch-mib` | from the inputs | The sketch's size, a power of two from 1 to 16384; the output does not depend on it (**The sketch** says how large a build needs) |

Progress goes to stderr every million games of a pass. The exit status is 0 on success, and on
`--version`, `--help` and no arguments, and 1 on a fatal error: a command line CLI11 rejects, an
unreadable or truncated input, a refused option, a count past 2^32 − 1, a write failure. A bad
game is counted, never fatal. On a fatal error no output is left behind: the writer writes
`<out>.partial` and renames it only once the write succeeded.

## Trim

```
chess-coach-pipeline trim --out-dir <dir> [--max-ply N] <in>...
```

Reduces the owner's inputs to what `build` reads, so that twelve months fit on disk. Each input
is a month file, `lichess_db_standard_rated_YYYY-MM.pgn.zst`, or the evaluation database,
`lichess_db_eval.jsonl.zst`, known by its name, and is written under the same name in `<dir>`,
which is created when missing. Any other name, two inputs of one name, and an output or
`<name>.partial` that exists in `<dir>` are refused before anything is written. Each file is
streamed through zstd in (`ZstdStreambuf`) and out (`ZstdOstreambuf`: `ZSTD_compressStream2` at
level 9 with four worker threads, in the calling thread when the zstd library has none), one
game or one line at a time, into `<name>.partial`, renamed once whole, and reading stops once
the write has failed. An unreadable input or a failed write leaves nothing behind for its file;
the inputs are trimmed in order, so when a later one fails the earlier outputs stay, whole, and a
rerun that names them again is refused for them, as for any output that exists. One line per
file goes to stdout: `<name>: N games seen, M kept, B bytes in, C bytes out, S s` for a month,
`<name>: N lines, M passed through unchanged, B bytes in, C bytes out, S s` for the evaluation
file. The exit status is as for `build`.

**A month file.** A game is its tag lines and its moves, which start, as they do for the
parser, at the first blank line or movetext line after the tags; it ends where the build's
chunker ends it, at a line that begins `[Event `, or at any line that begins `[` once its moves
have started. A tag line is read as the parser reads one, `[Name "value"]`: the name up to the
first space or tab, the value up to the first quote a backslash does not escape, then the
closing bracket and the end of the line; the header checks are given the value without its
backslashes, as the parser gives it, and the tag is written as it was. A game is kept when
`check_headers` (`game_filter`, §3's checks 2 to 6) admits it or refuses it for its variant or
its result: those two checks read tags trim writes, so the build makes them again and counts the
game as it would have. An unrated game, one of another speed and one without two decimal ratings
are dropped, and so are two kinds the build counts malformed: a game whose moves never start (its
tags followed by the next game or the end of the file) and one with a tag line the parser rejects
(its closing quote or bracket missing). A kept game is written as its tags Event, Result,
WhiteElo, BlackElo and, when present, Variant, SetUp and FEN, in that order; a blank line; its
first `--max-ply` (default 30, the build's default knob, 0 to 1000) moves as the parser reads
them, without comments (the clocks and evaluations among them), NAGs, move numbers (a number
glued to its move, `1.e4` or `1...e5`, leaves the move) and glyphs standing alone (`!`, `?!`,
which the scanner ignores, so they are no ply), numbered again from `1.`; then its Result tag's
result (`*` without one), alone for a game whose moves started empty; and a blank line. A result
token in the movetext ends the moves; a glyph on a move (`Nf3?!`) stays there, and the scanner
strips it as before.

So a build from a trimmed month at a `max_ply` no greater than the trim's gives the database of
the build from the dump, but for `meta`'s `games_skipped` and `skipped_*` counts: the games trim
drops are skipped by the build before their moves either way, and from the trimmed file never
reach it. Nothing in the trimmed file records the trim's `--max-ply`, and a build at a larger
`max_ply` reads it without complaint but finds every game cut at the trim's: a trimmed month
bounds the `max_ply` of every later build from it, and a release's row records the trim's
(`data/release/README.md`). Differences remain only for text Lichess does not write. The one
known is a game the parser counts malformed for a tag trim does not write (a value over its 255
characters), which the trimmed file lets through. Beyond that, trim reads the text line by line
and token by token in the shapes above, so a shape the parser reads in a way of its own, such as
a variation in parentheses, a line inside a comment that begins with `[`, or text after a tag's
closing bracket, may still read differently.

**The evaluation file.** Line for line, in order. A line that is a JSON object with a `"fen"`
string the merge's own reading of the raw line finds as it is (`fen_field`: the text after the
first `"fen":"`, up to the next quote), and whose evaluation chosen by §9's rule 2 (the merge's
own choice, `choose` in `eval_merge.cpp`: the deepest, then the greater knodes, then the earlier)
has a usable first pv, is written in the same shape with that evaluation alone, its first pv
reduced to its `cp` or `mate` and the first move of its `line`, its `knodes` and `depth` kept:
`{"fen":"…","evals":[{"pvs":[{"cp":25,"line":"d2d4"}],"knodes":50,"depth":40}]}`. Any other line
(malformed JSON, no `"fen"`, a `"fen"` the merge reads otherwise, with a space after its colon or
an escape in its FEN, an evaluation without an integer depth or knodes, a chosen first pv without
exactly one score or without a line) is written unchanged. The merge reads the same
candidate from a trimmed line as from the original, checks the same move against the same
position, and finds the same earlier line of two equal ones, so a build with the trimmed
evaluation file gives the same database byte for byte.

## Passes

1. **Checks.** The inputs' names and months (§6.1), the output path, `min_games`.
2. **Pass one** streams every input: the reader thread decompresses (`ZstdStreambuf`) and cuts
   the text into batches of about 4 MiB of whole games (`Chunker`, before a line that begins
   `[Event `); the workers split a batch into games and scan each; every visit of an admitted
   game is added to the sketch.
3. **Pass two** streams the inputs again through the same scanner. A visit whose sketch estimate
   reaches `min_games` is a candidate: it is counted exactly, by result, with the move played
   from it and the move's child; the FEN of a candidate key is recorded once.
4. **The cross-pass check.** The two passes must have admitted and skipped the same games, per
   tier and per reason, or the build stops ("the inputs read differently in pass two").
5. **Pruning.** Candidates whose exact visits are below `min_games` are dropped, with their
   moves; the sketch is freed.
6. **The evaluation merge** (§9), when `--eval` is given.
7. **Names** (§10) from core's `load_names`, matched by key.
8. **Badges** (§7).
9. **The writer** and the rename; then the report.

Memory is the sketch, the candidates of pass two with their moves and FENs, and one batch per
thread in the queue and in each worker; never the stream, never every distinct position.

## The scanner

`GameScanner` parses each game with its own `chess::pgn::StreamParser<64>` (a 4 KiB read
buffer), so a parser error, which ends a parser's stream, touches that game alone. It counts as
§3 says: a header the parser cannot read (a string token over its 255 characters), or a game
that ends before its moves start, is `malformed`; a game a header check had already failed keeps
that check's counter; a move token over the limit before the cap is a move that cannot be read,
`moves`; one past the cap is never read, so the game is admitted with what was collected. At
`startMoves` it runs the §3 checks 2 to 6 (`check_headers`); a failing game is skipped unparsed. An admitted
game's tier is core's `tier_of`, its result the `Result` header. Each move token has its glyph
stripped (`strip_glyphs`) and a NAG (`$n`) or a comment-only token ignored, then is played with
`Position::play_san_fast`, which gives the UCI and the child key; after `max_ply` moves the
parser skips the rest of the game, so a bad move past the cap is never read. A move that does not
play skips the game as `moves`, and a skipped game contributes nothing: the scanner hands a game
to the pass only at `endPgn`. Ratings are decimal integers of up to nine digits.

## The sketch

`VisitSketch` has four rows of `width` counters, `width` a power of two. The counters are 8-bit
when `min_games` is at most 255, as the default 100 is, and 16-bit above it: the cells are a
`std::variant` of the two vectors, and `add` and `estimate` are one body compiled for each, behind
the one interface. A visit of (tier, key) adds one to one counter per row, chosen by
multiply-shift hashing of the key mixed with a per-tier salt, and the estimate is the least of the
four. Counters saturate at `min_games` (a compare-and-swap that stops there), which is why
`min_games` is at most 65535.

**Guarantee:** for every (tier, key), estimate ≥ min(true visits, `min_games`). So every
(tier, key) with at least `min_games` visits is a candidate whatever the sketch's size, and the
size sets only how many false positives pass two counts and discards. The property test
`VisitSketch never underestimates below its saturation` checks it over random streams of visits,
sizes and saturations on both sides of 255, so at both widths; `run_build gives the same dump on
two runs, any thread count and a tiny sketch`
checks a 1 KiB sketch gives the same database. Saturating increments commute, so the final
counters do not depend on the order of the adds or the number of threads.

The default size is the least power of two of at least twice the compressed input, within 1 MiB
and 1 GiB: 64 MiB for a 2013 month, 1 GiB for a recent one; `--sketch-mib` overrides it, up to
16 GiB, and a size the sketch cannot take is an error value (`VisitSketch::create`).

**Sizing.** A row holds `width` = bytes / 4 counters at 8 bits and bytes / 8 at 16 bits, so each
counter takes, on average, total visits / width of the visits; that load must stay well under
`min_games`, or most cells saturate and pass two counts nearly every position. One recent month
(on the order of a hundred million games, some thirty visits each) fits the 1 GiB default: at the
default `min_games`, width 2^28 and about eleven visits per counter. Twelve recent months do not:
they are projected at about 1.3 × 10^10 distinct (tier, key) pairs, so at least that many visits,
which at 1 GiB of 8-bit counters (width 2^28, about 2.7 × 10^8) is about fifty per counter, half
of `min_games`, and many cells saturate; at 8 GiB (width 2^31) it is about six. A twelve-month
build at the default `min_games` therefore passes `--sketch-mib 8192`; with `min_games` above
255 the counters are 16-bit, and the same width takes `--sketch-mib 16384`. If the machine cannot
hold that, the further option is a per-depth frontier pass (`PLAN.md` §12, item 6).

On the 2013-01 month, which has no false positives at the default 64 MiB, a 1 MiB sketch passes
774 candidates for 660 survivors with 16-bit counters and 699 with 8-bit ones; the database is
the same.

## Pass two's counts

Each worker scans a batch into its own `BatchCounts`, which merges the batch's repeated nodes
(the start position appears in every game) and is flushed, at the end of the batch, into the
shared `Aggregate`: 256 shards by the key's top byte, a mutex each, each a map from (key, tier)
to the visits by result and a small vector of moves (`MoveCode`, child, counts by result). A
worker asks the aggregate whether a candidate key has a FEN once per batch, and records the FEN
from the scanner's `Position` when it has none; a FEN asked for in a game that is then skipped
is asked for again at the key's next visit. Counters are 32-bit; one that would pass 2^32 − 1
stops there and marks the aggregate, and `run_build` returns the error after pass two, so a
count never wraps (twelve months stay far below it). Sums commute, and the survivors
are sorted before they are written, so the output does not depend on the thread count.

## The evaluation merge

The file is streamed once, cut into batches of whole lines read by the workers; a line's order
is its batch's sequence number and its place in the batch. Only the `"fen"` field is read until
the key turns out to survive (`Position::from_fen`, which normalizes en passant as §5.1 says); a
surviving line is parsed with nlohmann::json, its evaluation chosen by §9's rule 2, and its
first pv's score and first move kept. The move is played with `Position::play_uci` as given and,
when that fails, as the king's two-square castling move for `e1h1`, `e1a1`, `e8h8`, `e8a8`, kept
only when that plays as castling; `best_move` is the played move's UCI. Each worker keeps the
best line per key (greater depth, then knodes, then the earlier line), and the workers' choices
are merged by the same rule, so the result does not depend on the thread count. A line without a
`"fen"` field, with an unreadable FEN, or with malformed JSON for a surviving key is counted as
unreadable; malformed JSON in a non-surviving line is never parsed, so it is not counted.

## Badges and the writer

`compute_badges` walks the survivors grouped by key: per node, the sum of its moves' totals; per
move, the integer share tests of §7.2, the tier-6 node of the same key for Book, and the loss
from the parent's and the child's evaluations through core's `evaluation_value` and
`evaluation_loss` (`score.hpp`), the rule the recommendation score reads; the thresholds are
§7.1's values, the constants of `badges.hpp` and core's `book_margin_cp`.

`write_database` opens the new file, sets `journal_mode=OFF` and `synchronous=OFF`, runs core's
`openings_schema_sql()`, and inserts meta, then positions, nodes and moves in the key order the
file stores (signed), 50,000 rows per transaction.

## The report

`name<TAB>value` lines: the output; one `input` line per month file in month order, then `eval` and
`names`, the evaluation file and the names directory as given or `none`, which is what
`tools/verify_db.sh --rebuild` builds again from (**Release database**); every meta row, then
`threads`, `sketch_bytes`, `sketch_width`, `candidates`, `nodes` and `nodes_tier_0` to
`nodes_tier_6`, `moves`, `positions`, `eval_lines`, `eval_unreadable`, `eval_matched` and
`eval_unusable` when an evaluation file was read, `positions_evaluated`, `positions_named`,
`name_collisions`, `name_failures`, `games_read`, `pass_one_seconds`, `pass_two_seconds`,
`total_seconds`, `games_per_second` (games read over the whole build's wall time) and
`peak_rss_bytes` (`getrusage`).

## The sample corpus

`pipeline/tests/sample/` holds the first 2,000 games of the 2013-01 month, recompressed and
named as that month; `eval/lichess_db_eval.jsonl.zst`, the lines of the evaluation database's
first five million whose FEN is a position surviving in the sample; and `golden.dump`, the
canonical dump of the sample built at `--min-games 2 --max-ply 20 --data-version 0` with that
slice and the committed names. `tools/run_corpus.sh` (the gate's corpus step) rebuilds it, dumps
it with `chess-coach-cli dump` and compares byte for byte; `build_test.cpp` does the same
in-process through `OpeningStore::dump`. `tools/make_sample.sh <data-dir>` regenerates all three
files; running it is tier A, under a brief that says so.

## Release database

A published `openings.db` is built and checked by two scripts, not by hand; `data/release/README.md`
says how a release uses them and records each published file.

`tools/build_release_db.sh <data-dir> <out.db> --data-version N [--months YYYY-MM,...]
[--threads T] [--sketch-mib M] [--min-games G] [--max-ply P]` builds from the month files
`lichess_db_standard_rated_YYYY-MM.pgn.zst` in `<data-dir>` (the months `--months` names, or every
one there, ascending), the evaluation database `lichess_db_eval.jsonl.zst` beside them and the
committed `data/chess-openings`, at `min_games` 100 and `max_ply` 30 unless told otherwise
(`docs/database.md` §8). It refuses a month named twice, a named month without its file, a
missing evaluation file and an existing output or report before building anything; it builds
the `release` preset's `chess-coach-pipeline`, runs it with `--report <out.db>.report.txt`, and
prints the report's key lines and the elapsed time. Without `--sketch-mib` it sizes the sketch by
the rule of **The sketch**: it estimates the visits at one per ten compressed bytes (a recent
month holds about a hundred million games of some thirty visits in about 31 GB; older months,
with fewer bytes per game, hold more per byte) and takes the smallest power of two from 1 MiB
whose width keeps them at `min_games` / 16 per counter or fewer (about six at the default),
up to 16 GiB. For one recent month that is 2 GiB (width 2^29, about six per counter); twelve
recent months would need more than the pipeline's 16 GiB, which the script then takes and says
the prediction is above the target. A trimmed month (**Trim**) holds about ten times the visits
per compressed byte, so the estimate undersizes the sketch for one: pass `--sketch-mib`, sized
for the months untrimmed. A build of recent months holds every core for hours, so it
is the coordinator's or the owner's on a quiet machine, never a lane's (`CLAUDE.md`, **Lanes**).

`tools/verify_db.sh <db> [--rebuild]` checks a built file, one line per check: sqlite3's
`PRAGMA integrity_check` and the store opening it (`chess-coach-cli stats`); every `meta` key of
§6.1, `schema_version` 1, the months well formed and ascending, the tiers as §4 states, and the
`admitted_tier_` and `skipped_` keys summing to `games_admitted` and `games_skipped`; the
invariants of §6.4 by SQL (every node at least `min_games` with its columns summing to its
games, every move with a node and a total of at least one, every node's moves summing to at
most the node, column by column, and the start position's games at a tier at least that tier's
admitted games when those reach `min_games`) and §6.2's rule that `positions` holds exactly the
keys with a node; the canonical dump's sha256, written to `<db>.sha256` in shasum's format for a
file named `<db>.dump`; and the file's size against the delivery rule (`PLAN.md` §12 item 2,
§8 of the contract): under 500 MB it is bundled, from 500 MB to 1 GB it is the owner's call, over
1 GB it is a separate download, and over 2 GB it is also too large for a GitHub release asset.
With `--rebuild` it builds the file again from the inputs and knobs its report names (the
report's `input`, `eval` and `names` lines and `meta`'s `min_games`, `max_ply` and
`data_version`) into `<db>.rebuild.db` and compares the two dumps byte for byte, the
reproducibility criterion of §11; the rebuild is deleted when they are equal and kept when they
differ. It needs `sqlite3` on the `PATH` and builds the `release` preset's CLI and, for
`--rebuild`, the pipeline.

On the sample corpus, built through a scratch data directory holding links to the sample's
month file and evaluation file (`--months 2013-01 --min-games 2 --max-ply 20 --data-version 0`),
the script's database dumps byte-equal to `golden.dump`, and `verify_db.sh --rebuild` passes
every check and reports the rebuild equal; a copy with a wrong `games_admitted`, a deleted node
or a node under `min_games` fails its check, and a copy with one name changed fails the rebuild.

## Benchmark

`pipeline_bench_test` (label `bench`, the release preset) builds the sample in-process at the
golden knobs with four threads and fails past its budget of 1 s. That is a smoke budget, far
above the time measured on the development machine (an M-series Mac, ten cores, under the load
of other lanes: 0.035 to 0.046 s, about 50,000 games per second, 15 MB peak resident), so that
slower CI runners pass; a regression in throughput is read from the full-month numbers. The full
2013-01 month (121,332 games, release preset, default knobs) measured on the same machine under
the same load:

| Threads | Seconds | Games per second | Peak resident |
|---|---|---|---|
| 1 | 4.4 | 27,000 | 102 MB |
| 2 | 2.5 | 49,000 | 115 MB |
| 4 | 1.4 | 84,000 | 120 MB |
| 8 | 0.8 | 158,000 | 113 MB |

## Tests

`pipeline/tests/`: `zstd_istream_test` (a known stream, concatenated frames, truncated, missing,
empty and corrupt files), `chunker_test` and `chunker_property_test`, `game_filter_test` (every
§3 row and the check order), `game_scanner_test` (Lichess's comments, NAGs and glyphs, every §3
row through the parser, a malformed header, a bad move before and past the cap, games without
moves, a game ending before its moves, a parser error before and past the cap and after a failed
header check, keys equal to core's replay), `move_code_test`, `sketch_test` and
`sketch_property_test`, `aggregate_test` (a count stopping at 2^32 − 1 and the overflow
reported), `work_pool_test`, `badges_test` (thresholds at their boundaries, Book's
tier-6 zero sum, the loss from both sides and with mates), `eval_merge_test` (the tie-breaks,
both castling forms, the no-fallback rule, unreadable and non-surviving lines, en passant
normalization, a file read by one and three threads), `options_test`, `report_test`, and
`build_test` on the sample: the golden dump, two runs, one and four threads and a tiny sketch
alike, the §6.4 invariants at `min_games` 1 and 2, and the refusals; `zstd_ostream_test` (a
stream larger than the buffers with worker threads read back, an empty stream, a file that
cannot be created); `trim_test` (the names, the movetext filter's comments, NAGs, `1...`
numbers, cap and result tokens, the games kept with their tags and the Chess960 game then
refused by the scanner, the evaluation chosen by depth, knodes and order with a mate score and
read back as the same candidate, the lines passed through, the refusals) and on the sample: the
trimmed month builds the untrimmed month's dump but for the skip counts, and the trimmed
evaluation file builds `golden.dump` exactly; and `pipeline_exit_status` (`exit_status.cmake`),
the executable's exit statuses, `trim`'s among them.

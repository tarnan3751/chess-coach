# The CLI

`chess-coach-cli` queries the opening database (`openings.db`, `docs/database.md` §6) from a
terminal: the canonical dump, the database's counts, a position's moves at a tier, and opening
names; the engine, through the engine cache of a data directory's `profiles.db` (§12); and the
profiles, their repertoires and their drill cards in that file. It is
how a database is inspected and verified, and its output is plain text, without colour, stable
from run to run, so that it can be golden-tested (the engine's lines excepted). It reads the
database only through `core`'s `OpeningStore`, opened read-only, plays moves only through
`core`'s `Position`, runs the engine only through `core`'s `Engine` and reads and writes
`profiles.db` only through `core`'s `ProfileStore`.

## Commands

```
chess-coach-cli dump    --db <file>
chess-coach-cli stats   --db <file>
chess-coach-cli explore --db <file> --tier <0-6> [--fen "<fen>" | --moves "<moves>"] [--score]
                        [--engine --data-dir <dir> [--lines N] [--seconds S] [--stockfish <path>]
                         [--threads T] [--hash M]]
chess-coach-cli name    --db <file> (--fen "<fen>" | --moves "<moves>")
chess-coach-cli eval    --data-dir <dir> [--fen "<fen>" | --moves "<moves>"] [--db <file>]
                        [--lines N] [--seconds S] [--min-depth D] [--stockfish <path>]
                        [--threads T] [--hash M]
chess-coach-cli spar    --db <file> --data-dir <dir> --tier <0-6> --colour white|black
                        [--seed N] [--end-move M] [--profile <name>] [--script "<moves>"]
                        [--seconds S] [--stockfish <path>] [--threads T] [--hash M]
chess-coach-cli spar    --data-dir <dir> [--profile <name>] --list
chess-coach-cli spar    --db <file> --data-dir <dir> [--profile <name>] --replay <id>
chess-coach-cli repertoire --data-dir <dir> --colour white|black [--profile <name>] [--db <file>]
                        add --moves "<moves>" | remove --moves "<moves>" | list
                        | coverage --tier <0-6> [--depth P] [--limit N]
                        | gaps --tier <0-6> [--depth P] [--limit N] | import <file.pgn>
chess-coach-cli drill   --data-dir <dir> --colour white|black [--profile <name>] [--at <seconds>]
                        list | review <card> again|hard|good|easy | sync
chess-coach-cli profile --data-dir <dir> list | create <name> --tier <0-6> | remove <name>
                        | export <file> | import <file>
chess-coach-cli --version | --help | <command> --help
```

`dump`, `stats`, `explore`, `name` and `spar` but `spar --list` take `--db`, the opening database,
and require it; `eval`, `spar`, `repertoire`, `drill` and `profile` require `--data-dir`, `eval`
takes `--db` as an option, and `repertoire coverage` and `repertoire gaps` need it. The options of
`repertoire`, `drill` and `profile` may come before or after their subcommand. A position is given by
`--fen`, a FEN of four or six fields, normalized as `docs/database.md` §5.1 states; or by
`--moves`, moves from the start position separated by spaces, each in UCI (`e2e4`, `e7e8q`,
`e1g1`) or SAN (`e4`, `Nf3`, `O-O`, `exd5`), which may be mixed; without either, `explore` and
`eval` use the start position. `--fen` and `--moves` exclude each other.

### dump

The canonical dump of `docs/database.md` §11, written by `core`'s store, and nothing else: the
same database gives the same bytes.

### stats

The `meta` rows as `key: value` lines, in the dump's order and with the dump's keys (every key
but `built_at` and `pipeline_version`, which change with every build); then one line per tier
with its `nodes` rows, its `moves` rows and the games at the start position at that tier; then
the positions that carry a name and the positions that carry an evaluation:

```
admitted_tier_0: 20
...
tiers: 1200,1400,1600,1800,2000,2200
tier 0 (<1200): 2 nodes, 3 moves, 20 games at the start
...
tier 6 (2200+): 2 nodes, 3 moves, 100 games at the start
named positions: 7
evaluated positions: 8
```

The counts are the store's (`OpeningStore::counts`), counted in SQL.

### explore

A position at one tier, here 1. e4 c5 at tier 3 of the tests' fixture database:

```
fen: rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -
key: 12927358150810715380
name: B20 Sicilian Defense
eval: cp 35 depth 38 best Nf3 g1f3
Nf3 g1f3 BP 150 47% 7% 47% 60% no
Nc3 b1c3 P 100 40% 10% 50% 40% no
```

- `fen` is the normalized FEN and `key` the position's key in unsigned decimal (§5.2).
- `name` is the ECO code and name of the deepest named position along `--moves` (§10), or the
  position's own name for `--fen` and the start position; the line is left out when there is
  none.
- `eval` is the stored evaluation, left out when there is none: `cp <n>` or `mate <n>` from
  white's side (§6.2), the depth, and the best move in SAN and UCI.
- Then one line per move played from the position at the tier, the most played first and equal
  totals by move text: the move in SAN and in UCI; its badges as the letters `B` (Book), `P`
  (Popular) and `D` (Dubious), in that order, or `-` for none (§7.3); its total; the shares of
  its plays that white won, that were drawn and that black won; its share of the plays from the
  position; and `yes` when its child is in book at the tier (has a `nodes` row there, §7.3),
  `no` when the move leads off book. A position with a `nodes` row and no moves (visited only
  at the depth cap, or where its games ended) prints no move lines.
- With `--score`, each move line gains six columns, the recommendation score's components
  (`docs/reference/core.md`, **The recommendation score**): `yes` or `no` for sound; the loss in
  centipawns, or `-` when it is undefined; the expected score; the replies needed; the penalty;
  and the score, the three fractions with three decimals. The lines keep their order:
  ```
  Nf3 g1f3 P 22 36% 5% 59% 45% yes yes 12 0.452 3 0.040 0.412
  ```
- With `--engine`, the line `engine: depth <d> <cp n | mate n> <pv>` follows the `eval` line (or
  the header, when there is no stored evaluation): the engine's first line, through the engine
  cache exactly as `eval` goes through it (below), with `--lines` 1, `--seconds` 1.0 and a
  minimum depth of 12 unless given, and the same engine options. `--data-dir` is required with
  `--engine`; without `--engine` nothing changes. The output is written only once the whole
  answer is known, so a failing engine, or a database that fails part way (a stored move that is
  not legal in its position, a `--score` without the position's `positions` row), leaves stdout
  empty.
- A position that is off book at the tier (no `nodes` row there) prints, after the lines above,
  the one line `off book at tier <t> (<band>)`, and exits 0: an absent position is an answer,
  not an error.

Percentages are whole numbers rounded half up, each computed on its own, so a move's three
result shares may sum to 99% or 101%.

### name

The ECO code and name of the deepest named position along `--moves`, or of the `--fen`
position, as `<eco> <name>`; `unnamed` when no position along the way has a name. One of
`--fen` and `--moves` is required.

### eval

A position's engine lines, from the engine cache of `<dir>/profiles.db` when it holds them deep
enough, else from a new search, which is then stored. Here the start position on the sample
corpus's database, with Stockfish 19:

```
fen: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -
key: 10026961946044190907
stored: cp 19 depth 75 best e4
engine: Stockfish 19
1: depth 17 cp 25 d4 d5 Nf3 Nf6 c4 dxc4 e3 e6
2: depth 16 cp 27 e4 c5 Nc3 d6 Nf3 Nf6 Bc4 e6
3: depth 16 cp 26 Nf3 c5 e4 d6 Nc3 Nf6 Bc4 e6
cached: no
```

and the same command again, served from the cache, prints the same lines with `cached: yes`.
Each line is the engine's last exact report for that multipv, so the lines are not always in
score order (`docs/reference/core.md`, **The engine**).

- `--data-dir` is required: `profiles.db` is opened there, and created, with the directory, when
  it is missing, which makes `eval` the CLI's one command that writes. It writes nothing else
  there.
- `fen` and `key` as `explore` prints them. With `--db`, when the database holds an evaluation of
  the position, `stored: <cp n | mate n> depth <d> best <SAN>` follows, from white's side as
  §6.2 stores it.
- `engine` is the engine's `id name`, as the cache recorded it for a cached answer. Then one line
  per line of the search, by multipv: `<n>: depth <d> <cp n | mate n> <pv>`, the score from the
  side to move as UCI gives it and the cache stores it (§12.2), unlike the `stored` line, and the
  first eight moves of the pv in SAN. A pv move the position does not allow (a fake engine's) is
  printed in UCI and ends the pv.
- `cached: yes` when the cache answered: it held rows for the position's key and `--lines` (3 by
  default) whose least depth is at least `--min-depth` (12 by default). Otherwise the engine was
  started and searched for `--seconds` (1.0; above 0 and at most 3600, as `core`'s engine
  accepts), the result went through the cache, which keeps the deeper of the stored and the new
  rows (§12.2), and the line is `cached: no`. A cached answer never starts or even looks for the
  engine.
- The engine is `--stockfish`, else the file the environment variable `CHESS_COACH_STOCKFISH`
  names, else `stockfish` on the `PATH`; it runs with `--threads` (1) and `--hash` (16 MB).
  Nothing is written to stdout unless the whole command succeeds.

### spar

A sparring session against the coach at a tier (`docs/reference/core.md`, **The spar session**),
saved under a profile (`--profile`, `Default` unless given) in the data directory's
`profiles.db` (`docs/database.md` §12.3):

```
seed: 1
coach: e4 (e2e4)
you: e6 (e7e6)
coach: Dubious move: e6 (e7e6), badges PD, loss -
  better: e5 (e7e5) P 300 games 47% 10% 43%
  better: c5 (c7c5) BP 250 games 44% 8% 48%
out of book at tier 3 (1600-1799): the engine plays from here
coach: d4 (d2d4)
...
session ended at move 3
summary: 5 moves, 1 interruptions, reached off book, result none
moves: 1. e4 e6 2. d4 a6 3. Nd2
saved: session 1 under Default
```

- `--tier` and `--colour` (the user's) are required for a new session. The seed (`--seed`, else
  the clock's seconds) is printed first; with the same seed, tier, colour and moves, the coach's
  book replies are the same.
- The user's moves come from `--script`, moves separated by spaces, or else from stdin, one at
  a time after a prompt that is the game so far in SAN (`1. e4 > `). A move is UCI or SAN; a
  move that is not legal is refused (`not a legal move here: <move>`) and asked again, and in a
  script it is an error. The session stops early when the script or the input ends.
- `you:` and `coach:` lines give each move in SAN and UCI. While in book the coach's replies
  come from the sampler; an interruption (a Dubious move, or one out of book) gives the reason,
  the move, its badges, its loss in centipawns or `-`, and the better choices with their badges,
  games and white, draw and black shares. The line `out of book at tier ...` marks where the
  engine takes over: Stockfish at limited strength (core's `EngineReplyProvider`, `--seconds`
  per move, 0.5 by default), started only then, so a session that stays in book needs no engine.
- The session ends when the user is to move at `--end-move` (25), or at mate or stalemate, then
  prints its summary and is saved: `saved: session <id> under <profile>`. A session stopped
  early is saved too, as stopped.
- `--list` prints the profile's saved sessions, `id date tier colour moves interruptions result`
  (the date of its start, UTC; `-` for no result).
- `--replay <id>` plays a saved session again from its seed and moves, with the end move and
  sampler minimum it was played with (`docs/database.md` §12.3), needing no engine (the engine's
  moves are replayed as recorded), and prints its interruptions and its summary. A record the
  session does not reproduce is an error, and so is a database of another `data_version` than the
  session played against, whose book replies may differ.

### repertoire

A profile's repertoire for one colour (`--profile`, `Default` unless given; `--colour`, required),
in the data directory's `profiles.db` (`docs/database.md` §12.4; `docs/reference/core.md`, **The
profile store**). Every choice made also makes or resets its drill card.

- `add --moves "<moves>"` makes, at each position along the line where the colour is to move, the
  line's move its choice, replacing another choice there, and prints `added: N`, `changed: N` and
  `unchanged: N`. Moves are UCI or SAN as for `--moves` above.
- `remove --moves "<moves>"` removes the choice at the position the line reaches, which must be
  the colour's to move, and only it (the choices after it stay), with its card; it prints
  `removed: 1`, or `removed: 0` when there was none.
- `list` prints one line per choice, by key: the key in unsigned decimal, the move in UCI and in
  SAN, and the position's FEN.
- `coverage --tier T` prints `compute_coverage` (`docs/reference/core.md`, **Repertoire
  coverage**) on `--db` at the tier, to `--depth` plies (20): the share of the start position's
  games the repertoire answers, the positions visited, the number of gaps, and the largest gaps,
  `--limit` of them (10), each as its share, its first line in SAN and its FEN:

```
covered: 8.3%
visited: 5
gaps: 2
50.0% e4 e5 | rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -
41.7% e4 c5 | rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -
```

- `gaps --tier T` prints only the gap lines.
- `import <file.pgn>` reads the file's games and variations (`docs/reference/core.md`, **PGN
  import**) and adds their choices for the colour where the repertoire has none, keeping its own
  where it has one: `games`, `variations`, `choices` (the file's, one per position), `added`,
  `unchanged`, `conflicts in the file`, `conflicts with the repertoire` and `unparsable`, one
  `name: N` line each.

### drill

The cards of a profile's repertoire for one colour, scheduled by SM-2 (`docs/reference/core.md`,
**The scheduler**). `--at` is the time in Unix seconds, the clock's when absent.

- `list` prints the cards due at that time, by due time and then key: the card's id, the key, the
  move in UCI and in SAN, `due <seconds>` and the position's FEN.
- `review <card> <grade>` grades a card of that repertoire `again`, `hard`, `good` or `easy` at
  that time and prints its new state: `card <id>: repetitions R interval D ease E due T`, the ease
  in thousandths.
- `sync` repairs the cards to match the repertoire and prints `created`, `reset` and `deleted`;
  the store keeps them in step, so it prints zeros unless the file was changed by other means.

### profile

The profiles of the data directory (§12.2, §12.5).

- `list` prints `id name tier T (label)` per profile, by id; `create <name> --tier T` prints
  `created: id name tier T`; `remove <name>` removes the profile with its sessions, repertoires and
  cards (the last profile stays).
- `export <file>` writes a copy of `profiles.db`, which must exist, to a file that must not; `import <file>`
  replaces `profiles.db` with an exported file of this version or an earlier one, migrated, and
  keeps the replaced one as `profiles.db.previous`. Neither may run while the app has the data
  directory open.

## Exit statuses

| Status | When |
|---|---|
| 0 | The command ran, including an off-book or unnamed position; `--version`, `--help`, or no arguments (which print the help) |
| 2 | The command line is rejected (an unknown command or option, a missing `--db`, `--tier` or `--data-dir`, `explore --engine` without `--data-dir`, a tier that is not a number, `--fen` with `--moves`), or a command fails: a database that does not open, a tier outside 0 to 6, a FEN that does not parse, a move that is neither UCI nor SAN or is not legal where it is played, `name` without a position, a `profiles.db` that does not open, `--lines` below 1 or `--seconds` not above 0, no engine to be found on a cache miss, an engine that does not start, dies or does not answer in time; for `spar`, a missing `--tier` or `--colour`, a colour other than `white` or `black`, `--list` with `--replay` or `--script`, no profile of that name, no saved session of that id, an illegal move in a script, a record that does not replay or a database of another `data_version` for `--replay`, a session that cannot be saved; for `repertoire`, `drill` and `profile`, no subcommand, a colour other than `white` or `black`, no profile of that name, a line that ends where the other colour is to move for `remove`, `coverage` or `gaps` without `--db`, a PGN file that cannot be read or has a broken header, a grade other than the four, a card that is not the repertoire's, a duplicate or empty profile name, the last profile removed, an export from a data directory without `profiles.db` or to a file that exists, and an import of a file that is not a `profiles.db`, is newer or cannot be migrated. One line on stderr, `chess-coach-cli: <reason>`, and nothing on stdout |
| 1 | An unexpected exception reached `main`; its message is printed on stderr |

## Code

`cli/src/main.cpp` only wires CLI11 to the commands. Each command is a function over its parsed
options and an `std::ostream`
(`cli/src/commands/{dump,stats,explore,name,eval,spar,repertoire,drill,profiles}`), returning a
`CommandError` as a value; the commands over `profiles.db` also take the clock, so tests inject
it. `commands/common` opens the store, resolves `--fen` or `--moves` to a position, the keys along
the way and the moves in UCI, finds the deepest name and rounds percentages;
`commands/engine_common` opens the profile store and words its errors, finds the engine binary,
searches through the cache (a lookup, and on a miss the engine started, searched and the result
stored) and writes an engine line with its pv in SAN; `commands/profiles` opens a profile by name
and reads a colour. The commands are the `cli_commands` library, which the executable and the
tests link.

## Tests

- `cli_commands_test` (labels `cli;unit`) runs every command on a fixture database
  (`cli/tests/fixture.cpp`) that the test writes into `core`'s schema in a temporary directory,
  a handful of positions, nodes, moves, names and evaluations whose keys come from `Position`,
  and compares the whole output with the expected text: `--moves` in SAN and in UCI, two move
  orders reaching one position, badges, a mate score, a position without moves, an off-book
  position, the `--score` columns with their numbers worked out by hand, and every error a
  command returns.
- `cli_engine_commands_test` (labels `cli;unit`) runs `eval` and `explore --engine` against
  `core`'s fake engine through `cli/tests/fake-stockfish.sh`, which execs the `fake-uci` the
  build names in `CHESSCOACH_FAKE_UCI`: a miss searches and stores; a repeat is served from the
  cache with the binary pointed at a path that does not exist and the fake's log never written; a
  minimum depth above the cached one searches again; a line count is its own entry; `--db` adds
  the stored evaluation; a missing engine, `--lines 0` and a bad move are errors; the data
  directory holds `profiles.db` and nothing else. The engine's lines vary, so there are no golden
  queries for them.
- `cli_spar_test` (labels `cli;unit`) runs `spar` on the fixture database with `core`'s fake
  engine answering the first legal move (`cli/tests/fake-stockfish-legal.sh`, the fake's `legal`
  script): a scripted session meets a Dubious interruption with its better choices, leaves the
  book, ends at its move and is saved, listed and replayed to the same summary; moves read from
  an input stream are refused when illegal and the session stops when the input ends; the errors
  come before any file is opened or created.
- `cli_repertoire_commands_test` (labels `cli;unit`) runs `repertoire`, `drill` and `profile` on
  scratch data directories with the clock injected: a line's choices added, replaced (UCI and SAN
  mixed), listed by key and removed one position at a time with its card; `coverage` and `gaps` on
  the fixture database, checked by hand and against `compute_coverage`; a PGN with a variation, a
  conflict in the file, one with the repertoire and an unparsable move; the drill flow (sync, list,
  review good, list again with the card's due date moved and the card no longer due); the
  profiles listed, created, exported, imported into another directory (whose file becomes
  `profiles.db.previous`) and removed; and each command's errors.
- `cli_exit_status` (labels `cli;unit`) runs the executable on rejected command lines and
  failing commands and checks the status 2, the one line on stderr and the empty stdout, and
  status 0 for `--version`, `--help` and no arguments.
- `cli_write_fixture <path>` writes the fixture database to a file, for driving the CLI by hand;
  it is built with the tests and is not one.

## Golden queries

`cli/tests/run_golden.sh [--update] <db> [<chess-coach-cli>]` runs every query under
`cli/tests/golden/` against a database and compares the output byte for byte. A query is
`<name>.args`, the command line without `--db`, one argument per line; its expected output is
`<name>.out`; an optional `<name>.head` holds a line count and limits the comparison to the
output's first lines. The runner appends `--db <db>`, uses `build/dev/cli/chess-coach-cli` unless
given another binary, prints a diff for every query that differs, and exits 1 when any does.
`--update` rewrites the `.out` files from the current output instead.

The queries: the dump's first 30 lines, `stats`, `explore` of the start position at tiers 0 and
6, `explore` after 1. e4 c5 at tier 3, with and without `--score`, and `name` of an open Sicilian
line. Their expected output
belongs to the sample corpus's database (`pipeline/tests/sample/`), which `tools/run_corpus.sh`
builds and runs the queries against; a change that regenerates the sample's golden dump
regenerates them too, with `--update` on that database (`build/dev/corpus/sample.db`).

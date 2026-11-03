# The CLI

`chess-coach-cli` queries the opening database (`openings.db`, `docs/database.md` §6) from a
terminal: the canonical dump, the database's counts, a position's moves at a tier, and opening
names. It is how a database is inspected and verified, and its output is plain text, without
colour, stable from run to run, so that it can be golden-tested. It reads the database only
through `core`'s `OpeningStore`, opened read-only, and plays moves only through `core`'s
`Position`.

## Commands

```
chess-coach-cli dump    --db <file>
chess-coach-cli stats   --db <file>
chess-coach-cli explore --db <file> --tier <0-6> [--fen "<fen>" | --moves "<moves>"] [--score]
chess-coach-cli name    --db <file> (--fen "<fen>" | --moves "<moves>")
chess-coach-cli --version | --help | <command> --help
```

Every command takes `--db`, the opening database, and requires it. A position is given by
`--fen`, a FEN of four or six fields, normalized as `docs/database.md` §5.1 states; or by
`--moves`, moves from the start position separated by spaces, each in UCI (`e2e4`, `e7e8q`,
`e1g1`) or SAN (`e4`, `Nf3`, `O-O`, `exd5`), which may be mixed; without either, `explore` uses
the start position. `--fen` and `--moves` exclude each other.

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
- A position that is off book at the tier (no `nodes` row there) prints, after the lines above,
  the one line `off book at tier <t> (<band>)`, and exits 0: an absent position is an answer,
  not an error.

Percentages are whole numbers rounded half up, each computed on its own, so a move's three
result shares may sum to 99% or 101%.

### name

The ECO code and name of the deepest named position along `--moves`, or of the `--fen`
position, as `<eco> <name>`; `unnamed` when no position along the way has a name. One of
`--fen` and `--moves` is required.

## Exit statuses

| Status | When |
|---|---|
| 0 | The command ran, including an off-book or unnamed position; `--version`, `--help`, or no arguments (which print the help) |
| 2 | The command line is rejected (an unknown command or option, a missing `--db` or `--tier`, a tier that is not a number, `--fen` with `--moves`), or a command fails: a database that does not open, a tier outside 0 to 6, a FEN that does not parse, a move that is neither UCI nor SAN or is not legal where it is played, `name` without a position. One line on stderr, `chess-coach-cli: <reason>`, and nothing on stdout |
| 1 | An unexpected exception reached `main`; its message is printed on stderr |

## Code

`cli/src/main.cpp` only wires CLI11 to the commands. Each command is a function over its parsed
options and an `std::ostream` (`cli/src/commands/{dump,stats,explore,name}`), returning a
`CommandError` as a value; `commands/common` opens the store, resolves `--fen` or `--moves` to a
position and the keys along the way, finds the deepest name and rounds percentages. The commands
are the `cli_commands` library, which the executable and the tests link.

## Tests

- `cli_commands_test` (labels `cli;unit`) runs every command on a fixture database
  (`cli/tests/fixture.cpp`) that the test writes into `core`'s schema in a temporary directory,
  a handful of positions, nodes, moves, names and evaluations whose keys come from `Position`,
  and compares the whole output with the expected text: `--moves` in SAN and in UCI, two move
  orders reaching one position, badges, a mate score, a position without moves, an off-book
  position, the `--score` columns with their numbers worked out by hand, and every error a
  command returns.
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

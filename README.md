# chess-coach

An offline desktop chess opening coach. It carries a comprehensive opening database built from
the Lichess monthly game dumps, split into rating tiers so a 1200 and a 2200 each see what is
actually played and what actually works at their level, with every move badged as Book, Popular
or Dubious from top-tier popularity and engine evaluation. A local Stockfish evaluates anything
off book. Four screens make it a coach rather than a reference: Explore the tree, Spar against
replies sampled from your tier, build a Repertoire with coverage against what you will face, and
Drill it by spaced repetition. No account, no network. Built with C++20, Qt 6 Widgets, CMake and
vcpkg, SQLite. `chess-coach` is a working name.

## Status

Pre-alpha and private. The four modules (`core`, `pipeline`, `cli`, `app`) configure, build and
test on macOS and Linux; the ingestion pipeline builds the tiered database from the Lichess
dumps, the CLI queries it, and the app opens an empty window titled with its version. The next
milestones are the engine and the app's first screen.

## How it will work

- **The pipeline** (`chess-coach-pipeline`) streams one or more monthly Lichess dumps, keeps
  rated blitz, rapid and classical games, buckets each by the average of the two players'
  ratings into seven tiers, keys every position in the first thirty plies by a Zobrist hash so
  transpositions merge, counts exactly every position that reaches a hundred games at a tier
  while keeping its memory bounded, merges the Lichess evaluation database for every kept
  position, names positions from the Lichess opening list, and writes one SQLite file.
- **The app** (`chess-coach`) opens that file beside a bundled Stockfish. Explore shows the moves
  from any position at your tier with their results, badges, names and evaluation. Spar plays
  replies drawn from what people at your tier really play and interrupts when you go wrong.
  Repertoire holds your lines per colour and shows how much of what you will face they cover.
  Drill schedules the moves by spaced repetition.
- **The CLI** (`chess-coach-cli`) does the same queries from a terminal and is how the database
  is inspected and verified.

## Building and running

The build needs CMake 3.28 or newer, Ninja, a C++20 compiler (Apple clang 15 or newer, GCC 13
or newer), vcpkg, Qt 6.4 or newer with the Svg module, clang-format and clang-tidy, and
Stockfish for the engine tests. vcpkg builds every other dependency from the manifest
(`vcpkg.json`, pinned to a baseline in `vcpkg-configuration.json`); Qt always comes from the
system, never from vcpkg.

On macOS, with Homebrew:

```
brew install ninja clang-format llvm stockfish qt
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

On Ubuntu 24.04:

```
sudo apt-get install build-essential cmake git curl zip unzip tar pkg-config ninja-build \
  clang-format clang-tidy qt6-base-dev qt6-svg-dev libgl1-mesa-dev stockfish
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

The build finds vcpkg through `VCPKG_ROOT`, or at `~/vcpkg` when that is unset, and on macOS
finds Qt through `brew --prefix qt`. Configure and build only through a preset: `dev` (debug),
`gate` (optimised, under ASan and UBSan) or `release`. Each builds into `build/<preset>/`, and
the first configure compiles the vcpkg ports, which takes a few minutes once.

```
cmake --preset dev && cmake --build --preset dev       # configure and build
tools/run_tests.sh                                      # every test but the benchmarks
tools/gates.sh                                          # everything a change must pass
build/dev/pipeline/chess-coach-pipeline --version
build/dev/cli/chess-coach-cli --version
build/dev/app/chess-coach --data-dir <dir> --db <file>  # the app on a scratch data directory
```

Without `--data-dir` the app uses the platform's application data directory (on macOS
`~/Library/Application Support/chess-coach`). How the presets, the tests and the gate work is in
[docs/reference/testing-tools.md](docs/reference/testing-tools.md).

## The pipeline

`chess-coach-pipeline build` reads one or more monthly dumps, named as Lichess names them
(`lichess_db_standard_rated_YYYY-MM.pgn.zst`), and writes a new `openings.db`:

```
build/release/pipeline/chess-coach-pipeline build \
  --in lichess_db_standard_rated_2013-01.pgn.zst lichess_db_standard_rated_2013-02.pgn.zst \
  --eval lichess_db_eval.jsonl.zst --out openings.db
```

It streams each dump twice and never decompresses one to disk: the first pass counts position
visits per tier in a fixed-size count-min sketch, the second counts exactly only the positions
the sketch says can reach the threshold (a hundred games at a tier by default), so its memory is
the sketch plus the survivors. It then merges the evaluation database for the surviving
positions, names them from the Lichess opening list, and badges every move. The same inputs give
the same database whatever the thread count; the knobs, the report and the internals are in
[docs/reference/pipeline.md](docs/reference/pipeline.md), the data contract in
[docs/database.md](docs/database.md).

On the development machine (ten cores, release build, other builds running), the 2013-01 month
(121,332 games) builds at the default knobs in 4.4 s on one thread (27,000 games per second) and
0.8 s on eight (158,000 games per second), with a peak resident size of about 115 MB, of which
the sketch is 64 MiB. A recent month holds about eight hundred times as many games.

## The CLI

`chess-coach-cli` queries a built database from the terminal:

```
chess-coach-cli stats   --db openings.db                            # meta and counts per tier
chess-coach-cli explore --db openings.db --tier 3 --moves "e4 c5"   # the moves at a tier
chess-coach-cli name    --db openings.db --moves "e4 c5 Nf3 d6 d4"  # the opening's name
chess-coach-cli dump    --db openings.db                            # the canonical dump
```

Tiers are 0 (under 1200) to 6 (2200 and above); moves are UCI or SAN from the start position,
or `--fen` gives a position. [docs/reference/cli.md](docs/reference/cli.md) describes every
command and its output.

## Data

The opening database is built from data Lichess publishes under the Creative Commons CC0
licence: the [monthly game dumps and the evaluation database](https://database.lichess.org/) and
the [opening names](https://github.com/lichess-org/chess-openings). The downloads are not part of
the repository; the pipeline reads them from a directory you point it at. A small sample is
committed for the tests.

## Licence

MIT; see [LICENSE](LICENSE). Stockfish is distributed with the application as a separate program
under the GNU General Public License version 3; its licence text and a link to its source ship
beside it.

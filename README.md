# chess-coach

A desktop chess opening coach that works entirely offline. It runs on an opening database built from
the Lichess monthly game dumps and split into seven rating bands, so a 1200 player and a 2200 player
each see what people play at their own level and how it goes for them. Every move is tagged Book,
Popular or Dubious based on how often it's played (at your level and at the top level) and what the
engine thinks of it. Anything outside the database gets evaluated by a local copy of Stockfish.

There's no account and no network access. Written in C++20 with Qt 6 Widgets, CMake, vcpkg and
SQLite. Packaged for macOS; it also builds and passes its tests on Linux. MIT licensed. The name is
a placeholder.

## Status

5 November 2026: version 1.0.0 is done but not tagged yet. The pipeline builds the tiered database
from the Lichess dumps, the CLI can query it, and the app has all four screens (Explore, Practice,
Repertoire, Drill) plus one-file backup and restore. The first release database (data version 1,
from the rated games of October 2026) is built, verified reproducible and published, ready for the
release to bundle; see `data/release/README.md`. The macOS package and the release workflow are in
place.

## The four screens

The screenshots come from the UI tests, which run on a small test database with a stand-in engine,
so don't read anything into the numbers.

**Explore** shows what players at your rating do in a given position: how often each move gets
played, how those games ended, the move's badge (Book, Popular or Dubious) and how often the
strongest players choose it. Below that you get the opening's name, the engine evaluation and
Stockfish's top lines. Transpositions are merged, so two move orders that reach the same position
show the same data. This is the starting position for the 1600–1799 band, in light and dark:

![Explore, light theme: board, move table with badges and result bars, three engine lines](docs/images/explore-light.png)

![Explore, dark theme](docs/images/explore-dark.png)

**Practice** lets you play an opening against a coach. While you're inside the database, it replies
with what players at your level actually play. Once the game leaves known territory, Stockfish takes
over at reduced strength. If you play a Dubious move, or one that leaves the known lines, the coach
stops you and shows what would have been better. Every game is saved so you can replay it. Here it
has flagged 1. a3, which is Dubious for the 1800–1999 band in the test database. The move stayed on
the board and the coach has replied:

![Practice: board after 1. a3 e5, coach panel explaining why a3 loses, with e4 and d4 suggested](docs/images/spar-interruption.png)

**Repertoire** stores the move you want to play in each position, as White and as Black. You add
moves by playing them on the board or by importing a PGN. It tells you how much of what opponents at
your rating play you have an answer for, lists the positions you still need to cover, and ranks the
candidate moves wherever you haven't picked one yet. Here's a White repertoire of 1. a3 and 2. Nf3
after 1...e5, which covers everything opponents play at that rating in the test data:

![Repertoire: tree of 1. a3 e5 2. Nf3, full coverage against 1800–1999, the saved move and the candidates](docs/images/repertoire.png)

**Drill** turns each repertoire move into a flashcard and schedules it with spaced repetition
(SM-2). A position comes up and you play your move. If you get it right, you say how hard it was. If
you get it wrong, the board shows you the right move. Here a two-card drill has just ended, with the
second card missed and the answer played out:

![Drill: summary of two cards, one right and one wrong, with the answer Nf3 shown on the board](docs/images/drill.png)

Repertoire and Drill also work from the terminal through `chess-coach-cli repertoire` and
`chess-coach-cli drill` (see **The CLI** below).

## Install

On macOS, grab `chess-coach-<version>-macos-<arch>.dmg` from the releases page (`arm64` for Apple
silicon), open it and drag `chess-coach.app` onto the Applications shortcut. The app bundles Qt, a
Stockfish binary (with its licence and a link to its source in `Contents/Resources/engine/`), and
the licence of every component it ships (`Contents/Resources/legal/`, listed in `NOTICE`).

The app isn't signed or notarized, so Gatekeeper blocks it the first time. Either right-click (or
Control-click) it in Applications and choose Open, then Open again in the dialog. On macOS 15 and
later, try opening it once, then go to System Settings > Privacy & Security and click Open Anyway.
Or remove the quarantine flag from a terminal:

```
xattr -dr com.apple.quarantine /Applications/chess-coach.app
```

The opening database ships inside the app as `Contents/Resources/openings.db` and is picked up
automatically. The About dialog shows which data version you have. A newer database comes as a
separate file: use File > Import database… once, and the app copies it into its data directory as
`openings.db`, which takes precedence over the bundled one from then on. A database passed with
`--db` from a terminal beats both. The bundled Stockfish is used unless you point the app at a
different one in Edit > Settings, or there's a `stockfish` on your `PATH`.

Profiles, repertoires, settings and the engine cache live in the data directory:
`~/Library/Application Support/chess-coach` by default, or wherever `--data-dir` points. Deleting or
replacing the app leaves it alone.

## Building and running

You'll need CMake 3.28 or newer, Ninja, a C++20 compiler (Apple clang 15+ or GCC 13+), vcpkg, Qt 6.4
or newer with the Svg module, clang-format, clang-tidy, and Stockfish for the engine tests.
Everything else comes from vcpkg through the manifest (`vcpkg.json`, pinned to a baseline in
`vcpkg-configuration.json`). Qt always comes from the system, never from vcpkg, which would build it
from source for hours.

macOS with Homebrew:

```
brew install ninja clang-format llvm stockfish qt
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

Ubuntu 24.04:

```
sudo apt-get install build-essential cmake git curl zip unzip tar pkg-config ninja-build \
  clang-format clang-tidy qt6-base-dev qt6-svg-dev libgl1-mesa-dev stockfish
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

The build looks for vcpkg in `VCPKG_ROOT`, falling back to `~/vcpkg`. On macOS it finds Qt through
`brew --prefix qt`. Always configure through a preset: `dev` (debug), `gate` (optimised, with ASan
and UBSan) or `release`. Each one builds into `build/<preset>/`. The first configure compiles the
vcpkg ports, which takes a few minutes, but only once.

```
cmake --preset dev && cmake --build --preset dev       # configure and build
tools/run_tests.sh                                      # all tests except the benchmarks
tools/gates.sh                                          # everything a change has to pass
build/dev/pipeline/chess-coach-pipeline --version
build/dev/cli/chess-coach-cli --version
build/dev/app/chess-coach --data-dir <dir> --db <file>  # run the app on a scratch data directory
```

Without `--data-dir` the app uses the normal application data directory (`~/Library/Application
Support/chess-coach` on macOS). [docs/reference/testing-tools.md](docs/reference/testing-tools.md)
explains the presets, the tests and the gate.

## The pipeline

`chess-coach-pipeline build` takes one or more monthly dumps, named the way Lichess names them
(`lichess_db_standard_rated_YYYY-MM.pgn.zst`), and writes a fresh `openings.db`:

```
build/release/pipeline/chess-coach-pipeline build \
  --in lichess_db_standard_rated_2013-01.pgn.zst lichess_db_standard_rated_2013-02.pgn.zst \
  --eval lichess_db_eval.jsonl.zst --out openings.db
```

It keeps rated blitz, rapid and classical games, assigns each one to a tier by the average of the
two players' ratings, and keys every position in the first thirty plies by a Zobrist hash so
transpositions merge. Each dump is streamed twice and never decompressed to disk. The first pass
counts position visits per tier in a fixed-size count-min sketch. The second pass counts exactly,
but only for the positions the sketch says might reach the threshold (100 games in a tier by
default). So memory use is the sketch plus the survivors, not every position ever seen. After that
it merges in the Lichess evaluation database for the surviving positions, names them from the
Lichess opening list, and badges every move. The output is deterministic: same inputs, same
database, whatever the thread count. The knobs, the report and the internals are in
[docs/reference/pipeline.md](docs/reference/pipeline.md), and the data contract is
[docs/database.md](docs/database.md).

Some numbers from the development machine (ten cores, release build, other things running): the
2013-01 month (121,332 games) builds with default settings in 4.4 s on one thread (27,000 games/s)
and 0.8 s on eight (158,000 games/s), peaking at about 115 MB resident, 64 MiB of which is the
sketch. A recent month has roughly eight hundred times as many games.

## The CLI

`chess-coach-cli` does the same work from a terminal. It's also how you inspect and verify a
database. One line per command:

```
chess-coach-cli dump       --db openings.db                          # the canonical dump
chess-coach-cli stats      --db openings.db                          # meta, counts per tier
chess-coach-cli explore    --db openings.db --tier 3 --moves "e4 c5" # the moves at a tier
chess-coach-cli explore    --db openings.db --tier 3 --engine --data-dir <dir>  # and Stockfish's
chess-coach-cli name       --db openings.db --moves "e4 c5 Nf3 d6"   # the opening's name
chess-coach-cli eval       --data-dir <dir> --moves "e4 e5"          # Stockfish's lines
chess-coach-cli spar       --db openings.db --data-dir <dir> --tier 3 --colour white
chess-coach-cli repertoire --data-dir <dir> --colour white add --moves "e4 e5 Nf3"
chess-coach-cli drill      --data-dir <dir> --colour white list      # the cards due
chess-coach-cli profile    --data-dir <dir> list                     # the profiles
```

Tiers run from 0 (under 1200) to 6 (2200 and up). Moves are UCI or SAN from the starting position,
or pass `--fen` for a specific position. `spar` plays a session in the terminal and can list and
replay saved ones. `repertoire` can also remove moves, list them, measure coverage and gaps, and
import PGN. `drill` can also grade a card. `profile` can also create, remove, export and import.
[docs/reference/cli.md](docs/reference/cli.md) covers every command and its output.

## Data

The opening database is built from data Lichess publishes under CC0: the [monthly game dumps and the
evaluation database](https://database.lichess.org/) and the [opening
names](https://github.com/lichess-org/chess-openings). The downloads aren't in the repository; the
pipeline reads them from wherever you put them. A small sample is committed for the tests.

## Licences and attributions

chess-coach is MIT licensed; see [LICENSE](LICENSE). It uses Qt 6 (LGPL-3.0, dynamically linked),
chess-library, SQLite and SQLiteC++, zstd, {fmt}, spdlog, CLI11, toml++, nlohmann/json, tl::expected
and reproc. The macOS app also ships the libraries Qt depends on. Stockfish (GPL-3.0) runs as a
separate process over UCI and is never linked; the app bundles a binary along with its licence and a
link to the exact source it was built from. The piece images are Colin M.L. Burnett's cburnett set
from Wikimedia Commons (CC BY-SA 3.0). The data is Lichess's, under CC0. [NOTICE](NOTICE) lists
every component and its licence, and [legal/](legal/) has the full text of each.

## About this project

This is a personal project and I'm not taking pull requests. If something breaks, open an
issue and I'll have a look.

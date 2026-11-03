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

Pre-alpha and private. Nothing has been built yet: the repository holds the project's conventions
and tools. The first milestone is the skeleton (a configured, tested, packaged build on macOS and
Linux); the second is the ingestion pipeline that turns the Lichess dumps into the tiered
database.

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

There is nothing to build yet. Once the skeleton lands, the build needs CMake 3.28 or newer,
Ninja, a C++20 compiler, vcpkg (`VCPKG_ROOT` set), Qt 6 from Homebrew or apt, and Stockfish on
the `PATH` for the engine tests:

```
cmake --preset dev && cmake --build --preset dev     # configure and build
tools/run_tests.sh                                    # every test
tools/gates.sh                                        # everything a change must pass
build/dev/app/chess-coach --data-dir <dir> --db <file>   # the app on a scratch data directory
```

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

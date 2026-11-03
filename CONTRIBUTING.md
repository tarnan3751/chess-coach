# Contributing to chess-coach

Thank you for looking. chess-coach is an offline desktop chess opening coach: C++20, Qt 6
Widgets, CMake with vcpkg, SQLite, and a local Stockfish over UCI. It is developed by one person
with an agent team, under rules written down in the repository; this page is the short version
for anyone else. Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md).

Bug reports and feature requests are issues, from the templates. A change is a pull request;
for anything larger than a fix, open an issue first, so the design can be agreed before the code
is written.

## Where to read

- [README.md](README.md): what the project is, how to install it, build it and run it.
- [CLAUDE.md](CLAUDE.md): the map of the repository. Written for the agent team, it holds the
  conventions, the architecture rules and the commands that every change follows, whoever
  writes it.
- [docs/database.md](docs/database.md): the data contract (tiers, the position key, the schema
  of both databases, the badges, the pipeline's knobs). Read it before touching anything that
  reads or writes data.
- `docs/reference/`: one file per area (`core.md`, `pipeline.md`, `cli.md`, `app.md`,
  `testing-tools.md`), saying how the area works.
- The comment at the top of every source file, which says what the file holds and which rules
  it obeys.

## Building

The prerequisites for macOS and Ubuntu are in the README's **Building and running**: CMake 3.28
or newer, Ninja, a C++20 compiler, vcpkg, Qt 6 with the Svg module (from Homebrew or apt, never
from vcpkg), clang-format, clang-tidy and Stockfish. Configure and build only through a preset:

```
cmake --preset dev && cmake --build --preset dev
```

`dev` is the debug build, `gate` builds optimised under AddressSanitizer and
UndefinedBehaviorSanitizer with warnings as errors, and `release` is the optimised build for the
benchmarks and the package. Each builds into `build/<preset>/`.

Run the app only on a scratch data directory, `build/dev/app/chess-coach --data-dir <dir> --db
<file>`: without `--data-dir` it opens your real one. Build a small database from the committed
sample with `build/dev/pipeline/chess-coach-pipeline build --in
pipeline/tests/sample/lichess_db_standard_rated_2013-01.pgn.zst --out <file>`. The Lichess dumps
are never committed.

## The gate

`tools/gates.sh` is everything a change must pass: the format check, the `gate` build under the
sanitizers, every test (unit, property, the engine tests against Stockfish, the offscreen UI
tests), the benchmarks in `release`, clang-tidy, the module boundary and the golden corpora. Run
it before you open a pull request, and paste its last lines into it; CI runs the same gate on
macOS and Ubuntu. `tools/gates.sh --quick` (format, the `dev` build, the unit tests, the
boundary) is for checks along the way. `tools/run_tests.sh [<module> [<regex>]]` runs the tests,
`tools/ui_tests.sh` runs the UI tests and saves their screenshots, and `tools/format.sh` formats
the C++ with the committed `.clang-format`. How each works is in
[docs/reference/testing-tools.md](docs/reference/testing-tools.md).

A change comes with its tests: a visible change with a UI test, a change to the position key, the
parsing or the aggregation with its property tests, a change to the engine client driven against
the fake engine and Stockfish.

## Commits

Every commit is a [Conventional Commit](https://www.conventionalcommits.org/):
`type(scope): Summary`.

- `type` is one of `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`, `chore`,
  `style`, `revert`.
- `scope`, optional, is a module (`core`, `pipeline`, `cli`, `app`) or `data`, `tools`, `docs`,
  `ci`, `build`.
- The summary is an imperative sentence in sentence case, with no trailing period, at most 72
  characters: `fix(app): Keep the tier when the database changes`.
- The body is plain paragraphs saying what changed and why.
- **No trailers** of any kind: no `Signed-off-by`, no `Co-authored-by`. GitHub's web editor and
  its suggestions add them, so commit locally.
- One logical change per commit, refactors in their own commits before the change that needs
  them.

`CHANGELOG.md` is generated from these messages at each release, so the summary is what users
read.

## How changes land

`main` moves only by landed series: a series is the commits of one change, built on a branch
(the maintainer's are named `phase-N/short-topic`) and rebased onto the current `main`, which
`tools/land_series.sh` applies with `git am`, checking every message against the convention and
the resulting tree, and then runs the gate. History stays linear, with no merge commits. A pull
request is landed the same way: keep it rebased on `main`, keep each commit one logical change
with a conforming message, and expect review comments to be answered by amending the commits
rather than by adding fix-up commits on top.

## Docs are part of the change

If a change makes a statement in `CLAUDE.md`, `docs/database.md` or a file under
`docs/reference/` false (a schema column, a threshold, a command's output, a behaviour), the
same change corrects it. A change to the subject of `docs/database.md` updates the document; a
change to the profile database's schema adds a migration and a test that opens the previous
version's file.

## Rules that are not negotiable

- **The module boundary.** `pipeline`, `cli` and `app` depend on `core` and never on each other.
  No Qt header or target outside `app`. The gate checks it.
- **No network, ever.** No sockets, no HTTP library, no QtNetwork, no download step in the
  build, no telemetry. The pipeline reads files the user downloaded; the app reads the database
  it is given. The gate checks it.
- **The data rules.** Every position is keyed by the Zobrist hash of the normalized position,
  computed in `core`; chess rules come from chess-library through `core` and nowhere else; every
  statistic, badge and recommendation names its rating tier; the same inputs give the same
  outputs.
- **The engine is a process.** Stockfish runs as a separate program over UCI and is never
  linked.

## Dependencies

A new dependency is a decision for an issue first. C++ dependencies come through vcpkg in
manifest mode (`vcpkg.json`, pinned by the baseline in `vcpkg-configuration.json`; an overlay
port under `vcpkg/ports/` for one the registry lacks); Qt comes from the system's package
manager and never through vcpkg. The licence policy:

- Permissive licences (MIT, BSD, Apache, zlib, public domain, CC0) freely.
- LGPL only linked dynamically, with its text shipped, as Qt is.
- GPL only as a separate process, never linked, as Stockfish is.
- AGPL never: lila and lila-openingexplorer are read only as claims about behaviour, never as
  code to copy.

A dependency that ships gets its entry in `NOTICE` and its full licence text under `legal/`;
the macOS package refuses to build when a library it bundles has none.

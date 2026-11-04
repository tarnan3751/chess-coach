# chess-coach (working name)

An offline desktop chess opening coach. A tiered opening database built from the Lichess monthly
game dumps by our own ingestion pipeline (one tree of positions per rating tier, every move badged
Book, Popular or Dubious from top-tier popularity and engine evaluation), a local Stockfish for
anything off book, and a Qt Widgets app with four screens: Explore, Spar, Repertoire and Drill.
C++20, CMake with vcpkg, Qt 6, SQLite. No account and no network, ever. Solo developer with an
agent team.

This file is the **map**: what the project is, where each area lives, the commands, the
conventions and the workflow. The scope, the phases, the status, the risks and every recorded
decision are in `PLAN.md`, which lives at the root of the owner's main working tree (the
coordinator's checkout, whose path every brief names) and is **gitignored on purpose**, so a
clone does not carry it: read it from the main tree by that path. The data contract (tiers, the
position key, the schema, the badges, the knobs) is `docs/database.md`, written before the
pipeline. How each area works is in its **reference file** under `docs/reference/`, written when
the area lands. Read this file, then `PLAN.md`'s sections for the phase in progress, then
`docs/database.md` for anything that touches the data, then the reference file of every area a
task touches, then the header comments of the files involved. A section named in bold is a
heading of this file or of a doc.

## Status

**Phases 0 to 6 are complete and version 1.0.0 is on `main`, untagged on purpose; Phase 7, the
UI/UX pass, landed on 2026-11-04 and Phase 8, performance and cleanup, is next; the `v1.0.0` tag
waits for it.** The skeleton (presets, the gate, the
boundary check, CI on macOS and Ubuntu; `docs/reference/testing-tools.md`); the data contract
(`docs/database.md`); `core` (the position key, `Position` over chess-library, tiers, badges,
names, the read-only opening store and the canonical dump, the recommendation score, the UCI
engine client with its fake engine, the profile store with the engine cache, spar sessions,
repertoires, cards and reviews across `profiles.db` versions 1 to 3, the sampler, the spar
session, the engine reply provider, the SM-2 scheduler, repertoire coverage, PGN import, profile
export and import, the opening database's locator and import; `docs/reference/core.md`); the
pipeline (`chess-coach-pipeline build`, two passes with a count-min sketch, the evaluation merge,
badges, names, the sample corpus and its golden dump, the benchmark, `trim`;
`docs/reference/pipeline.md`); the CLI (`dump`, `stats`, `explore` with `--score` and `--engine`,
`name`, `eval`, `spar`, `repertoire`, `drill`, `profile`, `database`, with `--db` optional beside
`--data-dir`; `docs/reference/cli.md`); the app (the board, the shell, Explore on a database
worker with the engine worker's lines and cancellation, the Practice screen (the Spar screen's
label in the app) on a spar worker, the Repertoire and Drill screens on a profile worker with
backup and restore, the opening database found through `--db`, the data directory and the app
bundle and imported with File > Import database, the bundled Stockfish found by itself, the
**Study** look (paper and night themes from `app/src/study_palette.hpp` and the stylesheets,
Source Serif 4 and Source Sans 3 bundled, our own icons, Fusion on every platform), plain
language throughout with Help > How to use chess-coach, sounds through our own `NSSound` backend
with a toggle, settings in `<data-dir>/settings.ini`, About, the UI harness with screenshots;
`docs/reference/app.md`); the macOS package with Qt, Stockfish and the published opening database
inside (the one `data/release/bundled.txt` names), the licence texts in `legal/`,
`tools/package.sh` and the tag-triggered release workflow (`docs/reference/testing-tools.md`);
and the release-database tooling, `tools/build_release_db.sh` and `tools/verify_db.sh`
(`docs/reference/pipeline.md`, **Release database**; `data/release/README.md`), with data
version 1 (October 2026) published as the `data-v1` release. `PLAN.md` §7 lists the phases with
their sizes (Phases 7 and 8 at the end), §6 the data, §9 the process, §11 the decisions, §12 the
resolved questions and §14 the deferred path.

## Stack (pinned)

- **C++20.** Apple clang on macOS for the `dev` and `release` presets (the dev machine's Command
  Line Tools ship clang 15.0.0, which lacks `std::expected` and has `std::format` only by OS
  version: use **fmt** and **tl::expected** instead, see **Gotchas**); the sanitized `gate`
  preset compiles with Homebrew LLVM's clang on macOS, because Apple clang 15's ASan runtime
  crashes on macOS 26 (`docs/reference/testing-tools.md`, **Presets**); GCC or Clang on Linux.
  Warnings as errors.
- **CMake 3.28 or newer with presets** (`dev`, `gate`, `release`: configure, build and test
  presets of the same names), **Ninja**, **vcpkg in manifest mode** with a pinned baseline in
  `vcpkg-configuration.json`; overlay ports under `vcpkg/ports/` for what the registry lacks.
- **Qt 6 Widgets, Svg, SvgWidgets and Test**, from Homebrew on macOS (`qt`, 6.12 at setup) and
  from apt on Linux, found through `CMAKE_PREFIX_PATH`; **never from vcpkg**, which builds Qt from
  source for hours. No Qt Quick, no QtNetwork.
- **chess-library** (Disservin, MIT, header-only, no releases: an overlay port pinned to a
  commit) for move generation, legality, SAN and UCI; our own Zobrist tables and FEN
  normalization in `core`.
- **SQLite through SQLiteCpp** (MIT) for the opening database and the profile database;
  **zstd** streaming for the dumps; **nlohmann/json** for the evaluation database; **fmt**,
  **spdlog**, **CLI11**, **toml++**, **tl-expected**; **reproc++** (MIT) for the Stockfish
  process.
- **Catch2 v3** for tests, **RapidCheck** for property tests, **Qt Test** under the offscreen
  platform for UI tests; **clang-format** with the committed `.clang-format`, **clang-tidy** with
  the committed `.clang-tidy`, ASan and UBSan in the `gate` preset.
- **Stockfish** (GPLv3) as a separate process over UCI, never linked; `brew install stockfish`
  locally (19 at setup), bundled per CPU at release with its licence text and a source link.
- **Data:** the Lichess monthly dumps and evaluation database (CC0), the `lichess-org/chess-openings`
  TSV (CC0, verified 2026-11-03), the cburnett piece set (CC BY-SA 3.0, attributed).
- **MIT** for our code. Dependency policy: permissive freely; LGPL (Qt) dynamically linked with
  its text shipped; GPL only as a separate process (Stockfish), never linked; AGPL (lila,
  lila-openingexplorer) never, read only as claims about behaviour.

Check before writing an API call. Qt 5 answers, Qt Quick answers to Widgets questions, pre-C++17
idioms and python-chess conventions are the most common mistakes here. Look a signature up in the
real header: Qt under `$(brew --prefix qt)/include/`, a vcpkg port under
`build/<preset>/vcpkg_installed/<triplet>/include/`, chess-library's single `chess.hpp`.

## Commands

| What | Command |
|---|---|
| The gate (format, build with warnings as errors under the sanitizers, every test, the benchmarks on an idle machine, clang-tidy, the boundary, the corpus); its quick profile (format, build in `dev`, unit tests, boundary) | `tools/gates.sh`; `tools/gates.sh --quick` |
| Run all tests | `tools/run_tests.sh` |
| Run one module's tests, optionally filtered by a ctest regex | `tools/run_tests.sh core 'Zobrist'` |
| The benchmarks, in the release preset | `tools/run_tests.sh --bench` |
| Format; check only | `tools/format.sh`; `tools/format.sh --check` |
| Configure and build (dev preset) | `cmake --preset dev && cmake --build --preset dev` |
| Run the app on an isolated data directory and a given database (the owner alone may omit `--data-dir`: it opens the real user data directory) | `build/dev/app/chess-coach --data-dir <dir> --db <file>` |
| The CLI; a command that reads the opening database takes `--db <file>`, or `--data-dir <dir>` with an `openings.db` in it | `build/dev/cli/chess-coach-cli <command> [options]` |
| Put an opening database into a data directory; show the one the CLI reads | `build/dev/cli/chess-coach-cli database --data-dir <dir> import <file>`; `... database --data-dir <dir> info` |
| The pipeline on the sample dump | `build/dev/pipeline/chess-coach-pipeline build --in pipeline/tests/sample/lichess_db_standard_rated_2013-01.pgn.zst --out <file>` |
| Trim month files and the evaluation database to what the build reads, under their names | `build/dev/pipeline/chess-coach-pipeline trim --out-dir <dir> [--max-ply N] <in>...` |
| Golden corpora: the sample dump through the pipeline, the CLI queries | `tools/run_corpus.sh` |
| UI tests with screenshots, offscreen | `tools/ui_tests.sh` |
| Package: the macOS disk image; the image built and checked mounted, with the opening database at `<file>` bundled when given (the release workflow bundles the one `data/release/bundled.txt` names) | `cmake --build --preset release --target package`; `tools/package.sh [--db <file>]` |
| A release database: built from a data directory (the coordinator's or the owner's, never a lane's, but on the sample); verified, with the reproducibility rebuild | `tools/build_release_db.sh <data-dir> <out.db> --data-version N [--months ...]`; `tools/verify_db.sh <db> [--rebuild]` |
| Land a delivered series; in a batch, every series but the last with `--no-gate` | `tools/land_series.sh <patch-dir> <base-commit> <tree-hash> [--no-gate]` |
| The changelog section for a release, from the commit log | `tools/changelog.sh <version> [<from-ref>] [<to-ref>]` |

**Any configure or build writes files**: `build/<preset>/` in the tree it runs in, the vcpkg
ports installed under it, and a normal app run reads and writes the owner's user data directory
(profiles, repertoires, the engine cache) under the OS application-support folder. The gate and
the UI tests run the app only with `--data-dir` pointed at scratch.

**Gates run in batches (Pace): the full gate once at the head of a series, on a fresh clone,
and once per batch of series landed together; the quick gate is for the coordinator's own small
changes and for a developer's checks while a series is in progress, never required per commit.**

## Where things live

| Area | Code | Reference |
|---|---|---|
| Core: position and key (Zobrist, normalization), the opening store (read-only), tiers, badges, names, the recommendation score, the profile store (profiles, repertoires, cards, reviews, spar sessions, engine cache), the SRS scheduler, the sparring sampler, the UCI engine client | `core/` | `docs/reference/core.md` |
| The data contract: tiers, speeds, the key, the schema of both databases, badges, the knobs, the canonical dump | `docs/database.md` | `docs/database.md` is the reference |
| Pipeline: zstd streaming, PGN scanning, bucketing, the two-pass aggregation, pruning, the eval merge, names, the writer, the benchmark | `pipeline/` | `docs/reference/pipeline.md` |
| CLI: explore, eval, name, stats, dump, drill in the terminal | `cli/` | `docs/reference/cli.md` |
| App: the Qt Widgets shell, the board, Explore, Spar, Repertoire, Drill, settings, the engine worker | `app/` | `docs/reference/app.md` |
| Data files: the chess-openings TSV, piece sets, release database metadata | `data/` | `docs/database.md` |
| Tests and tools: presets, the gate, the test runner, the corpus runner, the UI harness, landing, CI | `*/tests/`, `tools/`, `cmake/` | `docs/reference/testing-tools.md` (Phase 0) |

## Architecture rules

These keep the data exact, the app testable without a window, and the pipeline runnable on a
laptop. Treat them as hard rules.

### Module boundaries

- `core ← pipeline`, `core ← cli`, `core ← app`. `pipeline`, `cli` and `app` never depend on each
  other. Nothing in `core`, `pipeline` or `cli` includes a Qt header or links a Qt target; the
  gate fails if it does.
- **No network code anywhere**: no sockets, no HTTP library, no QtNetwork, no download step in
  the build. The pipeline reads files the owner downloaded; the app reads the database beside it
  or in its data directory.
- Every module is a CMake target with an explicit source list (no globbing), public headers
  under `<module>/include/`, private sources under `<module>/src/`, tests under
  `<module>/tests/`, each test labelled with its module and its kind (`unit`, `property`,
  `engine`, `ui`, `bench`).

### `core/` is exact, deterministic and keyed by position

- **Every position is keyed by its Zobrist hash** of the normalized position: pieces, side to
  move, castling rights, and the en passant file only when an en passant capture is legal; never
  the move counters. Our own tables, incremental on every move, so transpositions merge and the
  database, the repertoire and the engine cache agree on identity. The FEN is stored beside the
  key for display and is never a key.
- **Chess rules come from chess-library** and nowhere else: move generation, legality, SAN and
  UCI. `core` wraps it in its own `Position` type; there is no second rules implementation.
- **Tier is a first-class dimension.** Every statistic, badge, sample and recommendation is
  computed for a stated tier; nothing falls back to "all games" silently.
- **The opening database is read-only to the app.** It is opened read-only; profiles,
  repertoires, cards, reviews, spar sessions and the engine cache live in the profile database in
  the data directory, versioned with a migration per schema version.
- **Badges and the off-book rule are computed once, in the pipeline**, from the thresholds in
  `docs/database.md`; the app reads them. The recommendation score is computed in `core` from
  stored numbers, with its components exposed, never a black box.
- **The engine is a process.** Stockfish over UCI through `core`'s client: time-based searches,
  every full-strength result cached by key and line count with the depth reached (a hit needs
  the caller's minimum depth; limited-strength searches bypass the cache), the client owning the
  handshake with never two searches in flight. No Stockfish source is linked, ever.
- **Deterministic.** The same inputs and seed give the same outputs; the sparring sampler takes
  an explicit generator; no clock and no I/O in the pure parts, file access only through the
  store types.
- **Errors are values** at the library boundary (`tl::expected` with an error type); exceptions
  only for programming errors.

### `pipeline/` streams and never stores

- Reads `.pgn.zst` through a streaming decompressor and never writes a decompressed dump; filters
  by speed, rated flag and tier from the headers before touching the moves; stops parsing a game
  at the depth cap; skips and counts a game it cannot parse, never aborts the run on one.
- **Two passes, bounded memory**: pass one streams every game into a count-min sketch of
  position visits per tier, which never underestimates; pass two aggregates exactly only the
  candidates whose sketch count reaches the threshold. Every survivor's counts are exact; memory
  is the sketch plus the survivors, never every distinct position.
- Writes with one transaction per batch; records the months, the knobs and the data version in
  `meta`. Reproducible: the same months and knobs give the same canonical dump, which is how the
  sample corpus is checked.
- Multithreaded by game with a single writer; the throughput and the memory high-water mark are
  measured by the benchmark and stated in the README.

### `app/` is presentation

- **One store, many views.** Widgets never run SQL; every profile mutation goes through `core`'s
  store API, so the CLI and the app agree.
- **Engine and database work never runs on the GUI thread.** Searches run on the engine worker;
  results arrive by queued signal; a widget is touched only on the GUI thread.
- **The board is a `QGraphicsView` scene** with SVG pieces, animated moves and our own hit
  testing in board coordinates; theming through one stylesheet. No Qt Quick.
- **Every screen works with `--data-dir` and `--db`**; the app writes in its data directory and,
  on an export, in the file the user picks, nowhere else.
- **User-facing strings go through `tr()`** from the first screen; no translation files until a
  second language is asked for.
- **Persistence is versioned SQLite** with a migration per version; a profile saved by any
  release opens in every later one.

## Folder layout

```
core/            the library: position and key, opening store, tiers, badges, names, score,
                 profile store, SRS scheduler, sparring sampler, UCI client; include/, src/, tests/
pipeline/        chess-coach-pipeline: zstd streaming, PGN scanning, bucketing, the two-pass
                 aggregation, pruning, eval merge, names, writer; tests/ with the sample dump and
                 its golden canonical dump
cli/             chess-coach-cli: explore, eval, name, stats, dump, drill; tests/
app/             chess-coach: the Qt Widgets app, the board, the four screens; tests/ (Qt Test,
                 offscreen)
data/            the chess-openings TSV, piece sets, release database metadata
docs/            database.md (the data contract); reference/ (one file per area, as it lands)
legal/           the full licence text of every component that ships (`NOTICE` names them)
tools/           gates.sh, run_tests.sh, format.sh, land_series.sh, changelog.sh;
                 run_corpus.sh, ui_tests.sh
cmake/           warnings, sanitizers, the boundary check, packaging (Phase 0)
vcpkg/           overlay ports (chess-library)
.claude/agents/  scout, researcher, developer, reviewer
.github/         workflows (Phase 0)
PLAN.md          the plan: gitignored, read from the main tree
```

## Code conventions

- **Names.** Types `PascalCase`; functions, variables and namespaces `snake_case`; private data
  members with a trailing underscore (`count_`); enumerators `snake_case` inside an `enum class`;
  macros, which are rare, `CHESSCOACH_UPPER_SNAKE`. Qt overrides and slots keep Qt's camelCase
  where Qt requires it; our own members are `snake_case` even in `app`.
- **Files.** One type or one tight cluster per file; public headers `snake_case.hpp` under
  `<module>/include/chesscoach/`, private sources under `<module>/src/`, tests
  `<name>_test.cpp` under `<module>/tests/`. Every file opens with a comment saying what it
  holds and which rules it obeys.
- **Headers.** `#pragma once`; includes grouped as `.clang-format` orders them: the file's own
  header, then `<chesscoach/...>`, then quoted headers of the same module (`app`'s own), then
  third-party, then the standard library.
- **Namespaces.** Everything under `namespace chesscoach`, with a nested namespace per
  executable module where one helps (`chesscoach::pipeline`); no `using namespace` in a header.
- **Errors.** `tl::expected<T, E>` at every library boundary, `E` an `enum class` or a small
  struct; exceptions only for programming errors; `[[nodiscard]]` on every function whose result
  is the point.
- **Ownership.** `std::unique_ptr` for our objects; Qt parent ownership for `QObject`s; never
  both on one object. No raw owning pointers, no `new` outside a Qt parent-child construction,
  with one exception: a worker `QObject` that is moved to its own `QThread` cannot have a parent,
  so it is created with `new`, deleted by `deleteLater` on the thread's `finished` signal, and
  the window joins the thread in its destructor (the database, engine, spar and profile
  workers).
- **Formatting** is `.clang-format`'s (LLVM base, four-space indent, 100 columns, pointers
  left-aligned); `.clang-tidy` is the linter; both run in the gate and neither is argued with.

## C++, Qt and chess gotchas for this project

- **En passant in the key.** Hash the en passant file only when a capture is actually legal;
  otherwise the same position reached with and without a double pawn push gets two keys and the
  tree splits. The FEN's en passant field is normalized the same way before it is stored.
- **Normalized FEN** strips the halfmove and fullmove counters; everything else is kept as
  chess-library gives it. `parse(print(fen))` must reproduce the normalized form.
- **Lichess PGN**: SAN carries `+`, `#`, `=Q`, disambiguation and `O-O` with capital O; comments
  `{ [%clk 0:03:00] [%eval 0.17] }` sit between moves; there are no variations; the result token
  ends the movetext; the speed is the `Event` header ("Rated Blitz game") with `TimeControl`
  beside it; `WhiteElo` or `BlackElo` can be `?`, and such a game is skipped and counted.
- **zstd**: `ZSTD_decompressStream` with `ZSTD_DStreamInSize` buffers; a dump is one frame of
  tens of gigabytes; `ZSTD_decompress` of the whole file is never an option.
- **SQLite**: one transaction per batch of inserts or the write takes hours; `WITHOUT ROWID` on
  the key-indexed tables; `journal_mode=OFF` and `synchronous=OFF` during the pipeline's build
  only; `mmap_size` for reads; the opening database opened `SQLITE_OPEN_READONLY`; prepared
  statements once per connection; the Zobrist key is `uint64_t` stored as a bit-cast `int64`
  and compared as integers, never as text.
- **UCI**: `isready` and `readyok` before every `position`; after `go` only `stop` or waiting for
  `bestmove`, never a second `go`; `ucinewgame` between games; `info` lines carry `multipv`,
  `score cp` or `score mate` from the side to move, and `pv`; `UCI_LimitStrength` with `UCI_Elo`
  has a floor (1320 in Stockfish); the process can die mid-search and the client reports it.
- **Qt**: a widget touched off the GUI thread crashes at best; `QObject` parent ownership and
  `std::unique_ptr` on the same object double-delete; the engine worker is `moveToThread` with
  queued connections; one `QSvgRenderer` per piece shared by its items;
  `QStandardPaths::AppDataLocation` is the owner's real data directory, which is why `--data-dir`
  exists.
- **CMake and vcpkg**: configure only through a preset, so the vcpkg toolchain file is set at
  the first configure; ports install under `build/<preset>/vcpkg_installed/` per build directory
  and the binary cache under `~/.cache/vcpkg/archives` makes the second lane fast; Qt comes from
  `CMAKE_PREFIX_PATH`, never from the manifest.
- **Apple clang 15**: `std::expected` is C++23 and absent; `std::format` depends on the deployment
  target; the project uses `tl::expected` and `fmt`. Check the real header before using a C++20
  library feature.
- **Sanitizers**: ASan on macOS has no LeakSanitizer, so a leak shows only in the Linux job; the
  macOS `gate` build uses Homebrew LLVM's clang and its ASan runtime (Apple clang 15's crashes on
  macOS 26), and a `build/gate/` configured before that switch needs `rm -rf build/gate` once;
  UBSan flags signed overflow, so the Zobrist arithmetic is `uint64_t` throughout.
- **Lifetimes**: a `std::string_view` into the streaming parser's buffer dangles once the next
  chunk is read; copy what outlives the chunk.
- **Paths** in tests are built from `std::filesystem::path` segments, never string literals with
  `/`; a Linux-only or Windows-only failure is otherwise the first sign.

## Seeing the app

The coordinator runs the app on the main tree with `--data-dir` pointed at scratch and `--db` at
a built database, and reads the screenshots the UI tests produce. Agents see the app only through
the UI harness in their own clones (Qt Test under the offscreen platform; screenshots land under
`build/<preset>/ui-screenshots/`) or a manual run on scratch data. Headless runs (the tests, the
CLI, the pipeline on the sample dump) render nothing and catch most errors.

## Agent workflow

Work on this repository is split across a coordinator and four project subagents
(`.claude/agents/`). The split is by judgment required, and by how much context each seat can
afford to hold.

- **Coordinator** (the top-level session) — talks to the owner, makes design decisions within
  `PLAN.md` or escalates them, writes briefs (each naming its **tier**, below), approves plans
  where a plan is required, routes work and review findings, reads the real diff of every tier A
  change and of every fold, and lands series. **Small work it does itself** (**Pace**): a doc, a
  config or tooling change, a contained fix it can make and gate in minutes goes straight onto
  `main` in the main tree under the quick gate, without a brief or a reviewer, unless it touches
  tier A ground, which takes the tier A path whatever its size. It spawns an agent for anything
  larger, reads what it needs itself when that is faster than a scout, and sends the scout on
  sweeps.
- **`developer`** (Opus) — works in a clone of its own (**Lanes**), implements the brief, runs
  the full gate once at the series head (the quick gate while it works, as often as it helps,
  never required per commit), verifies behaviour through the tests, the CLI, the pipeline's
  sample corpus and the UI harness in its clone, updates the docs the change invalidates, and
  delivers a patch series on the current `main` with the tree it reproduces. Never writes to
  the main working tree. Plans first only when the brief asks (tier A) or when it meets an open
  decision. **Continue the same agent from plan to implementation** (`SendMessage`); respawning
  loses what it learned planning.
- **`reviewer`** (Opus) — independent review of a series or a batch of them, applied in a clone
  of its own: the full pass for tier A, the targeted pass for tier B (**Tiers**), a verdict per
  series. Report-only; it is given the code and the brief, never the developer's conclusions.
- **`scout`** / **`researcher`** (Sonnet) — read-only codebase lookups (including API shapes
  from the installed headers) and outside research (C++20, Qt 6, CMake, vcpkg, SQLite, zstd,
  the UCI protocol, the Lichess data, chess theory, licences). Neither configures or builds. A
  load-bearing research fact (an API shape, a Qt behaviour, a data-format detail, a licence
  term) is verified by Opus or the coordinator before a decision rests on it.

**Tiers.** Every brief names its tier, which sets the plan and the review:

- **A** — the position key (the Zobrist tables, normalization, the en passant and castling
  rules, the transposition rule); the schema of either database and its versioning and
  migrations; the pipeline's semantics (tier bucketing, the speed filter, the sketch, pruning,
  aggregation, determinism); the badge definitions and the off-book rule; the recommendation
  score, the SRS scheduler and the sparring sampler; the UCI client's protocol handling; module
  boundaries and the no-network rule; and any change to a golden corpus's expected output or a
  benchmark's budget. A plan before code, approved by the coordinator, who escalates design
  decisions to the owner; then the implementation; then the full review: keys probed on random
  games, aggregation checked for conservation and exactness, old profile files loaded, the
  engine driven against the fake and the real Stockfish, every gate, the UI harness driven.
- **B** — features inside existing frameworks: a CLI subcommand, a screen or widget, a new
  statistic column, a coach message, a piece set, a pipeline report, tooling, CI. No plan round
  trip: the brief carries the decisions and the developer stops only at an open decision. The
  targeted review, about thirty minutes: the diff read, the delivered tree checked against the
  developer's full-gate run, the changed behaviour driven once.
- **C** — docs, comments, config. No reviewer: the gate and the coordinator's own read.

Small later-scope items are **batched** by area, up to about five a slice, one commit each, one
brief and one review.

**Pace.** Speed is a goal of the process, not a by-product (`PLAN.md` §11, 2026-11-03). The
coordinator keeps every seat busy: developers run in parallel wherever their areas are disjoint,
scouts and researchers run alongside them, the next brief goes out while a review runs, and a
reviewer takes the series ready at the time as one batch rather than one series at a time. Gates
run in batches, never per commit: the developer's full gate once at the series head, the
reviewer's once per batch for tier A, the landing's once per batch of series landed together.
Work small enough for the coordinator to do in minutes is done by the coordinator, not briefed.
Nothing waits that need not wait.

**Briefs.** A brief is the whole of what an agent knows about the task, so it carries:

- **Title and tier.**
- **Goal** — what changes for the user or the tooling, in two or three sentences, and what is
  explicitly out of scope.
- **Decisions made** — every choice the coordinator has already settled (names, which module,
  which table, which threshold, which file), so the developer never has to guess; for tier A,
  the plan is where remaining choices surface. Decisions cite `PLAN.md` §11 by row where one
  applies, and `docs/database.md` by section for anything about the data.
- **Read** — `CLAUDE.md`, the `PLAN.md` sections for the phase, `docs/database.md` when the data
  is touched, the reference files of the areas touched, and any module or doc the task depends
  on; the scout's findings when there are any, as `path:line`.
- **Verification** — the tests expected (unit, property, corpus, engine, UI), and what the UI
  run must show when the change is visible.
- **Lane** — the clone to use, and the base commit the series must be built on.
- **Delivery** — the plan format (tier A) or the report format, as the agent definition states.

**Lanes.** Every configure and build writes `build/<preset>/` in the tree it runs in, and a
normal app run opens the owner's data directory, so the main working tree is the coordinator's
and stays at `HEAD`: no agent writes to it or its index. A developer or a reviewer works in a
`git clone` of its own under `~/Development/chess-coach-lanes/` (`lanes/dev-N` and
`lanes/review-N`, series under `series/<brief>/`), never under the session scratchpad or `/tmp`:
a reboot wipes `/private/tmp` (math-canvas lost every clone and a reviewed series that way on
2026-10-28). The first configure in a lane compiles the vcpkg ports not yet in the machine-wide
binary cache, which is slow once. Developers run at once, as many as there are disjoint areas
(**Pace**), under these rules: a lane rebases its series onto the current `main` before
delivery; a lane's docs changes stay within its area's reference file, the map only where a
status line changes; a lane runs the app
only through the UI harness or with `--data-dir` pointed at scratch, and **nobody runs the app
without `--data-dir` in a clone**, because that opens the owner's data directory; **no lane runs
the pipeline on a full monthly dump** (hours of every core: the sample dump and the small 2013
months are a lane's inputs, and a full build is the coordinator's, on the owner's instruction,
when no gate is running); engine tests use one thread and a small hash so lanes share the
machine; no lane kills a process it did not start (`pkill ninja` or `pkill stockfish` ends
another lane's gate) or clears `~/.cache/vcpkg`. The UI tests run under the offscreen platform,
so they need no display and no lock, and the full gate runs them.

**Review routing.** Reviews go out in batches (**Pace**): the series ready at the time, from one
lane or several, go to one reviewer in one brief, each with its tier, and come back with a
verdict per series. The reviewer's findings go back to the **same** developer (`SendMessage`),
which folds them into the series; the coordinator reads the fold's diff. A FAIL loops until it
passes and is re-reviewed; PASS WITH FIXES is folded and read, re-reviewed only when the fold
touched tier A ground; otherwise the coordinator reads the fold and lands. Anything the
developer did outside the brief (**Adjacent changes**) is kept or vetoed by the coordinator, not
the reviewer.

**Delivery and landing.** A series is `git format-patch` files on the current `main`, one logical
change per commit (refactors separate from features), each a **Conventional Commit**:
`type(scope): Summary`, with `type` one of `feat`, `fix`, `refactor`, `perf`, `test`, `docs`,
`build`, `ci`, `chore`, `style`, `revert`; `scope` a module name (`core`, `pipeline`, `cli`,
`app`) or `data`, `tools`, `docs`, `ci`, `build`; the summary an imperative sentence in sentence
case, no trailing period, at most 72 characters; a body of plain paragraphs saying what and why;
**no trailers**: no commit in this repository carries one, the coordinator's own included. The
series comes with the write-tree hash it reproduces and the full gate's tail at its head. Once
the review's findings are folded and the fold's diff read, the coordinator lands it with
`tools/land_series.sh <patch-dir> <base-commit> <tree-hash>` (clean tree and HEAD at the base
required; `git am`; the messages checked; the tree checked, with a rollback on mismatch; the gate
run; the commits left in place). Series land in batches (**Pace**): every series but the last
with `--no-gate`, the last running the full gate over the whole batch; a failing batch gate is
undone with `git reset --hard` to the pre-batch commit, and the series are relanded one at a
time to find the culprit. The coordinator pushes `main` to `origin` after each landing. The
owner reviews at phase gates and whenever they choose.

Constraints:

- **Decisions come from `PLAN.md`** (§11 decisions, §12 resolved questions, §5 architecture) and
  **`docs/database.md`** for the data. An agent that finds an open decision stops and reports;
  it does not guess.
- **The architecture rules above are not negotiable**, and neither are: the position key and the
  transposition rule, chess rules from chess-library only, tier as an explicit dimension, the
  two-pass bounded-memory pipeline, exact counts for every survivor, the engine as a process, no
  network, no Qt outside `app`, the GUI-thread rule, the store API, versioned persistence,
  clang-format, Conventional Commits, no trailers, no GPL or AGPL code in the tree.
- **Temporary instrumentation is tagged `[verify]`** and gone before a report:
  `grep -rn "\[verify\]" core pipeline cli app tools cmake` returns nothing.

## Release notes

- CPack with `macdeployqt` builds the `.dmg`, with the opening database and the Stockfish
  binary for the CPU inside the bundle when they fit the size rule in `PLAN.md` §12, or the
  database as a separate versioned file the app imports; Linux and Windows packages come after
  1.0. Installers are unsigned until the signing decision at 1.0, and the README documents the
  Gatekeeper workaround.
- `CHANGELOG.md` is generated from the Conventional Commit log per release; tags are `vX.Y.Z`;
  the database file carries its own data version, shown in the About dialog beside the app's.
- `NOTICE` lists every third-party component and licence that ships, `legal/` holds each
  licence's full text, and the Stockfish binary ships with the GPL text and a link to the exact
  source it was built from.

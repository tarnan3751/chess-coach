# Tests and tools

How the build is configured, how the tests are declared and selected, what the gate runs, the
measurements, the module boundary check, the compiler, format and tidy settings, CI, the macOS
package and the release workflow, the pinned dependencies, and how lanes deliver and land a
series. The commands
are in `CLAUDE.md`, **Commands**; the rules they enforce are in `CLAUDE.md`, **Architecture
rules**.

## Presets

Configure, build and test only through a preset (`CMakePresets.json`, schema version 6). Every
configure preset uses Ninja and builds into `build/<preset>/`, which holds the vcpkg ports
(`vcpkg_installed/`), the generated `compile_commands.json` and the generated version header
`generated/chesscoach/version_info.hpp`.

| Preset | Build type | Sanitizers | Used by |
|---|---|---|---|
| `dev` | Debug | none | day-to-day work, `tools/run_tests.sh`, the quick gate |
| `gate` | RelWithDebInfo | ASan and UBSan, no recovery (`CHESS_COACH_SANITIZE=ON`) | the full gate's build, tests and clang-tidy |
| `release` | Release | none | the benchmarks (`tools/run_tests.sh --bench`, the gate's bench step), the package |

Each configure preset has a build preset and a test preset of the same name. The test presets
print the output of a failing test and treat "no tests matched" as an error, which the gate and
`tools/run_tests.sh` catch: they say so in one line ("no tests match yet", "no benchmarks yet")
and succeed, so a module or a kind with no tests yet passes.

The presets set no toolchain: the root `CMakeLists.txt` picks it before `project()`, from
`$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake` when `VCPKG_ROOT` is set, else from
`~/vcpkg/scripts/buildsystems/vcpkg.cmake`, and fails the configure when neither exists. On
macOS it appends `brew --prefix qt` to `CMAKE_PREFIX_PATH` unless `Qt6_DIR` is set; on Linux Qt
is found on the default prefix. Qt is never a vcpkg port.

On macOS a sanitized configure (the `gate` preset) compiles our code with Homebrew's LLVM clang
(`clang++` and `clang` under `brew --prefix llvm`, or under `/opt/homebrew/opt/llvm` or
`/usr/local/opt/llvm` when `brew` is not on the `PATH`) when it is installed and no compiler was
given, because Apple clang 15's AddressSanitizer runtime crashes before `main` on macOS 26; the
vcpkg ports stay built by Apple clang and the binaries link the system libc++. `dev` and
`release` stay on Apple clang, so a library feature Apple clang 15 lacks still fails the full
gate's release build. If that path ever breaks, the alternatives are updating the Command Line
Tools to an Apple clang whose ASan runs on the installed macOS, or UBSan alone on Apple clang
with ASan left to the Linux job.

## The gate

`tools/gates.sh` runs layout, format, build, test, bench, tidy, boundary and corpus, in that
order, and prints one line per step; the script's header says what each step does. The full
gate builds the `gate` preset and runs every test but the benchmarks (`ctest -LE bench`), then
the benchmarks in the `release` preset, then clang-tidy; it fails without a Stockfish binary
(`CHESS_COACH_STOCKFISH` or `stockfish` on the `PATH`). `--quick` builds `dev` and runs the
`unit` tests and the boundary only. When tests fail, ctest's logs (every failing test's output,
and a RapidCheck property's seed) are kept in `build/gate-failures/<UTC timestamp>-<pid>/<preset>/`.

`tools/run_tests.sh [<module> [<regex>]]` configures and builds `dev` and runs every test but
the benchmarks, or one module's (`-L ^<module>$`), optionally filtered by name (`-R`);
`--bench` runs the benchmarks in `release`. `tools/format.sh` rewrites every `.cpp`, `.hpp`, `.h`
and `.mm` under the four modules with clang-format; `--check` is the gate's format step.

## Tests

Every test carries two ctest labels: its module (`core`, `pipeline`, `cli`, `app`) and its kind
(`unit`, `property`, `engine`, `ui`, `bench`). `ctest --preset dev -N -L property` lists the
property tests; `-L '^core$'` selects a module. Tests are declared with `chesscoach_add_test`
(`cmake/testing.cmake`):

```cmake
chesscoach_add_test(<target> MODULE <core|pipeline|cli|app>
                    KIND <unit|property|engine|ui|bench>
                    SOURCES <source>... [LIBS <library>...])
```

- **Catch2 kinds** (every kind but `ui`) link `Catch2::Catch2WithMain`, and
  `catch_discover_tests` registers each `TEST_CASE` as its own ctest test, named by the test
  case, after the target is built. The labels are set by a small script ctest includes after the
  discovered tests (`build/<preset>/<module>/<target>_labels.cmake`), because
  `catch_discover_tests(PROPERTIES LABELS "a;b")` loses the list's semicolon. Catch2's list of
  the discovered names holds each `[` and `]` as the bytes 0x02 and 0x03, so the script restores
  them and labels one test at a time, and a test case whose name has brackets keeps its labels.
- **The `labels` ctest test** (labels `cmake;unit`) runs `cmake -P cmake/labels_check.cmake`,
  which reads `ctest --show-only=json-v1` and fails on any test without exactly one module label
  (`core`, `pipeline`, `cli`, `app`, or `cmake` for the build's own tests) and one kind label, and
  when no test has brackets in its name (`core_tier_test` has one), so that case stays checked.
- **Property tests** are Catch2 test cases that `REQUIRE(rc::check("<name>", [] { ... }))`
  with RapidCheck (`LIBS rapidcheck`). ctest runs every KIND `property` test with
  `RC_PARAMS="max_success=2000 seed=20261103"` (set by the same labels script), so each property
  checks two thousand cases from a fixed seed, the same ones on every run (`PLAN.md` §5.5). The
  environment variable `CHESS_COACH_RC_PARAMS`, when set at configure time, replaces that value
  verbatim: `CHESS_COACH_RC_PARAMS="max_success=10000 seed=7" cmake --preset dev` runs other
  cases, and configuring again without it restores the default.
- **A failing property** prints its shrunk counterexample, the configuration it ran with
  (`Using configuration: seed=...`) and a line `To reproduce these, run with:
  RC_PARAMS="reproduce=..."`; the gate keeps both in its failure logs. Re-run the one failing
  case by running the test executable directly with that value, for example
  `RC_PARAMS="reproduce=..." build/dev/core/core_key_property_test "<test case name>"` (ctest
  would replace `RC_PARAMS` with its own), or re-run the whole property from its seed with
  `RC_PARAMS="max_success=2000 seed=<seed>"` the same way.
- **KIND `ui`** is a Qt Test executable (`QTEST_MAIN`) linking `Qt6::Test`: one ctest test,
  labelled `<module>;ui`, run with `QT_QPA_PLATFORM=offscreen`, so it needs no display. An `app`
  test links `app_lib`, the library the executable links, and saves screenshots under
  `build/<preset>/ui-screenshots/`; `tools/ui_tests.sh` runs these tests in the `dev` preset and
  lists the screenshots (`docs/reference/app.md`, **UI harness**).
- **`app_measure`**, the one test not declared through `chesscoach_add_test`: a program with a
  command line of its own (**Measuring**), so `app/CMakeLists.txt` adds it with `add_test`,
  labelled `app;bench`, under the offscreen platform.
- Every test target links `chesscoach::warnings`, like every other target of ours.

## Measuring

Phase 8 changes nothing for speed without a number before and after (`PLAN.md` §11,
2026-11-04), and the numbers come from `tools/measure.sh`. It builds the `release` preset, waits
for the one-minute load average to fall under 4 (`MEASURE_MAX_LOAD`), measures each section over
`--runs` runs (5), and prints one markdown table: each metric's median, its spread (min to max),
the number of samples, the unit and how it was measured, under a header with the commit, the
machine, the database, the engine and the load before and after. `--db` names the database, read
only (the release database for a baseline; without it the sample is built through the pipeline
into a scratch directory), `--engine` the Stockfish (else `CHESS_COACH_STOCKFISH` or the `PATH`),
`--tier` the tier (2, the app's default), `--out` a file for the table, and the sections to run
follow: `startup`, `explore`, `practice`, `board`, `memory`, `pipeline` and `cli`. The script's
header says what each section runs.

**The app** is measured by `app_measure` (`app/tests/measure.cpp`), a program linking `app_lib`
that opens the real `MainWindow` on a scratch data directory of its own (a `QTemporaryDir`) and
drives it as a user would: QTest's clicks on the board's squares, the first picking the piece and
painting before the timed second one, and the screens' buttons, found by object name as the UI
tests find them. It times with `QElapsedTimer` to the end of the paint pass that shows the
answer: its `QApplication` subclass's `notify` sees every event of the GUI thread, and a pass ends
when the outermost event that painted (an update request, or an expose) has returned, flush
included. Every wait runs the event loop until such an event; `QTest::qWait` and `qWaitFor` sleep
10 ms between passes, which delayed what was measured (a slide's first frame by a frame). Its own
read-only connection to the database chooses the moves and gives the summary line the Explore
screen must show, and core's `sampler_candidates` tells a coach's reply from the book from one
from the engine as the session does. It adds nothing to the app's code paths. Its scenarios:
`startup` (once per process: the process's start to `main` from the kernel's record on macOS,
`QApplication` with `configure_application`, the window's constructor, then the first paint, the
database named in the status bar and the start's rows), `explore` (the second click to the new
rows at the start, after 1. e4 c5 2. Nf3 d6, on the move that leaves book there and on a move off
book, the engine off or searching, and with it the engine's lines), `practice` (a game as white at
a fixed seed: replies from the book, the process's first reply from the engine, which starts it,
and later ones), `board` (the move's first paint, the slide's frames, a full repaint, Flip board
and `set_position`, at the device pixel ratio the platform gives: `QT_SCALE_FACTOR=2` stands in
for a Retina display) and `memory` (the resident size and the physical footprint after the start,
after each 50 of 200 Explore steps, after each of three practice games and after a drill of five
cards). The engines run at one thread and 16 MB of hash, at the settings' 2 s for Explore and
Practice's 0.5 s. ctest runs `app_measure all --fixture` on the fixture database with the fake
engine (labels `app;bench`), so the gate's bench step runs every scenario once in seconds; it
fails when a step never completes and has no time budget.

What the numbers are, and are not:

- **Offscreen.** The window paints into its backing store as it does on macOS, raster, but no
  window server composites it and no display's refresh paces it. The cocoa platform's share is
  not measured; `QT_QPA_PLATFORM=cocoa tools/measure.sh startup` on a display would.
- **A warm file cache.** Nothing here can empty it, so the database's pages and the libraries are
  in memory; a cold boot is slower. The first start of a freshly linked binary also pays the
  system's check of its signature (190 to 370 ms more before `main`), which is why the startup
  section discards one run.
- **The cores' power states.** On Apple silicon a step measured on an idle machine includes the
  cores waking: the Explore steps run two to three times faster while another core is busy (the
  engine searching, or a `yes > /dev/null`). The medians are an idle machine's, the user's case;
  compare a change's numbers with a baseline measured the same way, and read the spread.

### Baseline, 2026-11-04

The app, the CLI and the pipeline of `main` at `3849af9`, measured by `tools/measure.sh --db
openings-2026-10.db --engine stockfish --runs 5` on the development machine (an Apple M5, ten
cores, 24 GB, macOS 26.5.1): the release database (286 MB, data version 1) at tier 2, Stockfish
19, offscreen, the load average 2.1 before and 2.6 after. Medians over the runs, the spread in
brackets; times in milliseconds.

| What | Engine off | Engine on |
|---|---:|---:|
| Start to the window's first paint | 79.9 (79.1–82.3) | 81.7 (81.4–82.2) |
| Start to the Explore table's first rows | 87.2 (86.0–89.3) | 88.8 (88.4–89.3) |
| of which before `main`; `QApplication`; `MainWindow`'s constructor | 14.8; 10.1; 39.8 | 14.8; 10.1; 39.8 |
| Explore step: 1. e4 from the start | 6.7 (4.4–7.4) | 7.7 (5.6–10.1) |
| Explore step: 3. d4 after 1. e4 c5 2. Nf3 d6 | 4.1 (2.5–5.7) | 2.2 (2.1–2.4) |
| Explore step: a move that leaves book there (3. a4) | 16.7 (12.6–18.2) | 5.2 (4.8–7.5) |
| Explore step: a move off book | 4.7 (3.5–5.2) | 7.1 (3.6–10.2) |
| Explore's engine lines: a 2 s search; from the cache (the four steps' medians) | | 2004.7–2022.3; 4.4–16.1 |
| Practice: a reply from the book | | 3.4 (3.2–4.5) |
| Practice: the first reply from the engine; a later one (0.5 s searches) | | 607.8 (607.5–609.8); 502.5 (501.4–503.4) |

| The board | Device pixel ratio 1 | Device pixel ratio 2 |
|---|---:|---:|
| A clicked move to its first paint | 2.8 (1.7–3.0) | 4.6 (3.6–4.9) |
| The 150 ms slide: frames per second; the longest gap | 58.5; 24.4 | 58.9; 23.8 |
| A full repaint; Flip board painted; `set_position` and a repaint | 1.7; 5.5; 2.8 | 3.3; 8.0; 6.9 |

| Memory, one session | Resident | Physical footprint |
|---|---:|---:|
| The start shown | 60.2 MB | 21.2 MB |
| After 50 Explore steps; after 200 | 66.5 MB; 66.6 MB | 21.9 MB; 22.1 MB |
| After one practice game; after three | 68.9 MB; 69.0 MB | 22.7 MB; 22.7 MB |
| After a drill of five cards | 69.2 MB | 22.9 MB |

The pipeline's benchmark built the sample in 29 ms (28–29), 69,565 games per second, 14.2 MB at
its peak. The CLI's `explore` took 3.2 to 4.6 ms a process and `stats` 722 ms (719–727). In the
app nothing a user waits for takes 100 ms but what the settings ask (a search) and, the first time
a practice game leaves book in a run, the coach's engine starting (about 105 ms more); memory
grows while the first steps read the database in and then holds.

## The module boundary

`core`, `pipeline` and `cli` never include or link Qt, and nothing includes or links a network
library (`CLAUDE.md`, **Module boundaries**). Three checks hold it:

1. **At configure time**, `chesscoach_assert_no_qt(<target>)` (`cmake/boundary.cmake`), called
   on every target in `core/`, `pipeline/` and `cli/`, walks the target's link closure at the
   end of the configure and fails it on a Qt target (`Qt6::...`) or a Qt library file.
2. **The `boundary` ctest test** (labels `cmake;unit`, so the quick gate runs it too) runs
   `cmake -P cmake/boundary_check.cmake` over the sources, C++ (`*.cpp`, `*.hpp`, `*.h`) and
   Objective-C++ (`*.mm`): no Qt include (`#include` or `#import` of `<Q...>` or `<Qt.../...>`) in
   a source and no `Qt6::` in a `CMakeLists.txt` or `*.cmake` under
   `core/`, `pipeline/` or `cli/`; no network include (curl, httplib, Boost.Asio or Beast,
   sockets, `QtNetwork` and its classes, Apple's `CFNetwork` and `Network` frameworks and
   Foundation's `NSURL...` headers), no use of `NSURLSession`, `NSURLConnection`,
   `NSURLRequest`, `CFNetwork` or the `nw_connection_`/`nw_listener_` types (which Foundation
   and AppKit bring in without an include of their own), no `@import` of those frameworks, and
   no network target or framework (linked, found by `find_library`, or named by path) anywhere
   under the four modules and `cmake/`; no network port in `vcpkg.json`. It lists every
   offending line.
3. **The gate's boundary step** greps the same patterns, and runs even when the build failed.

The patterns of 2 and 3 are the same rules: CMake's regular expressions have no `[[:space:]]`,
so `boundary_check.cmake` spells it `[ \t]`, and it writes the network names with one letter
bracketed (`lib[c]url`) so that the file does not match the gate's grep, which reads `cmake/`.
A change to either pattern set changes the other.

## Flags

C++20 without extensions. `chesscoach::warnings` (`cmake/warnings.cmake`), linked PRIVATE by
every target of ours, is `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
-Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion
-Wformat=2 -Werror`, plus `-Wduplicated-cond -Wlogical-op -Wno-dangling-reference` on GCC (not `-Wuseless-cast`,
which rejects the casts between `uint64_t` and `size_t` that macOS needs and Linux does not;
GCC 13's `-Wdangling-reference`, part of its `-Wextra`, flags a `const auto&` bound to an
accessor's returned reference, a false positive GCC 14 narrowed; and `-Wnull-dereference` is
clang-only, because GCC's fires inside libstdc++'s `<optional>` at -O3). Third-party
headers come in through imported targets as system headers, so the flags apply to our code
only (Qt's moc output included). `CHESS_COACH_SANITIZE` (`cmake/sanitizers.cmake`) adds
`-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer` to every
target of ours, compiled on macOS by Homebrew's LLVM clang (**Presets**); the vcpkg ports are
built without it. ASan on macOS has no LeakSanitizer, so a
leak shows only in the Linux job.

## Formatting and tidy

`.clang-format` is LLVM with four-space indents, 100 columns and left-aligned pointers;
includes are regrouped into blocks: the file's own header (for a test, the header it tests),
`<chesscoach/...>`, the app's own quoted headers, third-party headers (any `<...>` with a slash,
a dot or a capital, Qt's included), then the standard library (`<lowercase_name>`). Namespace
closing comments are added. Both CI jobs and the dev machine run clang-format and clang-tidy
of the same major version: Homebrew's `llvm` and `clang-format` on macOS, and on Ubuntu the
`clang-format` and `clang-tidy` wheels from PyPI pinned to the same major (Ubuntu's own
packages are a major behind, and apt.llvm.org failed to resolve on the runners), with Ubuntu's
the `run-clang-tidy` driver taken from the same wheel's `data/bin` so that it matches the binary
(Ubuntu's own `run-clang-tidy-18` ran its own `clang-tidy-18`, which is how a green macOS job
and a red Ubuntu job once disagreed, and its start-up probe passes clang-tidy an empty source
argument that version 23 answers "No checks enabled"); the gate also names the binary with
`-clang-tidy-binary` and caps the driver's jobs by `CMAKE_BUILD_PARALLEL_LEVEL`. When Homebrew
moves to a new major, the pinned wheel versions move with it.

`.clang-tidy` enables `bugprone-*`, `cert-*`, `clang-analyzer-*`, `concurrency-*`,
`cppcoreguidelines-*`, `misc-*`, `modernize-*`, `performance-*`, `portability-*` and
`readability-*`, minus `modernize-use-trailing-return-type`, both magic-numbers checks,
`readability-identifier-length`, `cppcoreguidelines-pro-bounds-pointer-arithmetic`,
`cppcoreguidelines-pro-bounds-array-to-pointer-decay`, `cppcoreguidelines-owning-memory` (Qt
parent ownership), both non-private-member-variables checks,
`bugprone-easily-swappable-parameters`, `readability-function-cognitive-complexity`,
`misc-include-cleaner`, `cppcoreguidelines-avoid-do-while` and `portability-avoid-pragma-once`
(the project's headers use `#pragma once`). The list starts from `-*`, so the compiler's own
warnings are left to the build, and `ExtraArgs: ['-Wno-unknown-warning-option']` keeps clang from
reporting the GCC-only warning flags in a GCC build's compile commands. Every finding is an error; headers are checked
when their path is under one of the four modules. The gate runs `run-clang-tidy` over the
`gate` build's `compile_commands.json` for the sources under `core/`, `pipeline/`, `cli/` and
`app/`. A `NOLINT` names its check and sits on the one line it excuses. `core/.clang-tidy`
inherits this configuration and turns off `clang-analyzer-optin.core.EnumCastOutOfRange` and
`clang-analyzer-security.ArrayBound` for core's sources: the analyzer follows core's calls into
chess-library's header and reports false positives inside it, where no `NOLINT` can sit
(`docs/reference/core.md`, **chess-library and the wrapper**).

## CI

`.github/workflows/ci.yml` runs `tools/gates.sh`, the full gate, on `macos-15` and
`ubuntu-24.04` for every push to `main` and every pull request, each job limited to 90 minutes,
a newer push cancelling a running one. macOS installs `ninja llvm clang-format stockfish qt`
from Homebrew; Ubuntu installs `ninja-build clang-format clang-tidy qt6-base-dev qt6-svg-dev
libgl1-mesa-dev stockfish` from apt (Qt 6.4) and puts `/usr/games`, where apt installs
Stockfish, on the `PATH`. vcpkg is cloned into `$RUNNER_TEMP/vcpkg` at the baseline
`vcpkg-configuration.json` pins (read with `jq`, so the hash lives in one file) and bootstrapped, and `VCPKG_ROOT` points at it; the vcpkg binary cache (`~/.cache/vcpkg/archives`)
is kept in the Actions cache, keyed by the OS and the hash of `vcpkg.json`,
`vcpkg-configuration.json` and `vcpkg/ports/`. On a failure the job uploads
`build/gate-failures/`. `.github/dependabot.yml` updates the actions weekly. macOS minutes cost
ten times Ubuntu's on a private repository (`PLAN.md` §9).

## Packaging

`cmake --build --preset release --target package` makes `chess-coach-<version>-macos-<arch>.dmg`
in `build/release/` with CPack's DragNDrop generator: an image named `chess-coach <version>`
holding `chess-coach.app` and a link to `/Applications`, the version being `project()`'s and the
arch the build's (`CMAKE_OSX_ARCHITECTURES`, else the processor). `cmake/packaging.cmake` is
included at the root on macOS only; Linux and Windows packages come after 1.0.

The `app` target stays a plain executable, so the `dev` and `gate` builds hold no bundle and
nothing of the packaging runs in a build. The install rules assemble the bundle in CPack's
staging directory: `Contents/MacOS/chess-coach`; `Contents/Info.plist` from
`cmake/Info.plist.in`, with the version and the placeholder bundle identifier
`dev.chess-coach.app`; and `LICENSE`, `NOTICE` and `legal/` under `Contents/Resources/`. The
pieces and the themes are compiled into the executable. CPack then runs
`cmake/package_bundle.cmake` on the staged bundle (`CPACK_PRE_BUILD_SCRIPTS`), in four steps:

- **The engine.** The Stockfish named by `CHESS_COACH_STOCKFISH` in the environment, else
  `stockfish` on the `PATH` (the `PATH` alone, never an app bundle), is copied unmodified to
  `Contents/Resources/engine/stockfish`, with `legal/stockfish.txt` (GPL-3.0) beside it as
  `COPYING` and a `SOURCE.txt` naming the version from the `id name` of its `uci` answer, the
  source `https://github.com/official-stockfish/Stockfish/releases/tag/sf_<version>`, and how the
  binary was obtained, from the keg's `INSTALL_RECEIPT.json` when it is Homebrew's (a bottle or
  a source build of the formula). No Stockfish, or one whose version is not a number like `19`
  or `17.1`, fails the package. The binary is the build machine's CPU's.
- **Qt.** `macdeployqt` from `brew --prefix qt` copies the frameworks the app links and their
  libraries into `Contents/Frameworks/` and rewrites the references. It runs with `-no-plugins`
  and is given two plugins to deploy for, copied into `Contents/PlugIns/` first: the cocoa
  platform and the offscreen platform, which lets the bundled app run headless. There is no style
  plugin: the app draws with Fusion, which QtWidgets builds in (`docs/reference/app.md`,
  **Themes**). Homebrew's `qt` installs every module's plugins in one directory, and
  `macdeployqt`'s own plugin set would deploy the virtual keyboard and through it QtQuick, QtQml
  and QtNetwork. `Contents/Resources/qt.conf` sets `Plugins = PlugIns`, so the app never loads a
  plugin, and with it a second Qt, from Homebrew's prefix.
- **The licences.** Every library `macdeployqt` copied in beside the Qt frameworks (ICU, GLib,
  OpenSSL, FreeType, HarfBuzz and the rest that Homebrew's Qt links) must have its licence text
  in `legal/<formula>.txt`, the formula being the Homebrew keg the library's
  `<brew prefix>/opt/*/lib/` path resolves to, named without an `@<version>`; one that has none
  fails the package, so a library Homebrew's Qt starts to link cannot ship without its licence.
  `NOTICE` names each, under the libraries Qt depends on.
- **The signature.** `codesign --force --deep --sign -` over the bundle: an ad-hoc signature,
  which Apple silicon requires once `macdeployqt` has rewritten the binaries. Releases are not
  signed with an identity or notarized.

**The opening database** is bundled when the release preset is configured with the cache variable
`CHESS_COACH_BUNDLE_DB` set to a file (`cmake --preset release -DCHESS_COACH_BUNDLE_DB=<file>`,
which `tools/package.sh --db <file>` does): the install rules copy it to
`Contents/Resources/openings.db`, where the app finds it when neither `--db` nor the data directory
names one (`docs/reference/app.md`, **The database**). `-DCHESS_COACH_BUNDLE_DB=` clears it, which
`tools/package.sh` without `--db` does, so its package then holds no database whatever an earlier
configure cached. The release workflow bundles the published database `data/release/bundled.txt`
names (**Release**).

`tools/package.sh [--db <file>]` builds the `release` preset and the package, refusing a `--db`
file that is not readable before the configure and one that the build's own `chess-coach-cli stats
--db` does not read as an opening database before the package (a relative path is the caller's),
mounts the image read-only and checks the bundle: `otool -L` of the app binary shows only Qt
frameworks through `@rpath`, `@loader_path` or `@executable_path` and system libraries, and no run
path outside the bundle; the app, started under the offscreen platform with a scratch `--data-dir`,
loads the bundle's offscreen plugin and is still running three seconds later, having loaded no
library from outside the bundle but the system's (`DYLD_PRINT_LIBRARIES`), which a Homebrew Qt on
the build machine would otherwise hide; the bundled Stockfish answers `uci` with `uciok`, with
`COPYING` and `SOURCE.txt` beside it; and with `--db`, `Contents/Resources/openings.db` is that
file byte for byte (`cmp`), without it there is none. It prints the image's path and size and exits
non-zero on the first failure. It takes minutes, so it is not part of the gate; the release
workflow runs it, and so does anyone changing the packaging.

## Release

`.github/workflows/release.yml` runs on every pushed tag `v*` and by hand (`workflow_dispatch`), as
one `macos-15` job: the same Homebrew tools, vcpkg at the baseline and binary cache as CI, then the
opening database to bundle and `tools/package.sh --db` on it. `data/release/bundled.txt` names the
database in three lines, the tag of the data release that holds it (`data-vN`), the asset's name and
the file's sha256; the job downloads the asset with `gh release download <tag> --pattern <asset>`
under the workflow's token, fails when `shasum -a 256` gives another sum, and hands the file to the
package (`data/release/README.md`, **Where the file goes**). That download fetches a published
artefact in CI, as the job's brew and vcpkg installs do; nothing is downloaded in the build, so the
no-network rule stands. On a tag it checks that the image's version is the tag's, then makes a draft
GitHub release named for the tag with the image attached and `tools/changelog.sh <version>` as its
body (`gh release create --draft --verify-tag`; the checkout fetches the whole history and the tags
for it). Run by hand on a branch, it keeps the image as the run's artifact `chess-coach-macos`
instead. Publishing the draft is a person's step.

To release: set `project(VERSION)` in the root `CMakeLists.txt`, land it, tag `main` as
`vX.Y.Z` and push the tag; check the draft and publish it. `CHANGELOG.md` is generated from the
commit log with `tools/changelog.sh` at the release and committed then, as its header says; the
workflow does not write it.

## Dependencies

vcpkg in manifest mode: `vcpkg.json` lists every dependency of every phase, and
`vcpkg-configuration.json` pins the builtin registry's baseline and adds the overlay ports
under `vcpkg/ports/`. The baseline is vcpkg commit `c653b77e80d1b61e5f5bd2eabf15bc6ec3bfe7ad`,
which gives these versions:

| Port | Version | Licence | CMake | Used by |
|---|---|---|---|---|
| `fmt` | 12.2.0 | MIT | `fmt::fmt` | `core` (public) |
| `tl-expected` | 1.3.1 | CC0-1.0 | `tl::expected` | `core` (public) |
| `cli11` | 2.7.2 | BSD-3-Clause | `CLI11::CLI11` | `pipeline`, `cli` |
| `catch2` | 3.16.1 | BSL-1.0 | `Catch2::Catch2WithMain` | the tests |
| `rapidcheck` | 2023-12-14 | BSD-2-Clause | `rapidcheck` | the property tests |
| `chess-library` | 2026-10-27 (overlay) | MIT | header only: `find_path(... chess-library/chess.hpp)` | `core`, from Phase 1 |
| `zstd` | 1.5.7 | BSD-3-Clause | `zstd::libzstd` | `pipeline`, from Phase 1 |
| `sqlitecpp` | 3.3.3 (with `sqlite3` 3.54.1) | MIT (SQLite: public domain) | `SQLiteCpp` | `core`, from Phase 1 |
| `nlohmann-json` | 3.12.0 | MIT | `nlohmann_json::nlohmann_json` | `pipeline`, from Phase 1 |
| `spdlog` | 1.17.0 | MIT | `spdlog::spdlog` | from Phase 1 |
| `tomlplusplus` | 3.4.0 | MIT | `tomlplusplus::tomlplusplus` | from Phase 1 |
| `reproc` | 14.2.8 | MIT | `reproc++` (`find_package(reproc++)`) | `core`'s engine client, Phase 2 |

**chess-library** has no releases, so `vcpkg/ports/chess-library/` pins a master commit:
`3af102c53752472c208d48921ece705b1f0d1626` (2026-10-27, `chess.hpp` version 0.9.10), archive
SHA512 `e543f67edc1c60504712c372011f4f3a0a741c4fc57f0d06c657cba55d39e891434e9cb215d114127072b3328141d1edcf7cf59b51158cad1207818aacf5db32`.
It installs the single header as `include/chess-library/chess.hpp` and its MIT licence as the
copyright. To move it, change `REF`, `SHA512` (`shasum -a 512` of
`https://github.com/Disservin/chess-library/archive/<commit>.tar.gz`) and the port's
`version-date` together.

**Linking each library once.** `fmt`, `tl-expected`, `CLI11` and `SQLiteCpp` are found once,
in the root `CMakeLists.txt`, so every directory links the same imported target (one found
again in each directory is a different target, which a link line must name again); `SQLiteCpp`
stays a private dependency of `core`, `pipeline_lib` and the test fixtures, and `app_lib` never
sees its headers. The root also sets policy `CMP0156`: the "ignoring duplicate libraries" warning
is Apple's linker reporting a static library named twice on a link line, which CMake writes by
its 3.28 rules when an executable and the libraries it links share a dependency, and CMake 3.29
and later, under the policy, write each library once for the linkers that need no repetition
(Apple's, LLD, MSVC). CMake 3.28, which the policy does not exist in, still builds, with the
warning.

**Outside vcpkg:** Qt 6 Widgets, Svg, SvgWidgets and Test from Homebrew (6.12.0 at setup) or
apt (6.4.2 on Ubuntu 24.04), `find_package(Qt6 6.4 ...)` in `app/` only; Stockfish 19 from
Homebrew, a separate process, never linked; clang-format from its own Homebrew formula
(`clang-format`, 23 at setup) or apt; clang-tidy and `run-clang-tidy` from Homebrew's `llvm`
(23 at setup; not linked into the `PATH`, so the gate looks in the keg) or apt. `NOTICE` names
every component and its licence, and `legal/` holds each shipped component's licence text: the
vcpkg ports' from their installed `share/<port>/copyright` at the pinned versions, the Homebrew
libraries' from their kegs, and the canonical text where the source carries none, as `NOTICE`
says entry by entry.

## Lanes and landing

A developer or reviewer works in a clone of its own under
`~/Development/chess-coach-lanes/lanes/<lane>`, never in the main tree, and delivers a series
of `git format-patch` files under `~/Development/chess-coach-lanes/series/<brief>/` with the
base commit and the tree the series reproduces (`CLAUDE.md`, **Lanes**, **Delivery and
landing**). The first configure in a lane compiles the vcpkg ports missing from the binary cache
(`~/.cache/vcpkg/archives`, shared by every lane), once; every later configure restores them
from it. The coordinator lands a series with
`tools/land_series.sh <patch-dir> <base-commit> <tree-hash>`, which checks the messages,
applies the patches, checks the tree and runs the gate; in a batch every series but the last
lands with `--no-gate`, and the last runs the gate over the whole batch.

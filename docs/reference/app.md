# The app

`chess-coach` is the Qt 6 Widgets application (`CLAUDE.md`, **app/ is presentation**). This file
covers the main window, the board, the database worker, the engine worker, the Explore screen,
the Spar screen and its worker, the settings, the themes, the About dialog, and the UI harness.

Every widget lives on the GUI thread. The opening database (`--db`) is opened read-only and read
only by the database worker on its own thread (**Database worker**); the engine and the engine
cache belong to the engine worker on another (**Engine worker**); a sparring session, with its
own connection to `--db` and its own engine, belongs to the spar worker on a third (**Spar**).
The app writes two files in `--data-dir`: `settings.ini`, only when a setting changes
(**Settings**), and `profiles.db`, only when the engine worker stores a search (**Engine
worker**) or the spar worker saves a session (**Spar**).

## Code

| File | What |
|---|---|
| `app/src/main.cpp` | The entry point, wiring only: `QApplication`, `--data-dir` and `--db`, the window |
| `main_window.{hpp,cpp}` | `MainWindow`: the frame, the line played, the worker's thread (**Shell**) |
| `db_worker.{hpp,cpp}` | `DbWorker`, the database worker on its own thread (**Database worker**) |
| `engine_worker.{hpp,cpp}` | `EngineWorker`, the engine and its cache on their own thread (**Engine worker**) |
| `explore_data.hpp` | `ExploreData` and `ExploreMove`, the worker's answer for a position |
| `explore_model.{hpp,cpp}` | `ExploreModel`, the table model of the Explore screen (**Explore**) |
| `explore_screen.{hpp,cpp}` | `ExploreScreen`, the Explore screen |
| `result_delegate.{hpp,cpp}` | `ResultDelegate`, the result bar of Explore's table and Spar's better choices |
| `spar_worker.{hpp,cpp}` | `SparWorker`, the live session, its store connections and its engine on their own thread (**Spar**) |
| `spar_data.hpp` | The spar worker's answers: `SparState`, `SparInterruption`, `SparSummary`, the saved sessions, a replay |
| `spar_screen.{hpp,cpp}` | `SparScreen`, the Spar screen (**Spar**) |
| `eval_bar.{hpp,cpp}` | `EvalBar`, the evaluation bar beside the board |
| `settings.{hpp,cpp}` | `Settings`: the theme, the last tier and the engine in `settings.ini` (**Settings**) |
| `settings_dialog.{hpp,cpp}` | `SettingsDialog`, Edit > Settings for the engine (**Settings**) |
| `about_dialog.{hpp,cpp}` | `AboutDialog`, the versions and the attributions (**About**) |
| `board_view.{hpp,cpp}` | `BoardView`, the board: position, input, animation (**Board**) |
| `board_scene.{hpp,cpp}` | `BoardScene`, the board's items and their layout, and the hit test |
| `board_theme.{hpp,cpp}` | `BoardTheme`, the board's colours, with the light and dark presets |
| `piece_set.{hpp,cpp}` | `PieceSet`, one `QSvgRenderer` per piece from the Qt resource |
| `promotion_chooser.{hpp,cpp}` | `PromotionChooser`, the four-button promotion popup |
| `squares.{hpp,cpp}` | Square numbers and names, UCI parsing, the placement read from the FEN |

All but `main.cpp` are the static library `app_lib` (namespace `chesscoach::app`, `AUTOMOC`),
which the executable and the UI tests link. The piece set and the two stylesheets
(`app/resources/light.qss`, `dark.qss`) are compiled into it as Qt resources.

## Shell

`MainWindow(data_dir, db)` is titled `chess-coach <version>` and holds:

- **A toolbar** (`toolbar`): *New game* (`new_game_action`: back to the start position),
  *Back* (`back_action`, the platform's Back shortcut: one move back), both disabled on the Spar
  screen, *Flip board*
  (`flip_action`), and the tier selector (`tier_selector`), a `QComboBox` of the seven
  `tier_label` strings with the tier index as each item's data, at the last tier the settings
  hold, else tier 2 (`MainWindow::default_tier`).
- **The menus**: *Edit > Settings…* (`settings_action`, **Settings**), *View > Theme* with
  *Light* and *Dark* (`theme_light`, `theme_dark`), and *Help* with *About chess-coach*
  (`about_action`) and *About Qt* (`about_qt_action`).
- **The evaluation bar** (`eval_bar`) and **the board** (`board`, a `BoardView`) on the left, and
  below them **the move list** (`move_list`, a `QListWidget`) with one row per move pair,
  `1. e4 e5`.
- **The screen switcher** on the right: a `QTabBar` (`screen_tabs`) over a `QStackedWidget`
  (`screens`) with Explore (**Explore**), Spar (**Spar**), then Repertoire and Drill, each a
  centred placeholder label for now.
- **The status bar**: `database_status`, "No database" or the `--db` file's name (its tooltip the
  full path); `data_version_status`, "Data version <v>" once the database is open; and
  `engine_status`, "Engine: <state>" as the engine worker reports it ("not started" until then,
  "not found", the engine's name when idle, "searching").

**The line.** The window keeps the moves played from the start position (UCI and SAN) and the key
of every position along them. On Explore, a move on the board (`move_played`) appends to it (on
Spar it goes to the session instead, **Spar**); clicking a row
of the move list goes to the position after that row's last move, *Back* drops the last move, and
*New game* drops them all: the window replays the line on a fresh `Position` into the board with
`set_position`, without animation. After every change, and when the tier changes, the window asks
the database worker for the position at the end of the line at the selected tier; after every
change of position (not of tier), and once at start, it asks the engine worker to evaluate it.

**The database.** With `--db`, the window asks the worker to open it at start; the status bar
names it and, once open, shows its data version. When it does not open, a non-blocking message
box (`database_error`) says why, the status bar says "No database", and Explore shows its empty
state; the board still plays. Without `--db` the window starts in that empty state.

The names in parentheses are the widgets' object names, which the UI tests find them by. Every
user-facing string goes through `tr()`.

## Database worker

`DbWorker` is a `QObject` moved to its own `QThread`, which the window starts at construction
and, in its destructor, quits and waits for; the worker is deleted on its thread when the thread
finishes (`deleteLater` on `finished`). It owns the `OpeningStore`, opened read-only from `--db`,
for the window's life, so the store is only ever used from that thread, and the GUI thread never
touches it. The window reaches it through two signals connected to its slots, every crossing
queued:

| Slot | Answer |
|---|---|
| `open_database(path)` | `database_opened(data_version)`, the file's `data_version` from `meta`, or `database_failed(message)` |
| `query(line_keys, tier)` | `position_ready(ExploreData)`: the position at the end of the line at the tier. Without an open database, nothing |

A store query that throws (the file failing after a successful open) closes the store and answers
`database_failed`. `ExploreData` carries the key and the tier, the FEN, the `positions` row and the
`nodes` row at the tier when there are any, the ECO code and name of the deepest named position
along the line (`docs/database.md` §10), the sum of the totals of the position's moves at tier 6
when it has a tier-6 node, and one `ExploreMove` per move at the tier, most played first and then
by UCI: its SAN (from a `Position` built from the stored FEN on the worker), UCI, badges, total
and results, its score (white's wins and half the draws over the total when white is to move,
black's when black is), its share at tier 6 when the position has a tier-6 node (§7.2: zero when
the move was not played there), and whether its child has a `nodes` row at the tier (§7.3).
`register_explore_types()` registers both types and the line of keys for the queued connections.
The window drops an answer for a key or a tier it has left.

## Engine worker

`EngineWorker` is a `QObject` on its own `QThread`, set up like the database worker: started by
the window, deleted on its thread when the thread finishes. It owns one core `Engine` and its own
`ProfileStore` connection to `<data-dir>/profiles.db` for the engine cache (`docs/database.md`
§12.2); the GUI thread touches neither. Its slots, reached by queued connection:

| Slot | What it does |
|---|---|
| `configure(EngineSettings)` | Keeps the settings for the next search and drops the running engine, which quits, so the next search starts one with them |
| `evaluate(key, fen, request_id)` | Answers `lines_ready(key, request_id, lines, cached)`, from the cache or a search, unless the request is stale; reports `engine_state(text)` and `engine_failed(message)` |

**An evaluation.** First the cache: `cache_lookup(key, lines, 12)`, a hit needing every line at
depth 12 or more, as the CLI's `eval`; a hit is answered with `cached` set and the engine is not
touched. On a miss the worker finds the engine: the settings' binary, else
`CHESS_COACH_STOCKFISH`, else `stockfish` on the `PATH`, else `stockfish` beside the app (the
bundle's, from Phase 6), and reports "not found" when there is none (`find_engine_binary`, which
the spar worker shares). The engine starts at the
first search (Threads and Hash from the settings) and again after it dies or times out. The worker
reports "searching", runs `search` for the settings' line count and seconds, reports the engine's
name, and stores a full result with `cache_store`, answering with what the cache then holds.
`profiles.db` is opened only when it exists, for a lookup, or when a search is to be stored, so
it is created by the first stored search and a run that searches nothing writes nothing.

**One search at a time, cancelled by moving on.** The worker's slots run one at a time on its
thread, and `Engine::search` blocks there, so two searches are never in flight. Each request the
window sends carries the next request id; before sending it, the window calls
`cancel_before(request_id)` directly from the GUI thread, which records the id and calls
`Engine::stop()` (the one engine call another thread may make; the worker's mutex only keeps the
engine from being replaced meanwhile). The search in progress then returns at once, marked
stopped, and is neither stored nor answered; a request older than the recorded id is skipped
unsearched; and the window drops an answer whose id or key is not its current one. The window
never blocks: a 30 s search is stopped by the next move. Closing the window cancels the same way
before it quits and joins the thread, and the engine quits with the worker.

**Scores.** `EngineLine` carries the multipv, the depth, the score and the principal variation in
SAN (at most ten moves, then "…"). UCI and the engine cache give scores from the side to move;
the worker turns them to white's side, negating them when black is to move, so that they read
like the stored evaluations of `openings.db`. Explore shows them that way.

## Explore

`ExploreScreen` shows the worker's answer:

- **Above the table**: the opening name (`explore_name`, or "Unnamed"); the stored evaluation
  (`explore_evaluation`), "+0.3" for centipawns as pawns from white's side, "M3" or "-M3" for a
  mate for white or black, or "no evaluation"; and the node's games at the tier (`explore_games`).
- **The table** (`explore_table`, a `QTableView` over `ExploreModel`): Move (SAN), Badges (the
  letters B, P and D, with a tooltip naming them), Games, Result (a delegate painting the white,
  draw and black shares as a bar with their percentages), Score % (one decimal), Top tier % (one
  decimal, or an em dash without a tier-6 node), and In book (yes or no). The rows keep the
  worker's order. Activating a row, by double-click or by Return or Enter, plays its move on the
  board (`BoardView::play_move`), which then goes through the line like any other move.
- **Off book**: when the position has no node at the tier, a label (`explore_message`) replaces
  the table: "Off book at <tier>: 0 games". The same label holds the empty state without a
  database.
- **The engine's lines**, below: a label (`engine_label`), "Engine", "Engine (cached)", or
  "Engine: searching", "Engine: not found" or the failure, and a short list (`engine_lines`) with
  one line per multipv, "1: depth 18 -0.3 Nc6 Bb5 Nf6", the score from white's side written as
  the stored evaluation is.

The model updates by removing and inserting rows rather than resetting, so the view keeps its
column settings (Result stretches; the others fit their contents).

`EvalBar` is the vertical bar beside the board: white's share of it is
`1 / (1 + e^(-0.00368208 * cp))` for a centipawn score, the curve lichess draws its bar with, and
all or nothing for a mate. It shows the position's stored evaluation when it has one, else the
engine's first line, and is hidden when there is neither; it follows the board's flip.

## Spar

**The worker.** `SparWorker` is a `QObject` on its own `QThread` for the window's life, set up
like the other two. It owns the live core `SparSession` (`docs/reference/core.md`), its own
`OpeningStore` connection to `--db` (read-only, opened at the first start), its own `Engine` for
the coach's replies off book, and its own `ProfileStore` connection for saving; the GUI thread
touches none of them, and the engine worker's engine is not this one. Its slots, reached by
queued connection:

| Slot | Answer |
|---|---|
| `configure(EngineSettings, seconds)` | Keeps the engine settings and each reply's seconds, and drops a running engine |
| `start(tier, user_white, seed, end_move)` | `session_started(seed, fen, SparState)`, then, when the user plays black, the coach's first move; a running session is saved as stopped first; `session_failed(message)` without a database |
| `user_move(uci)` | `move_accepted(san, uci, interruption, SparState)`, the interruption when the coach interrupts the move, then the coach's reply, `coach_moved(san, uci, SparState)`, unless the session ended |
| `end_session()` | Saves the running session as stopped: `session_ended(SparSummary)` |
| `list_sessions()` | `sessions_ready(sessions)`: the Default profile's saved sessions, newest first |
| `replay_session(id)` | `replay_ready(SparReplay)`: a saved session played again from its seed, with each interruption at its ply |

`SparState` carries the phase as text ("In book", "Off book: engine replies", "Ended"), the move
number, whose turn it is, and whether the session ended. The coach's reply comes from the sampler in
book and from the engine off book: the engine starts at the first off-book reply, with Threads and
Hash from the engine settings and the binary found as the engine worker finds it, and searches
through `EngineReplyProvider` at the session's tier (`ProviderConfig{seconds, elo_offset = 100}`),
again after it dies. A reply that fails, or a read of a corrupt opening database, which throws,
reports `session_failed`, and the session waits for End session. When the session reaches its move
limit, or is ended, the worker saves it with `ProfileStore::save_spar_session` under the Default
profile (`profiles.db` is created then if it is absent), with its start time and, when it ended
rather than being stopped, its end time; it answers `session_ended` with the summary (the seed, the
moves in SAN, the interruptions, the phase reached, the result and the saved id) and then the list.
A replay runs `replay_spar_session` on the saved row, with its seed, moves, end move and
`min_games`, and is refused, with a message, for a session played against another `data_version` of
the opening database, whose book replies may differ. Closing the window while a session runs does
not save it.

**The window.** While Spar is the current screen, the board is the session's: a move on it goes
to `user_move`, and the coach's replies are played on it with their animation. Explore's line,
query and evaluation wait as they were: switching to Spar cancels Explore's engine search and
hides the evaluation bar, which is Explore's position's, and switching back puts Explore's
position, its table and its evaluation back. The tier selector is disabled while a session runs,
which is played at the tier selected when it started. The board takes a move only when a session
runs, it is the user's turn, and no interruption is open; the window turns the board to the
user's side at the start. The move list shows the session's moves, or a replay's up to its ply,
and clicking a row does nothing on Spar.

**The screen.** `SparScreen` holds:

- the colour (`spar_white`, `spar_black`), *Start* (`spar_start`) and *End session*
  (`spar_end`), and the status line (`spar_status`): "In book · move 3", "Off book: engine
  replies · move 5", "Ended", or a message;
- **the interruption panel** (`spar_interruption`), shown when the coach interrupts a move: the
  reason and the move (`spar_reason`, "Dubious move: a3" or "Off book: h4"), the move's badges
  and its loss in centipawns when it has one (`spar_played`, "a3 played: PD, loses 109 cp"), the
  better choices (`spar_better`: Move, Badges, Games and Result, with the result bar), and
  *Continue* (`spar_continue`). The move stands; Continue closes the panel and gives the board
  back;
- **the summary** (`spar_summary`, `spar_summary_text`), when a session ends: whether it was
  stopped, ended at its move limit or ended in a result, the seed, the interruptions, the phase
  reached and the moves;
- **the saved sessions** (`spar_sessions`): Date, Tier (its band), Colour, Moves (plies),
  Interruptions and Result ("stopped" for one ended early, else the game's result when it has
  one, else "move limit"), newest first, and *Replay* (`spar_replay`), which plays the selected one
  again: *Previous* and *Next* (`spar_previous`, `spar_next`) step through it one ply at a time
  with the position (`spar_replay_position`, "ply 3 of 6") on the board, and each interruption is
  shown read-only, without Continue, after the move it interrupted. *Start* ends the replay.

## Settings

`Settings` reads and writes `<data-dir>/settings.ini` through `QSettings` in INI format, never the
registry or the platform's own settings store: `theme` (`light` or `dark`), `tier` (the last
tier selected), and the engine's `engine/binary` (empty for the default binary),
`engine/threads` (half the cores, at least one), `engine/hash_mb` (64), `engine/seconds` (2.0,
0.1 to 60) and `engine/lines` (3, 1 to 10), and sparring's `spar/end_move` (25, 1 to 200: the
move number that ends a session), `spar/seconds` (0.5, 0.1 to 60: each off-book reply's search)
and `spar/seed` (unset: each session's seed is the clock's seconds; set, every session uses it),
each at its default when unset or out of range. The spar settings have no dialog yet.
*Edit > Settings…* (`SettingsDialog`, `settings_dialog`) edits the engine's: the binary with a
Browse button, the threads, the hash, the seconds and the lines; accepting saves them, sends them
to the engine worker, which restarts its engine with them at the next search, and evaluates the
position again. Reading does not create anything; the first write, when the theme or the tier
changes, creates the data directory and the file. This is the first thing the app writes: a run
that changes no setting leaves the data directory as it found it, or absent.

## Themes

Each theme is one stylesheet, `app/resources/light.qss` and `dark.qss`, compiled in under
`:/themes/` and applied to the whole application with `QApplication::setStyleSheet`; the board
follows with `BoardTheme::light()` or `BoardTheme::dark()` (`BoardView::set_theme`). *View >
Theme* switches and saves the choice; the window applies the saved one at start.

## About

*Help > About chess-coach* opens `AboutDialog` (`about_dialog`, non-blocking): the app's version,
the open database's data version, and the attributions of `NOTICE` in plain text without links:
Qt under the LGPL, chess-library under MIT, the cburnett pieces by Colin M.L. Burnett from
Wikimedia Commons under CC BY-SA 3.0, the Lichess data under CC0, SQLite in the public domain, and
the other libraries with their licences. *About Qt* is Qt's own dialog.

## Board

`BoardView` is a `QGraphicsView` over a `BoardScene`. Its interface:

| Member | What |
|---|---|
| `set_position(const Position&)` | Shows a position at once, without animation, with no piece picked, no last move, and the check highlight set from the position |
| `position()` | The view's own copy of the position, which its moves are played on |
| `flip()`, `flipped()` | Black at the bottom, or white again |
| `move_played(QString uci, QString san)` | Emitted after a legal move is played: its UCI and its SAN in the position before it |
| `board_scene()`, `animating()` | Read access for tests: the scene's state, and whether a move is still moving |

**Input** is a press, a drag and a release, handled by the view itself (the scene's items take
no events). A press on a piece of the side to move picks it: the square is highlighted and its
targets get hints, a dot on an empty square and a ring on a capture (en passant included). Then:

- **Two clicks:** a press on one of the picked piece's targets plays the move, animated. A press
  on another piece of the side to move picks that one instead; a press anywhere else drops the
  pick.
- **Drag and drop:** moving the pointer past the platform's drag distance with the button held
  drags the piece under it; a release on a target plays the move, not animated, since the piece
  is already there; a release anywhere else puts the piece back and drops the pick. A release
  without a drag leaves the piece picked for a second click.
- **Promotion:** a move to the last rank with a pawn first shows `PromotionChooser`, a column of
  queen, rook, bishop and knight over the promotion square's file, the queen nearest the square.
  Each button carries an action (`promote_q`, `promote_r`, `promote_b`, `promote_n`) that plays the
  move with that piece; a press on the board instead cancels the move. The popup is a child of
  the viewport, so it needs no window of its own and shows in the board's screenshots.

Legality is never the view's: the targets are the position's `legal_moves_uci()` from the
picked square, and a move is played with `play_uci`, whose `PlayedMove` gives the SAN.

**Hit testing** is ours, in board coordinates: the event's viewport position goes through
`mapToScene`, and `BoardScene::square_at` turns the scene point into a square, or none off the
board. The scene measures `BoardScene::square_size` (100) units per square with the top-left
corner at the origin: unflipped, column = file and row = 7 − rank; flipped, column = 7 − file and
row = rank. `square_rect(square)` is the inverse. Squares are numbered `rank * 8 + file`, a1 = 0,
as `docs/database.md` §5.2 numbers them.

**Drawing.** The scene holds 64 square items, the coordinates (file letters in the bottom row's
bottom-right corners, rank digits in the left column's top-left corners, each in the other
shade's colour, moved by the flip), one `QGraphicsSvgItem` per piece, and the highlights: the last
move's from and to squares, the picked square, a red glow on the king of the side to move when it
is in check, and the hints. Stacking: squares, highlights, coordinates, pieces, hints, then a
dragged piece above everything. The view fits the scene in its viewport keeping the aspect
ratio, so squares stay square at any size. The pieces are rebuilt from the position's placement
(read from the first field of its FEN, for display only) after every move.

**Animation.** A clicked move slides the moving piece from its square to its target over
`BoardView::animation_ms` (150 ms), a `QVariantAnimation` on the item's position with an
out-cubic curve; castling slides the rook too. A new move, a press, `set_position` or `flip`
stops any animation still running and puts every piece on its square.

## Theme and pieces

`BoardTheme` holds the board's colours: the light and dark squares, the last move, the picked
square, the check glow, the hints, and the coordinates on each shade. `BoardTheme::light()` is
the default and `BoardTheme::dark()` the dark theme's; a view takes its theme at construction and
`set_theme` recolours it (**Themes**).

`PieceSet` loads the twelve pieces of a set from the Qt resource as `QSvgRenderer`s, one per
piece type, which every item drawing that piece shares; `pixmap(piece, size)` draws one for the
promotion buttons. The cburnett set is in `data/pieces/cburnett/` (its README gives the source,
Wikimedia Commons, and the licence, CC BY-SA 3.0) and is compiled in by `qt_add_resources` under
`:/pieces/cburnett/<w|b><K|Q|R|B|N|P>.svg`, so the binary needs no file beside it to draw the
board.

## UI harness

The UI tests are Qt Test classes, `app/tests/ui_<name>_test.cpp`, each one ctest test labelled
`app;ui` through `chesscoach_add_test(... KIND ui ...)` and run under the offscreen platform, so
they need no display. They drive the widgets with `QTest`'s mouse events on the board's viewport
at the centres of squares and find the window's widgets by object name.
`app/tests/ui_harness.hpp` holds the helpers: `child<T>(parent, name)` (the widget of that object
name, stopping the run when it is missing), `click(view, "e2")`, `square_point(view, "e2")`,
`taken(spy)` for the moves a `QSignalSpy` on `move_played` recorded, and
`save_screenshot(widget, test, state)`, which saves `QWidget::grab()` as
`build/<preset>/ui-screenshots/<test>-<state>.png` (the directory is the compile definition
`CHESSCOACH_UI_SCREENSHOT_DIR`).

- `ui_board_test`: the start position's 32 pieces on their squares; two clicks playing 1. e4 with
  its hints, animation and last-move highlight; a drag from g1 to f3 after 1. e4 e5; illegal
  clicks and an illegal drop playing nothing; the flip mirroring the squares, the coordinates and
  the hit test; a promotion through the popup, and its cancellation; the check highlight after
  1. e4 f5 2. Qh5+; `set_position` from a FEN. Screenshots: `board-start`, `board-e2-picked`,
  `board-after-e4`, `board-dragging`, `board-flipped`, `board-promotion`, `board-check`,
  `board-fen`.
- `ui_main_window_test`: the title, the tier selector's seven tiers, the move list following the
  board and New game clearing it, Flip board, the four screens, the status bar without a
  database. Screenshots: `main-window-start`, `main-window-after-moves`, `main-window-flipped`.
- `ui_explore_test`, on a fixture database the test writes into `core`'s schema with real keys
  (`app/tests/fixture_db.cpp`, the CLI fixture's rows): opening it, with the data version, the
  start position off book at tier 2 and its moves with badges, bars, scores and top-tier shares at
  tier 3; switching tiers; moves on the board showing the replies and the deepest name; playing a
  row by double-click and by Return; going back by the move list, *Back* and *New game*; an
  off-book position; the evaluation bar for a centipawn score, a mate and no evaluation; a
  database that does not open; `settings.ini` appearing only after a theme change, alone in the
  data directory; both themes; the About dialog. Screenshots: `explore-start-tier3`,
  `explore-sicilian-light`, `explore-off-book`, `explore-light`, `explore-dark`, `about-dialog`.
- `ui_engine_test`, with core's fake engine (`fake-uci`) set as the binary through
  `app/tests/fake-stockfish.sh`, its script, delay and log chosen by environment variables: the
  lines after a request, white to move and black to move, and the evaluation bar following the
  first line where nothing is stored; a cache seeded through `ProfileStore` served without
  starting the engine; a 30 s search stopped by a move while a timer keeps firing, and the window
  closing at once; a missing binary; the Settings dialog saving and applying its values, and a new
  window starting with them. Screenshots: `explore-engine`, `explore-engine-dark`,
  `settings-dialog`.
- `ui_spar_test`, on the fixture database's tier-4 tree (the start position's e4, a Dubious a3
  and d4, black's e5 after 1. a3, and white's Nf3 alone after 1. a3 e5) with the fake engine's
  `legal` script as the coach off book, a fixed seed and a move limit of four: a session as white
  interrupted on 1. a3 with its better choices, Continue, interrupted again on 2. h4 off book and
  answered by the engine, ended at its move limit with the summary and the saved session listed,
  and replayed with the interruptions at plies one and three; a session as black, where the
  coach moves first and the board turns; End session saving a stopped session; and Explore's
  line untouched by a session. Screenshots: `spar-interruption`, `spar-summary`, `spar-replay`.

The window and Explore tests set `CHESS_COACH_STOCKFISH` to a path that does not exist, so no
real engine runs in them and they write nothing; the engine and Spar tests name the fake engine
in their settings.

`tools/ui_tests.sh [<regex>]` builds the `dev` preset, runs the UI tests (`ctest -L ui`, filtered
by name with the regex), and lists the screenshots of the run; the full gate runs the same tests
in the `gate` preset. A screenshot is how an agent sees the app: read the PNGs.

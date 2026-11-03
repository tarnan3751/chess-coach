# The app

`chess-coach` is the Qt 6 Widgets application (`CLAUDE.md`, **app/ is presentation**). This file
covers the main window, the board, the database worker, the Explore screen, the settings, the
themes, the About dialog, and the UI harness. The engine worker comes with the engine, later in
Phase 3 (`PLAN.md` §7).

Every widget lives on the GUI thread. The opening database (`--db`) is opened read-only and read
only by the database worker on its own thread (**Database worker**). The one file the app writes
is `<data-dir>/settings.ini`, and only when a setting changes (**Settings**).

## Code

| File | What |
|---|---|
| `app/src/main.cpp` | The entry point, wiring only: `QApplication`, `--data-dir` and `--db`, the window |
| `main_window.{hpp,cpp}` | `MainWindow`: the frame, the line played, the worker's thread (**Shell**) |
| `db_worker.{hpp,cpp}` | `DbWorker`, the database worker on its own thread (**Database worker**) |
| `explore_data.hpp` | `ExploreData` and `ExploreMove`, the worker's answer for a position |
| `explore_model.{hpp,cpp}` | `ExploreModel`, the table model of the Explore screen (**Explore**) |
| `explore_screen.{hpp,cpp}` | `ExploreScreen`, the Explore screen, and its result bar delegate |
| `eval_bar.{hpp,cpp}` | `EvalBar`, the evaluation bar beside the board |
| `settings.{hpp,cpp}` | `Settings`, the theme and the last tier in `settings.ini` (**Settings**) |
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
  *Back* (`back_action`, the platform's Back shortcut: one move back), *Flip board*
  (`flip_action`), and the tier selector (`tier_selector`), a `QComboBox` of the seven
  `tier_label` strings with the tier index as each item's data, at the last tier the settings
  hold, else tier 2 (`MainWindow::default_tier`).
- **The menus**: *View > Theme* with *Light* and *Dark* (`theme_light`, `theme_dark`), and
  *Help* with *About chess-coach* (`about_action`) and *About Qt* (`about_qt_action`).
- **The evaluation bar** (`eval_bar`) and **the board** (`board`, a `BoardView`) on the left, and
  below them **the move list** (`move_list`, a `QListWidget`) with one row per move pair,
  `1. e4 e5`.
- **The screen switcher** on the right: a `QTabBar` (`screen_tabs`) over a `QStackedWidget`
  (`screens`) with Explore (**Explore**), then Spar, Repertoire and Drill, each a centred
  placeholder label for now.
- **The status bar**: `database_status`, "No database" or the `--db` file's name (its tooltip the
  full path); `data_version_status`, "Data version <v>" once the database is open; and
  `engine_status`, "Engine: not started".

**The line.** The window keeps the moves played from the start position (UCI and SAN) and the key
of every position along them. A move on the board (`move_played`) appends to it; clicking a row
of the move list goes to the position after that row's last move, *Back* drops the last move, and
*New game* drops them all: the window replays the line on a fresh `Position` into the board with
`set_position`, without animation. After every change, and when the tier changes, the window asks
the worker for the position at the end of the line at the selected tier.

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

The model updates by removing and inserting rows rather than resetting, so the view keeps its
column settings (Result stretches; the others fit their contents).

`EvalBar` is the vertical bar beside the board: white's share of it is
`1 / (1 + e^(-0.00368208 * cp))` for a stored centipawn score, the curve lichess draws its bar
with, and all or nothing for a mate; it is hidden when the position has no stored evaluation, and
follows the board's flip.

## Settings

`Settings` reads and writes `<data-dir>/settings.ini` through `QSettings` in INI format, never the
registry or the platform's own settings store: `theme` (`light` or `dark`) and `tier` (the last
tier selected). Reading does not create anything; the first write, when the theme or the tier
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

`tools/ui_tests.sh [<regex>]` builds the `dev` preset, runs the UI tests (`ctest -L ui`, filtered
by name with the regex), and lists the screenshots of the run; the full gate runs the same tests
in the `gate` preset. A screenshot is how an agent sees the app: read the PNGs.

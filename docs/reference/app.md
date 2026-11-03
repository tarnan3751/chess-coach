# The app

`chess-coach` is the Qt 6 Widgets application (`CLAUDE.md`, **app/ is presentation**). This file
covers the main window's frame, the board, its theme and piece set, and the UI harness. Explore's
data, the engine worker, settings and theme switching come later in Phase 3 (`PLAN.md` §7).

Everything here runs on the GUI thread and reads and writes nothing: `--data-dir` and `--db` are
parsed and passed to the window, which shows the database's file name and opens neither.

## Code

| File | What |
|---|---|
| `app/src/main.cpp` | The entry point, wiring only: `QApplication`, `--data-dir` and `--db`, the window |
| `main_window.{hpp,cpp}` | `MainWindow`, the frame (**Shell**) |
| `board_view.{hpp,cpp}` | `BoardView`, the board: position, input, animation (**Board**) |
| `board_scene.{hpp,cpp}` | `BoardScene`, the board's items and their layout, and the hit test |
| `board_theme.{hpp,cpp}` | `BoardTheme`, the board's colours, with the light and dark presets |
| `piece_set.{hpp,cpp}` | `PieceSet`, one `QSvgRenderer` per piece from the Qt resource |
| `promotion_chooser.{hpp,cpp}` | `PromotionChooser`, the four-button promotion popup |
| `squares.{hpp,cpp}` | Square numbers and names, UCI parsing, the placement read from the FEN |

All but `main.cpp` are the static library `app_lib` (namespace `chesscoach::app`, `AUTOMOC`),
which the executable and the UI tests link. The piece set is compiled into it as a Qt resource.

## Shell

`MainWindow(data_dir, db)` is titled `chess-coach <version>` and holds:

- **A toolbar** (`toolbar`): *New game* (`new_game_action`: the start position and an empty move
  list), *Flip board* (`flip_action`), and the tier selector (`tier_selector`), a `QComboBox` of
  the seven `tier_label` strings with the tier index as each item's data, at tier 2 to start
  (`MainWindow::default_tier`).
- **The board** (`board`, a `BoardView`) on the left, and below it **the move list**
  (`move_list`, a `QListWidget`) with one row per move pair, `1. e4 e5`, appended from the
  board's `move_played`.
- **The screen switcher** on the right: a `QTabBar` (`screen_tabs`) over a `QStackedWidget`
  (`screens`) with Explore, Spar, Repertoire and Drill, each a centred placeholder label for now.
- **The status bar**: `database_status`, "No database" or the `--db` file's name (its tooltip the
  full path), and `engine_status`, "Engine: not started".

The names in parentheses are the widgets' object names, which the UI tests find them by. Every
user-facing string goes through `tr()`.

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
the default; `BoardTheme::dark()` is ready for the theme switch. A view takes its theme at
construction.

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
`app/tests/ui_harness.hpp` holds the helpers: `click(view, "e2")`, `square_point(view, "e2")`,
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
  board and New game clearing it, Flip board, the four screens, the status bar. Screenshots:
  `main-window-start`, `main-window-after-moves`, `main-window-flipped`.

`tools/ui_tests.sh [<regex>]` builds the `dev` preset, runs the UI tests (`ctest -L ui`, filtered
by name with the regex), and lists the screenshots of the run; the full gate runs the same tests
in the `gate` preset. A screenshot is how an agent sees the app: read the PNGs.

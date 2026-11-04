# The app

`chess-coach` is the Qt 6 Widgets application (`CLAUDE.md`, **app/ is presentation**). This file
covers the main window, the board, the database worker, the engine worker, the Explore screen,
the Spar screen (labelled Practice) and its worker, the profile worker, the Repertoire and Drill
screens, export and import, the settings, the files, the wording, the themes, the Help and About
dialogs, and the UI harness.

Every widget lives on the GUI thread. The opening database, the file the window found at start or an
import put in place (**The database**), is opened read-only and read only by the database worker on
its own thread (**Database worker**); the engine and the engine cache belong to the engine worker on
another (**Engine worker**); a sparring session, with its own connection to the opening database and
its own engine, belongs to the spar worker on a third (**Spar**); the repertoires and cards, with
another connection to the opening database for coverage and the score, belong to the profile worker
on a fourth (**Profile worker**). The app writes these files in `--data-dir`: `settings.ini`, only
when a setting changes (**Settings**); `profiles.db`, only when the engine worker stores a search
(**Engine worker**), the spar worker saves a session (**Spar**), the profile worker changes a
repertoire or a card (**Profile worker**), or a profile is imported (**Export and import**), which
also leaves `profiles.db.previous`; and `openings.db` when a database is imported, which leaves
`openings.db.previous` (**The database**). Each import copies its file in first as
`profiles.db.importing` or `openings.db.importing`, which it renames into place. A profile export
goes to the file the user picks. Files are chosen with a chooser of the app's own, which keeps no
state (**Files**).

## Code

| File | What |
|---|---|
| `app/src/main.cpp` | The entry point, wiring only: `QApplication`, `--data-dir` and `--db`, the window |
| `main_window.{hpp,cpp}` | `MainWindow`: the frame, the line played, the workers' threads, export and import (**Shell**) |
| `main_window_repertoire.cpp`, `main_window_drill.cpp` | `MainWindow`'s Repertoire and Drill parts: the board on those screens and the profile worker's answers (**Repertoire**, **Drill**) |
| `db_worker.{hpp,cpp}` | `DbWorker`, the database worker on its own thread (**Database worker**) |
| `engine_worker.{hpp,cpp}` | `EngineWorker`, the engine and its cache on their own thread (**Engine worker**) |
| `explore_data.hpp` | `ExploreData` and `ExploreMove`, the worker's answer for a position |
| `explore_model.{hpp,cpp}` | `ExploreModel`, the table model of the Explore screen (**Explore**) |
| `explore_screen.{hpp,cpp}` | `ExploreScreen`, the Explore screen |
| `result_delegate.{hpp,cpp}` | The results bar and the badge pills; `ResultDelegate`, the bar of Spar's better choices (**Tables**) |
| `spar_worker.{hpp,cpp}` | `SparWorker`, the live session, its store connections and its engine on their own thread (**Spar**) |
| `spar_data.hpp` | The spar worker's answers: `SparState`, `SparInterruption`, `SparSummary`, the saved sessions, a replay |
| `spar_screen.{hpp,cpp}` | `SparScreen`, the Spar screen (**Spar**) |
| `profile_worker.{hpp,cpp}` | `ProfileWorker`, the repertoires and cards on their own thread (**Profile worker**) |
| `profile_data.hpp` | The profile worker's answers: the repertoire tree, coverage, scores, a PGN import's counts, the cards due |
| `repertoire_screen.{hpp,cpp}` | `RepertoireScreen`, the Repertoire screen (**Repertoire**) |
| `drill_screen.{hpp,cpp}` | `DrillScreen`, the Drill screen (**Drill**) |
| `default_profile.{hpp,cpp}` | The profile the app works on: Default, else the first |
| `tables.{hpp,cpp}` | The read-only tables of Spar and Repertoire; the Study table's header, rows and type sizes (**Tables**) |
| `file_chooser.{hpp,cpp}` | `FileChooser`, the app's own file chooser, and the functions that run it (**Files**) |
| `locations.{hpp,cpp}` | The app bundle's resources, the opening database the window opens, the message when there is none (**The database**) |
| `eval_bar.{hpp,cpp}` | `EvalBar`, the evaluation bar beside the board |
| `study_palette.{hpp,cpp}` | `StudyPalette`, the Study look's colours per theme (**Themes**) |
| `fonts.{hpp,cpp}` | `install_fonts`, the bundled fonts registered and the application font set (**Themes**) |
| `icons.{hpp,cpp}` | `themed_icon` and `tinted_icon`, the toolbar's icons in a theme's colours (**Themes**) |
| `settings.{hpp,cpp}` | `Settings`: the theme, the last tier, the engine, Spar and Repertoire in `settings.ini` (**Settings**) |
| `settings_dialog.{hpp,cpp}` | `SettingsDialog`, Edit > Settings (**Settings**) |
| `about_dialog.{hpp,cpp}` | `AboutDialog`, the versions and the attributions (**About**) |
| `help_dialog.{hpp,cpp}` | `HelpDialog`, Help > How to use chess-coach, and the toolbar's tooltips (**Help**) |
| `board_view.{hpp,cpp}` | `BoardView`, the board: position, input, animation (**Board**) |
| `board_scene.{hpp,cpp}` | `BoardScene`, the board's items and their layout, and the hit test |
| `board_theme.{hpp,cpp}` | `BoardTheme`, the board's colours, with the light and dark presets |
| `piece_set.{hpp,cpp}` | `PieceSet`, one `QSvgRenderer` per piece from the Qt resource |
| `promotion_chooser.{hpp,cpp}` | `PromotionChooser`, the four-button promotion popup |
| `squares.{hpp,cpp}` | Square numbers and names, UCI parsing, the placement read from the FEN |
| `wording.{hpp,cpp}` | How the app words a rating band, a count, a number, pawns, a day and a database's months (**Wording**) |

All but `main.cpp` are the static library `app_lib` (namespace `chesscoach::app`, `AUTOMOC`),
which the executable and the UI tests link. The piece set, the two stylesheets
(`app/resources/light.qss`, `dark.qss`), the fonts (`app/resources/fonts/`) and the icons
(`app/resources/icons/`) are compiled into it as Qt resources.

## Shell

`MainWindow(data_dir, db)` is titled `chess-coach <version>` and holds:

- **A toolbar** (`toolbar`), icon and label buttons: *New game* (`new_game_action`, the `plus`
  icon: back to the start position), *Back* (`back_action`, `back`, the platform's Back shortcut:
  one move back), both working on Explore's line and Repertoire's and disabled on Spar and Drill,
  *Flip board* (`flip_action`, `flip`); a separator; the label *Rating* (`rating_label`) and the
  tier selector (`tier_selector`), a `QComboBox` of the seven rating bands (**Wording**), each
  with its first letter capitalised ("Under 1200", "2200 and above"), with the tier index as each
  item's data, at the last tier the settings hold, else tier 2 (`MainWindow::default_tier`), its
  arrow the `chevron-down` icon in the muted colour; and at the right an icon-only *Settings*
  button (`settings_toolbar_action` on the button `settings_button`, `gear`), which opens Edit >
  Settings….
- **The menus**: *File* with *Back up your work…* and *Restore a backup…* (`export_action`,
  `import_action`, **Export and import**) and *Import database…* (`import_database_action`, **The
  database**), *Edit > Settings…* (`settings_action`, **Settings**), *View > Theme* with *Light* and
  *Dark* (`theme_light`, `theme_dark`), and *Help* with *How to use chess-coach* (`help_action`,
  **Help**), *About chess-coach* (`about_action`) and *About Qt* (`about_qt_action`).
- **The evaluation bar** (`eval_bar`, 14 px wide) and **the board** (`board`, a `BoardView`) on
  the left, 8 px apart at the top of the column, the board square and the bar always exactly its
  height; below them **the move list** (`move_list`, a `QListWidget`) with one row per move pair,
  `1. e4 e5`, which a delegate paints with the move number muted in a column of its own and the
  last move, the board's, marked in the selection colour. The window opens at 1200 × 780, its
  content 14 px from its edges and the two columns 14 px apart. The board column is as wide as the
  board the window's height allows, leaving the screen column at least 700 px, which takes the
  rest; the board is never less than 300 px, and the window's minimum width is what the two
  columns' minimums need, so nothing clips. At the default size the board is 436 px.
- **The screen switcher** on the right: a `QTabBar` (`screen_tabs`) over a `QStackedWidget`
  (`screens`) with Explore (**Explore**), Practice (**Spar**), Repertoire (**Repertoire**) and Drill
  (**Drill**), the names in Source Serif 4, the current one over a rule in the accent which covers
  the hairline under the whole strip. The board belongs to the current screen: a move on it goes
  to that screen's line, session or card, and the evaluation bar and Explore's engine are idle on
  the other three.
- **The status bar**, two labels: `database_status` on the left, the opening database by its
  months (`meta`'s, in English: "October 2026", or "October 2026 to March 2027" for a run) and its
  data version, "Opening database: October 2026, data version 1" ("Opening database: data version
  1" when `meta` names no months), "Loading the opening database…" until the worker answers, or
  "No opening database", its tooltip the file's full path and where it was found, "… (data
  directory)", "(--db)" or "(app bundle)"; and `engine_status` on the right, the engine as the
  engine worker reports it by `EngineState` (`not_started`, `starting`, `ready`, `searching`,
  `not_found`, `failed`) beside the engine's name, never by the worker's words: "Engine not
  started" until then, "Engine starting…", "<name> ready" when idle ("Stockfish 17 ready"),
  "<name> thinking…" while it searches, "No engine found", or "The engine did not start" for a
  binary that does not answer the handshake. Explore's engine panel shows the worker's words for
  the same state, its `detail`.

**The line.** The window keeps the moves played from the start position (UCI and SAN) and the key of
every position along them. On Explore, a move on the board (`move_played`) appends to it (on the
other screens it goes to theirs); clicking a row of the move list goes to the position after that
row's last move, *Back* drops the last move, and *New game* drops them all: the window replays the
line on a fresh `Position` into the board with `set_position`, without animation. After every
change, and when the tier changes, the window asks the database worker for the position at the end
of the line at the selected tier; after every change of position (not of tier), and once at start,
it asks the engine worker to evaluate it.

**The database.** At start the window takes the file `--db` names, whether or not it is there, so
that a missing one is reported rather than passed over; without `--db` it takes the first file that
exists of `<data-dir>/openings.db` and the app bundle's `Contents/Resources/openings.db` (core's
`locate_opening_database`, through `find_opening_database` in `locations.cpp`), asks the database
worker to open it, and hands its path to the spar and profile workers, which open it when they first
need it. The status bar names it by its months and data version once it is open, with its full path
and where it was found as its tooltip. When it does not open, a non-blocking message box
(`database_error`) says why in a sentence ("The opening database … could not be opened. It is not
a database file, or it is damaged. Import one with File > Import database."), the status bar says
"No opening database", and Explore's empty state names the file: "There is no opening database at
…. Check the file given with --db." for a missing `--db`, or that it could not be opened, with the
import; the board still plays. When there is none, Explore's empty state says to import one with
File > Import database and names the three places it looks, "inside the app" being the bundle.

**The bundle.** The bundle's resources are `<the executable's folder>/../Resources` when the
executable runs inside a `.app`, else there are none (`bundle_resources`). The window works them out
once, on the GUI thread at start, and hands them to the engine and spar workers and the Settings
dialog, which look for the bundled engine there. The environment variable
`CHESS_COACH_BUNDLE_RESOURCES`, read only when it is set, names another folder in their place: the
UI tests' hook, which a scratch folder stands in for the bundle through.

**Importing a database.** *File > Import database…* (`import_database_action`) asks for a file with
the file chooser (`*.db`, **Files**), then sends it to the database worker
(`DbWorker::import_database`), which closes its store and runs core's `import_opening_database` on
its own thread: the copy of a release database takes seconds and has no progress to show, so the
window shows a window-modal busy dialog (`database_import_progress`), which keeps repainting and
which neither Escape nor a close dismisses, and disables both of the File menu's imports until the
worker answers. The import checks the file through `OpeningStore`, copies it to
`openings.db.importing` and renames it into place as `<data-dir>/openings.db`, keeping the file it
replaces as `openings.db.previous`; it refuses a file that is not an opening database this build
reads, a newer schema, the data directory's own `openings.db` and any other file inside the data
directory. On success the worker answers `database_imported` and opens the new file, which refreshes
Explore, the status bar and About; the spar and profile workers read it from then on (a session
running keeps reading the file it started on until it ends), and a message box (`database_imported`)
gives its data version and the months it holds: "The new opening database is ready (data version
7). It holds games from January 2013." On failure it answers `database_import_failed`, a message
box (`database_error`) says why, "junk.db was not imported. It is not a database file, or it is
damaged.", and the worker opens the old file again, when there was
one: after a `--db` that named no file it stays without a store, and the failure is the one box.

The names in parentheses are the widgets' object names, which the UI tests find them by. Every
user-facing string goes through `tr()`.

## Database worker

`DbWorker` is a `QObject` moved to its own `QThread`, which the window starts at construction and,
in its destructor, quits and waits for; the worker is deleted on its thread when the thread finishes
(`deleteLater` on `finished`). It owns the `OpeningStore`, opened read-only from the file the window
found (**The database**) and replaced by an import's (**Importing a database**), so the store is
only ever used from that thread, and the GUI thread never touches it. The window reaches it through
two signals connected to its slots, every crossing queued:

| Slot | Answer |
|---|---|
| `open_database(path)` | `database_opened(data_version, months)`, the file's `data_version` and `months` from `meta`, or `database_failed(message)` |
| `query(line_keys, tier)` | `position_ready(ExploreData)`: the position at the end of the line at the tier. Without an open database, nothing |
| `import_database(data_dir, source, current)` | `database_imported(path, data_version, months)` and the new file opened, or `database_import_failed(message)` and `current` opened again (**Importing a database**) |

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
| `evaluate(key, fen, request_id)` | Answers `lines_ready(key, request_id, lines, cached)`, from the cache or a search, unless the request is stale; reports `engine_state(state, name, detail)` and `engine_failed(message)` |

**An evaluation.** First the cache: `cache_lookup(key, lines, 12)`, a hit needing every line at
depth 12 or more, as the CLI's `eval`; a hit is answered with `cached` set and the engine is not
touched. On a miss the worker finds the engine: the settings' binary, else `CHESS_COACH_STOCKFISH`,
else `stockfish` on the `PATH`, else the app bundle's `Contents/Resources/engine/stockfish`, where
the packaging puts it (**The bundle**), and reports "not found" when there is none
(`find_engine_binary`, which the spar worker shares), and "failed to start" when the binary does not
answer the UCI handshake. The engine starts at the first search (Threads and Hash from the settings)
and again after it dies or times out, the worker reporting "starting" meanwhile and the engine's
name once it has started. The worker reports "searching", runs `search` for the settings' line
count and seconds, reports the engine's name, and stores a full result with `cache_store`,
answering with what the cache then holds. `profiles.db` is opened only when it exists, for a
lookup, or when a search is to be stored, so it is created by the first stored search and a run
that searches nothing writes nothing.

**One search at a time, cancelled by moving on.** The worker's slots run one at a time on its
thread, and `Engine::search` blocks there, so two searches are never in flight. Each request the
window sends carries the next request id; before sending it, the window calls
`cancel_before(request_id)` directly from the GUI thread, which records the id and calls
`Engine::stop_before(request_id)` (the engine calls another thread may make are the stops; the
worker's mutex only keeps the engine from being replaced meanwhile). Every search carries its
request id as its generation, so the search in progress of an older request returns at once,
marked stopped, and is neither stored nor answered, and so does one the worker was just about to
start when the stop came, which sends nothing to the engine; a request older than the recorded id
is skipped unsearched; and the window drops an answer whose id or key is not its current one. The window
never blocks: a 30 s search is stopped by the next move. Closing the window cancels the same way
before it quits and joins the thread, and the engine quits with the worker.

**Scores.** `EngineLine` carries the multipv, the depth, the score and the principal variation in
SAN (at most ten moves, then "…"). UCI and the engine cache give scores from the side to move; the
worker turns them to white's side, negating them when black is to move, so that they read like the
stored evaluations of `openings.db`. Explore shows them that way. UCI's mate 0, the side to move
checkmated, has no sign to turn: the line carries the mated side instead (`mated`), shown as "M0"
when black is mated and "-M0" when white is, and the evaluation bar fills for the winner.

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
engine's first line, and is empty when there is neither, keeping its width so that the board
beside it does not move; it follows the board's flip. It is empty on the other screens.

## Tables

The Study look's parts for a table that paints itself (`tables.{hpp,cpp}`,
`result_delegate.{hpp,cpp}`), in the colours of a `StudyPalette` its owner keeps:

- **The header** (`TableHeader`, a `QHeaderView`): the `chrome` fill with a `border` hairline
  under it; each label in 11.5 px semibold `muted`, 10 px from the section's edge, at the left or
  the right as the model's `Qt::TextAlignmentRole` for the section says, and its note
  (`header_note_role`) after it in the regular weight; the sections' tooltips are the model's.
- **The rows**: the body `panel` inside a hairline frame, no grid and no alternate colours; a cell
  has 10 px of padding left and right and 5 px above and below, a `row_line` rule under every row,
  and the selected row filled with `selection` (`paint_row`). Type sizes are in pixels, fractions
  included, through the point size at the device's DPI (`sized_font`); numbers take tabular
  figures where Qt can ask the font for them (6.7 and later, `tabular`).
- **The pills** (`paint_badge_pills`): Book, Popular and Dubious in that order, each in its
  badge's colours, 11.5 px semibold, 1 px by 7 px of padding, 2 px corners, 4 px apart.
- **The results bar** (`paint_results_bar`): 150 × 16 px after the cell's padding, a hairline
  frame round white wins, draws and black wins in the results colours, with the white and black
  percentages inside their segments in 10 px bold, and the draws' only in a segment 18 px wide or
  more (in the white segment's text colour); a percentage that does not fit its segment is left
  out. `ResultDelegate` paints it over the style's cell for Spar's better choices, in the light
  palette (Spar has none of its own yet).

## Spar

The Spar screen is labelled **Practice** in the app: its tab, its buttons and its messages say
"practice game", while the code, the CLI's `spar` and this section keep "spar" (`PLAN.md` §11,
2026-11-04).

**The worker.** `SparWorker` is a `QObject` on its own `QThread` for the window's life, set up
like the other two. It owns the live core `SparSession` (`docs/reference/core.md`), its own
`OpeningStore` connection to `--db` (read-only, opened at the first start), its own `Engine` for
the coach's replies off book, and its own `ProfileStore` connection for saving; the GUI thread
touches none of them. Its engine is not the engine worker's: while a session is off book two
engine processes run, Explore's idle. Its slots, reached by queued connection:

| Slot | Answer |
|---|---|
| `configure(EngineSettings, seconds)` | Keeps the engine settings and each reply's seconds, and drops a running engine |
| `start(tier, user_white, seed, end_move)` | `session_started(seed, fen, SparState)`, then, when the user plays black, the coach's first move; a running session is saved as stopped first; `session_failed(message)` without a database |
| `user_move(uci)` | `move_accepted(san, uci, interruption, SparState)`, the interruption when the coach interrupts the move, then the coach's reply, `coach_moved(san, uci, SparState)`, unless the session ended |
| `end_session()` | Saves the running session as stopped: `session_ended(SparSummary)` |
| `list_sessions()` | `sessions_ready(sessions)`: the Default profile's saved sessions, newest first |
| `replay_session(id)` | `replay_ready(SparReplay)`: a saved session played again from its seed, with each interruption at its ply |

`SparState` carries the phase as a sentence ("You are in the book.", "You have left the book, so
the engine replies.", "The practice game is over."), the move number, whose turn it is, and whether the session ended. The coach's reply comes from the sampler in
book and from the engine off book: the engine starts at the first off-book reply, with Threads and
Hash from the engine settings and the binary found as the engine worker finds it, and searches
through `EngineReplyProvider` at the session's tier (`ProviderConfig{seconds, elo_offset = 100}`),
again after it dies; each session after the first sends the running engine `ucinewgame`
(`Engine::new_game`), as UCI asks between games. A reply that fails, or a read of a corrupt opening
database, which throws, reports `session_failed`, and the session waits for End the game. When the
session reaches its move limit, or is ended, the worker saves it with
`ProfileStore::save_spar_session` under the Default profile (`profiles.db` is created then if it is
absent), with its start time and, when it ended rather than being stopped, its end time; it answers
`session_ended` with the summary (the seed, the moves in SAN, the interruptions, the phase reached,
the result and the saved id) and then the list. A replay runs `replay_spar_session` on the saved
row, with its seed, moves, end move and `min_games`, and is refused, with a message, for a session
played against another `data_version` of the opening database, whose book replies may differ; a
session started from the same row then plays the record ply by ply, the user's moves through
`play_user_move` and the coach's through `coach_reply`, so each interruption is placed at the ply
where the session raised it. Closing the window while a session runs saves it as stopped: the window
calls `stop_coach()` from the GUI thread, which stops the coach's search in progress and keeps
another from starting (the one call another thread makes, under the worker's mutex), then waits on
`end_session` (a blocking queued call) before it joins the thread, so the wait is a save, not a
search.

**The window.** While Spar is the current screen, the board is the session's: a move on it goes to
`user_move`, and the coach's replies are played on it with their animation. Explore's line, query
and evaluation wait as they were: switching to Spar cancels Explore's engine search and empties the
evaluation bar, which is Explore's position's, and switching back puts Explore's position, its table
and its evaluation back. The tier selector is disabled while Spar is current and a session runs,
which keeps the tier selected when it started; on Explore the selector is Explore's, and changing it
there leaves the session's tier alone. The board takes a move only when a session runs, it is the
user's turn, and no interruption is open; the board faces the user: the colour of the session
running, of the replay, or chosen for the next. Explore keeps its own side of the board, which *Flip
board* sets there, and gets it back on return. The move list shows the session's moves, or a
replay's up to its ply, and clicking a row does nothing on Spar.

**The screen.** `SparScreen` holds:

- its description (`spar_description`), in the muted colour (**Themes**): "Play an opening against
  the database. The coach stops you when you leave the book and shows the better choices.";
- the colour, *Play White* and *Play Black* (`spar_white`, `spar_black`), *Start a practice game*
  (`spar_start`) and *End the game* (`spar_end`), and the status line (`spar_status`): "Move 3.
  You are in the book.", "Move 5. You have left the book, so the engine replies.", "The practice
  game is over.", or a message;
- **the interruption panel** (`spar_interruption`), shown when the coach interrupts a move: the
  reason, a sentence naming the move (`spar_reason`, "a3 is Dubious (popular here, but it loses
  ground)." or "h4 leaves the book. The database has too few games at this rating after it."),
  the move's badges when the reason has not named them, and its loss in pawns (`spar_played`,
  "The engine says it gives away about 1.1 pawns.", or that the database has no evaluation for
  it), the better choices (`spar_better`: Move, Badges by name, "Book, Popular", Games and
  Results, with the result bar), and *Keep playing* (`spar_continue`). The move stands; Keep
  playing closes the panel and gives the board back;
- **the summary** (`spar_summary`, `spar_summary_text`), when a session ends: how it ended ("You
  ended this practice game early.", "The practice game reached its move limit.", "White won the
  practice game."), how often the coach stopped the user ("The coach stopped you twice."), how far
  it went ("You left the book, and the engine played the rest."), and the moves;
- **the saved sessions** (`spar_sessions`, under "Your saved practice games"): Date ("4 November
  2026, 14:05"), Rating (its band), You played, Moves (white's and black's together, so six plies
  are three), Stops and Result ("Ended early" for one ended early, else "White won", "Black won"
  or "Draw" when the game has a result, else "Move limit"), newest first, and *Replay this game*
  (`spar_replay`), which plays the selected one again: *Previous move* and *Next move*
  (`spar_previous`, `spar_next`) step through it one ply at a time with the position
  (`spar_replay_position`, "Step 3 of 6") on the board, and each interruption is shown read-only,
  without Keep playing, after the move it interrupted. *Start a practice game* ends the replay.

## Profile worker

`ProfileWorker` is a `QObject` on its own `QThread` for the window's life, set up like the
others. It owns a `ProfileStore` connection to `<data-dir>/profiles.db`, through which every read
and change of the repertoires and cards goes, under the Default profile (`default_profile`: the
profile named Default, else the first, as the spar worker saves its sessions), and its own
read-only `OpeningStore` connection to `--db` for coverage and the score; the GUI thread touches
neither. `profiles.db` is opened when it exists or when a slot writes, so looking at an empty
repertoire creates nothing. Its slots, reached by queued connection:

| Slot | Answer |
|---|---|
| `set_choice(white, key, fen, uci)`, `remove_choice(white, key)` | `repertoire_changed(white, message)`; the store keeps the node's card in step |
| `load_repertoire(white)` | `repertoire_ready(RepertoireData)`: the choices by key and the tree |
| `compute_coverage(white, tier, depth)` | `coverage_ready(CoverageData)`: core's `compute_coverage` to `depth` plies, the gaps' lines in SAN |
| `score_position(key, tier)` | `scores_ready(ScoreData)`: core's `score_moves` with each move's SAN |
| `import_pgn(path, white)` | `pgn_imported(PgnImportInfo)`, then `repertoire_changed`: `import_repertoire_pgn` and `import_choices` |
| `sync_cards(white)` | `cards_synced(white, created, reset, deleted)` |
| `due_cards(white)` | `cards_ready(CardsData)`: the colour's cards due now, each with its FEN, its answer in SAN and its name, and both colours' due and total counts |
| `record_review(card_id, white, key, grade)` | `review_recorded(card_id, due_at)`; the card must be the colour's card with the id at the key, as `cards_ready` listed it |
| `export_profiles(path)` | `profiles_exported(path)`, or `export_failed(message)` |
| `import_profiles(path)` | `profiles_imported(path, error)`, error empty on success (**Export and import**) |
| `close_stores()` | Closes the profile store (**Export and import**) |

A failure answers `profile_failed(message)`, which the current screen shows, or a message box
off Repertoire and Drill. **The tree** is the repertoire's lines from the start, walked depth
first: where the colour is to move, its choice; where the opponent is, each legal reply, by UCI,
that reaches a position with a choice. A position is walked once, by the first line that reaches
it, so a transposition shows under one line only, and a card is named to follow "Your move" by
that line ("after 1. e4 e5 2. Nf3"), "in the starting position", or by its FEN when the tree does
not reach it ("in the position …"). "Now" is the clock's seconds, given to the store for the due cards and each
review. A message says what happened and then one way forward, a profile store error in its own
sentences ("That file is not a chess-coach backup. Choose a file saved with File > Back up your
work."), and a read that throws names `profiles.db` and the restore.

## Repertoire

On Repertoire the board is at a line of the screen's own, from the start, which the move list
shows and *New game*, *Back* and the move list's rows go back along. `RepertoireScreen` holds:

- its description (`repertoire_description`), muted: "Choose the move you will play in each
  position. Coverage shows how much of what your opponents play you have an answer to.";
- the colour, *White repertoire* and *Black repertoire* (`repertoire_white`,
  `repertoire_black`), whose repertoire is shown and edited; the board turns to its side;
- *Add lines from a PGN file…* (`repertoire_import`), the file chooser (**Files**) and then
  `import_pgn`; the message line (`repertoire_message`) says what was read and what was taken, a
  sentence for each count that is not zero: "Read 1 game from the file. Added 1 new move to your
  repertoire. Left out 1 move where your repertoire already has a different one.", a move left
  out being a later move in the file for a position it has chosen, or a choice the repertoire
  already answers differently, which it keeps;
- the coverage (`repertoire_coverage`), "Coverage is 62.5% against players rated 1800–1999, over
  the first 20 half-moves.", at the tier selected and the depth in the settings, computed again
  after every change and at a change of tier;
- the tree (`repertoire_tree`, a `QTreeView`), the choices in bold, the opponent's replies plain;
  a click on a move takes the board to the position after it;
- the position (`repertoire_position`): "Your move here is Nf3.", "You have no move saved here
  yet…", or "Your opponent is to move here…", and *Remove this move* (`repertoire_remove`);
- the recommendation panel (`repertoire_scores`, when the colour is to move, under "Moves to
  choose from here, best first"): Move, Safe (core's sound), Gives away (the loss in pawns),
  Expected, Replies, Penalty, Score and Played (the share), as core's score gives them, the
  shares and scores as percentages, each column's header with a tooltip saying what it is; and
  *Save this move* (`repertoire_choose`), which makes the selected move the choice;
- the gaps (`repertoire_gaps`, "Positions with no answer yet, most common first"): How often,
  Moves and Position (FEN), and *Go to this position* (`repertoire_goto`), which takes the board
  to the selected gap.

A board move where the colour is to move makes that move the position's choice, replacing any;
a move where the opponent is to move only follows the line.

## Drill

`DrillScreen` holds its description (`drill_description`), muted: "Flashcards on your repertoire.
Each card shows a position. Play your move."; the colour, *White repertoire* and *Black
repertoire* (`drill_white`, `drill_black`); the colour's cards due, "1 due now, out of your 2
cards." (`drill_due`, or that there are none yet and how to make them); both repertoires'
progress, "Your White repertoire has 2 cards, 1 due now. Your Black repertoire has no cards."
(`drill_progress`); *Update cards from your repertoire* (`drill_sync`) and *Start the drill*
(`drill_start`); the status line (`drill_status`); the verdict (`drill_verdict`); *That was
hard*, *That was easy* and *Next card* (`drill_hard`, `drill_easy`, `drill_next`); and the
summary (`drill_summary`, `drill_summary_text`).

Entering the screen, or Update cards from your repertoire, syncs the colour's cards with its
repertoire ("Your cards now match your repertoire.", then the cards made, started again and
removed) and then asks for the cards due. A drill takes the first due card each time: the board
at the card's position, turned to its colour, and "Your move after 1. e4 e5. Play it on the
board." (the opponent's last move is not known, so the position is named by its line). The board
takes one move:

- **correct**: "Right. Nf3 is your move here.", graded good unless *That was hard* or *That was
  easy* is pressed first; the grade is recorded once, at the first of a press, *Next card*, or the
  end of two seconds, and the status bar gives the day the card comes back, "You will see this
  card again on 5 November 2026.";
- **wrong**: recorded as again at once; the verdict, "Not quite. Your move here is Nf3, not Nc3.
  The board shows it now.", names the card's move, and the board goes back to the card's position
  and plays it.

*Next card* asks for the cards due again, so a card answered is no longer among them; when none is
due the drill ends with "The drill is over. You answered 2 cards, 1 right and 1 wrong. The ones
you missed come back soon." Leaving the screen ends a drill the same way, and closing the window
records a grade still open.

## Export and import

*File > Back up your work…* (the profile's export) writes a copy of `profiles.db` through the
profile worker
(`ProfileStore::export_profiles`, `docs/database.md` §12.5), to a file the chooser's save mode gives
(**Files**). An export never replaces a file: the chooser refuses a name that is taken, and the
worker refuses one that appeared since with a message box (`profile_error`); the chooser offers
`chess-coach-backup.db`. *File > Restore a backup…* (the import) replaces `profiles.db` with the
chosen file. The other connections to it close first: the
engine worker's search stops, so that it answers at once, and the engine and spar workers close
their stores (`close_stores`, each a blocking queued call, a reset and no more). Then the profile
worker (`import_profiles`) closes its own and runs `ProfileStore::import_profiles` on its thread,
the copy and the store's busy timeout being no work for the GUI thread, which migrates the copy and
keeps the replaced file as `profiles.db.previous`; meanwhile a window-modal busy dialog
(`profile_import_progress`), which neither Escape nor a close dismisses, takes the window's input,
so that no move opens the engine cache and no End the game saves a session while the file is swapped,
and both of the File menu's imports are off until the worker answers `profiles_imported`. The engine
and spar workers then close their stores once more, in case one opened the old file meanwhile; each
opens the file again at its next use, and the current screen reads it again. An error goes to the
message box. A running spar session is kept and saved into the imported file when it ends.

## Settings

`Settings` reads and writes `<data-dir>/settings.ini` through `QSettings` in INI format, never the
registry or the platform's own settings store: `theme` (`light` or `dark`), `tier` (the last tier
selected), and the engine's `engine/binary` (empty for the default binary), `engine/threads` (half
the cores, at least one), `engine/hash_mb` (64), `engine/seconds` (2.0, 0.1 to 60) and
`engine/lines` (3, 1 to 10), and sparring's `spar/end_move` (25, 1 to 200: the move number that ends
a session), `spar/seconds` (0.5, 0.1 to 60: each off-book reply's search) and `spar/seed` (unset:
each session's seed is the clock's seconds; set, every session uses it), and Repertoire's
`repertoire/depth` (20, 2 to 40: the coverage depth in plies), and `files/last_dir`, the folder the
file chooser last chose in (**Files**), each at its default when unset or out of range. *Edit >
Settings…* (`SettingsDialog`, `settings_dialog`) edits, under plain names, the engine's (Engine
program, the binary with a Browse button, which opens the file chooser, and, while it is empty,
the default binary's path as its placeholder, "Leave empty to use /opt/homebrew/bin/stockfish", or
that none is found; Processor cores to use, the threads; Engine memory, the hash; Thinking time per
position, the seconds; Lines to show), Practice's move limit and seconds per reply (Practice games
end at move, `spar_end_move`; Thinking time per practice reply, `spar_seconds`) and the coverage
depth in half-moves (Check coverage up to, `repertoire_depth`); accepting saves them, sends them to the
engine worker and the spar worker, which restart their engines with them at the next search,
evaluates the position again and, on Repertoire, computes the coverage again. A new move limit
applies from the next session. Reading does not create anything; the first write, when the theme or
the tier changes, creates the data directory and the file. This is the first thing the app writes: a
run that changes no setting leaves the data directory as it found it, or absent.

## Files

The app chooses files with `FileChooser`, a dialog of its own (`file_chooser`), never `QFileDialog`:
Qt's dialog keeps its last folder through `QSettings(UserScope, "QtProject")`, which on macOS always
writes `~/Library/Preferences/com.qtproject.plist`, and the macOS panel keeps its own state in the
app's defaults, both outside the data directory. The chooser is a `QFileSystemModel` in a
`QTreeView` (`file_chooser_tree`) showing folders and the files its name filters match, a path line
(`file_chooser_path`; Return there goes to the folder or file typed, never accepting, and in save
mode a file that does not exist yet selects its folder and fills in its name; a leading `~` is the
home folder), in save mode a file name (`file_chooser_name`), a message line
(`file_chooser_message`), and *Open* or *Save* (`file_chooser_accept`) with *Cancel*. Open mode
takes an existing file, and a double click on one; save mode takes the folder the path line names,
or a file path typed there without Return, which gives the folder and, unless the name field was
edited after it, the name (the user's last edit wins), and refuses a name that holds a slash or is
taken, as an export never replaces a file. It starts in the folder last chosen in (`files/last_dir`
in `settings.ini`), else the home folder, and keeps no state of its own. `choose_file_to_open` and
`choose_file_to_save` run it for Browse… (every file), Add lines from a PGN file… (`*.pgn`), Back up
your work… and Restore a backup… (`*.db`). As a second guard the window first sends Qt's settings for anything else
that takes the default format to the data directory (`keep_qt_settings_in`:
`QSettings::setDefaultFormat(IniFormat)` and `setPath` for both scopes).

## Wording

The sentences on every screen write their numbers one way, through `wording.hpp`, in English
(`QLocale::English`) whatever the system's locale: a rating band as "under 1200", "1200–1399" to
"2000–2199" and "2200 and above" (`rating_band`, from core's `tier_lower_bounds`), which reads
after "players rated" and is what the Rating menu lists; a count with thousands separators,
"173,722" (`number`), and with its noun, "1 card" or "2 cards" (`counted`, there being no
translation files to give plurals); a loss in pawns with one decimal, "1.1" for 109 centipawns
(`pawns`); a day as "4 November 2026" (`day`); and the months an opening database holds as
"October 2026" or "October 2026 to March 2027" (`month_range`, from `meta`'s `YYYY-MM` list).

## Themes

The look is **Study** (`PLAN.md` §11, 2026-11-04; the visual spec in the lanes' `briefs/`): paper
by day and brown-black by night, Source Serif 4 for the screen names and headings over Source Sans
3 for everything else, 1 px hairlines, 2 px corners, a walnut board and a deep green accent. The
themes keep their names, *Light* and *Dark*, and their setting values, `light` and `dark`.

- **The colours** are `StudyPalette::light()` and `dark()` (`study_palette.hpp`), the spec's
  tokens. Each theme is one stylesheet, `app/resources/light.qss` and `dark.qss`, which carries
  the same values by hand (`app_resources_test` checks that each holds its palette's colours),
  compiled in under `:/themes/`. `apply_theme` sets the application's `QPalette` from the theme's
  `StudyPalette` (window, panel for bases and buttons, text, selection for highlights, the
  hairline for `Mid`, muted for placeholders and disabled text), then the stylesheet with
  `QApplication::setStyleSheet`, then the board's colours, `BoardTheme::light()` or
  `BoardTheme::dark()` (`BoardView::set_theme`: the walnut squares, the last move, the pick and the
  hints in the accent's green, the check in red, each coordinate in the other square's colour),
  the evaluation bar's frame (`EvalBar::set_palette`) and the toolbar's icons. *View > Theme*
  switches and saves the choice; the window applies the saved one at start. A label whose
  dynamic property `muted` is true, such as a screen's description, takes the muted colour
  (`QLabel[muted="true"]` in both stylesheets).
- **The style** is Fusion on every platform, set by the window at start: it draws what the
  stylesheet does not reach (radio buttons, spin boxes, whose arrows a stylesheet rule would hide,
  scroll bars, and a standalone tab bar's alignment, which the macOS style centres) in the theme's
  palette, where the macOS style draws them in the system's colours.
- **The fonts** are static TTFs from Adobe's releases under the SIL Open Font License 1.1
  (`NOTICE`, `legal/source-serif-4.txt`, `legal/source-sans-3.txt`): Source Serif 4 SemiBold 4.005,
  and Source Sans 3 Regular, SemiBold and Bold 3.052, compiled in under `:/fonts/`.
  `install_fonts`, which the window calls at start, registers them with `QFontDatabase` once and
  makes Source Sans 3 at 13 px the application font, so the app needs no font on the system
  (`app_resources_test` checks both families register). The stylesheets name Source Serif 4 for
  the screen tabs (15 px, 600) and the position's name (`explore_name`, 17 px, 600), and the small
  size for the status bar and the table headers, which is 12 px: Qt takes whole pixels in a
  stylesheet, where the spec says 11.5.
- **The icons** are our own, `app/resources/icons/<name>.svg` under `:/icons/`: 16 by 16, a
  1.6 px stroke with round caps and joins, no fills, drawn in `#000000` (`plus`, `back`, `flip`,
  `chevron-down`, `gear`, `sound-on`, `sound-off`, `explore`, `spar`, `repertoire`, `drill`).
  `themed_icon(name, palette)` replaces `#000000` with the palette's text colour, and its muted
  colour for the disabled pixmaps, and renders them with `QSvgRenderer` at 16 and 32 px, so one
  file serves both themes; `tinted_icon(name, normal, disabled)` takes the two colours directly.

## Help

*Help > How to use chess-coach* opens `HelpDialog` (`help_dialog`, non-blocking): a `QTextBrowser`
(`help_text`) of five short sections in rich text, a heading and two to four sentences each:
Explore, Practice, Repertoire, Drill, and "The database and the engine" (the games the database is
built from, what a rating band is, Book, Popular and Dubious each in a few words, and that nothing
leaves the computer). The text holds no link, and the browser opens none (`setOpenLinks(false)`).

The toolbar's buttons and the Rating menu each carry a one-sentence tooltip: *New game* "Go back
to the starting position.", *Back* "Take back the last move.", *Flip board* "Turn the board
around." and the tier selector "Choose the rating of the players whose games you see."
(`add_toolbar_tooltips`, which finds them by their object names and which the toolbar's setup
calls once).

## About

*Help > About chess-coach* opens `AboutDialog` (`about_dialog`, non-blocking): the app's version,
the open database's data version ("Opening database, data version 1"), a line on what the app is,
and the attributions of `NOTICE` in plain text without links:
Qt under the LGPL, chess-library under MIT, the cburnett pieces by Colin M.L. Burnett from
Wikimedia Commons under CC BY-SA 3.0, the Lichess data under CC0, the Source Serif 4 and Source
Sans 3 fonts by Adobe under the SIL Open Font License 1.1 with their versions, SQLite in the public
domain, and the other libraries with their licences. *About Qt* is Qt's own dialog.

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
`BoardView::animation_ms` (150 ms), a `QVariantAnimation` on the item's position with an out-cubic
curve; castling slides the rook too. A new move, a press on one of the user's own pieces,
`set_position` or `flip` stops any animation still running and puts every piece on its square.

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
  board and New game clearing it, Flip board, the four screens, Help > How to use chess-coach
  with its five sections and no link, the toolbar's tooltips, each screen's description, the
  status bar without a database. Screenshots: `main-window-start`, `main-window-after-moves`,
  `main-window-flipped`, `help-dialog`.
- `ui_explore_test`, on a fixture database the test writes into `core`'s schema with real keys
  (`app/tests/fixture_db.cpp`, the CLI fixture's rows, with the badges §7.3 gives them): opening it,
  with the data version, the start position off book at tier 2 and its moves with badges, bars,
  scores and top-tier shares at tier 3; switching tiers; moves on the board showing the replies and
  the deepest name; playing a row by double-click and by Return; going back by the move list, *Back*
  and *New game*; an off-book position; the evaluation bar for a centipawn score, a mate and no
  evaluation, its width kept; the Dubious 1. a3 at tier 4; a database that does not open;
  `settings.ini` appearing only after a theme change, alone in the data directory; both themes; the
  About dialog. Screenshots: `explore-start-tier3`, `explore-sicilian-light`, `explore-off-book`,
  `explore-light`, `explore-dark`, `about-dialog`.
- `ui_engine_test`, with core's fake engine (`fake-uci`) set as the binary through
  `app/tests/fake-stockfish.sh`, its script, delay and log chosen by environment variables: the
  lines after a request, white to move and black to move, and the evaluation bar following the
  first line where nothing is stored; a cache seeded through `ProfileStore` served without
  starting the engine; a 30 s search stopped by a move while a timer keeps firing, and the window
  closing at once; a missing binary, and one that fails the handshake; a checkmated side to move,
  white's and black's, read from the fake's mate 0; the Settings dialog saving and applying
  its values, and a new window starting with them. Screenshots: `explore-engine`,
  `explore-engine-dark`, `settings-dialog`.
- `ui_spar_test`, on the fixture database's tier-4 tree (the start position's e4, a Dubious a3
  and d4, black's e5 after 1. a3, and white's Nf3 alone after 1. a3 e5) with the fake engine's
  `legal` script as the coach off book, a fixed seed and a move limit of four: a session as white
  interrupted on 1. a3 with its better choices, Continue, interrupted again on 2. h4 off book and
  answered by the engine, ended at its move limit with the summary and the saved session listed,
  and replayed with the interruptions at plies one and three; a session as black, where the
  coach moves first and the board turns; End the game saving a stopped session, and closing the
  window saving one; a second session sending the coach's engine `ucinewgame`, read from the fake's
  log; the Settings dialog's spar entries ending the next session at the new limit;
  and Explore's line and tier untouched by a session, the tier selector disabled only on Spar.
  Screenshots: `spar-interruption`, `spar-summary`, `spar-replay`.
- `ui_repertoire_test`, on the same tier-4 tree: 1. e4 chosen by a board move and shown in the tree,
  then replaced by 1. a3, each time with the coverage equal to core's `compute_coverage` on the same
  stores; the gap 1. a3 e5 listed and Go to this position taking the board there; Save this move
  setting Nf3 there from the recommendation panel and Remove this move clearing it; a PGN import
  with its counts, a move left out among them; the black repertoire, the board turned and white's move only followed; and an export,
  refused a second time over the same file, imported back after the choice was removed. Screenshots:
  `repertoire-gaps`, `repertoire-tree`.
- `ui_lookup_test`, with a scratch folder as the bundle's resources
  (`CHESS_COACH_BUNDLE_RESOURCES`): the database `--db` names, a `--db` that names no file reported
  in the error box, the bundle's when the data directory has none, and the empty state naming the
  three places; File > Import database… through the file chooser, the data version going from 0 to
  7, the months in the message, `openings.db.previous` left behind and Explore reading the new file
  at the same position; a file that is not a database refused with the old one opened again; and the
  fake engine found as the bundle's `engine/stockfish` with no setting, no `CHESS_COACH_STOCKFISH`
  and no `stockfish` on the `PATH`, named in the Settings dialog's placeholder. Screenshots:
  `lookup-no-database`, `lookup-imported`.
- `ui_file_chooser_test`: open mode picking a file through the model, the name filter hiding the
  others and a folder refused; save mode refusing a taken name and taking a new one; Return in the
  path line going to a folder, a file and `~` without accepting; save mode reading a full path typed
  without Return, taking the name edited last, and refusing a name with a slash; Browse in the
  Settings dialog starting in `files/last_dir` and keeping the new folder; and the platform's
  settings folder gaining no entry, but the system's own `com.apple.` ones, while the tests run.
  Screenshot: `file-chooser-open`.
- `ui_drill_test`, on a white repertoire of two choices saved through `ProfileStore` with its
  cards then deleted (`drop_cards` in the fixture), so that entering the screen syncs them back:
  a drill with a correct move, graded good at Next card and due again a day later, a wrong move,
  graded again with the card's move played on the board, and the summary; and That was easy
  changing a correct move's grade. Screenshots: `drill-card`, `drill-summary`.

The window, Explore, Repertoire and Drill tests set `CHESS_COACH_STOCKFISH` to a path that does
not exist, so no real engine runs in them, and the window and Explore tests write nothing; the
engine and Spar tests name the fake engine in their settings.

`tools/ui_tests.sh [<regex>]` builds the `dev` preset, runs the UI tests (`ctest -L ui`, filtered
by name with the regex), and lists the screenshots of the run; the full gate runs the same tests
in the `gate` preset. A screenshot is how an agent sees the app: read the PNGs.

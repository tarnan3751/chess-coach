# Core

How `core` works: the position key, `Position` over chess-library, tiers, badges, opening names,
the read-only opening store with the canonical dump, the recommendation score, the UCI engine
client and the profile store with the engine cache. What these must do is fixed by
`docs/database.md` (the key §5, the schema §6, the badges §7, the names §10, the dump §11,
`profiles.db` §12, the score's inputs §13); this file says how `core` does it. The rules it obeys
are in `CLAUDE.md`, **`core/` is exact, deterministic and keyed by position**. The sparring
sampler, the spar session and the scheduler are described below; repertoire coverage is described
here when it lands.

| Header (`core/include/chesscoach/`) | Holds |
|---|---|
| `random.hpp` | `Splitmix64`, the generator of the Zobrist tables and of the sampler's draws |
| `key.hpp` | `Key`, the Zobrist tables, the bit cast SQLite stores |
| `position.hpp` | `Position`, `Color`, `FenError`, `MoveError`, `PlayedMove` |
| `tier.hpp` | `Tier`, `tier_of`, the bounds, the labels |
| `badges.hpp` | `Badge`, `Badges` |
| `names.hpp` | `load_names`, `NameTable`, `Opening`, `NameError` |
| `opening_schema.hpp` | `openings_schema_sql()`, `opening_schema_version` |
| `opening_store.hpp` | `OpeningStore`, the row structs, `Meta`, `StoreError` |
| `score.hpp` | `score_moves`, `ScoreComponents`, `ChildRows`, `evaluation_loss`, the score's numbers |
| `sampler.hpp` | `sampler_candidates`, `sample_reply`, `Candidate`, `SamplerConfig` |
| `spar.hpp` | `SparSession`, `SparConfig`, `Interruption`, `Summary`, `ReplyProvider`, `SparError` |
| `scheduler.hpp` | `CardState`, `Grade`, `Review`, `new_card`, `review`, `replay`, `is_due`, SM-2's numbers |
| `engine.hpp` | `Engine`, `EngineOptions`, `SearchRequest`, `SearchLine`, `SearchResult`, `EngineError` |
| `profile_store.hpp` | `ProfileStore`, `Profile`, `CachedSearch`, `Migration`, `Clock`, `ProfileError` |
| `version.hpp` | `Version` (Phase 0) |

Errors are values (`tl::expected` with an `enum class` or a small struct). No public header
includes chess-library, SQLite or reproc: `Position`, `OpeningStore`, `Engine` and `ProfileStore`
hold them behind a pointer.

## The key

`Key` is `std::uint64_t`. `zobrist::table` is the 781 values of §5.2, computed at compile time
(`consteval make_table()`, 781 outputs of `Splitmix64` from `0x434845535320434F`, `random.hpp`)
in the contract's draw order;
`piece_square(piece, square)`, `black_to_move()`, `castling(CastlingRight)` and
`en_passant_file(file)` index it. chess-library's `Piece` as an int (white P N B R Q K, then
black) and `Square` as an int (a1 = 0 to h8 = 63) are the contract's indices, so the board's own
values index the table directly. `key_to_sql` and `key_from_sql` are the `std::bit_cast` between
the key and the int64 the `key` and `child` columns store.

Two computations, one result:

- **From scratch** (`core/src/board_key.cpp`, private because it takes a `chess::Board`): the XOR
  of every piece on its square, black to move, each castling right the board holds, and the en
  passant file when the board keeps a target. `Position::start` and `Position::from_fen` key
  this way.
- **Incrementally** (`make_move` in `core/src/position.cpp`), before and after the library makes
  the move: toggle black to move; remove the old en passant file; XOR the moving piece out of
  its square and the placed piece (the promoted piece for a promotion) into its target, and the
  captured piece out (the pawn beside the target for en passant); for castling, which
  chess-library encodes as king takes rook, the king and the rook from their squares to the
  standard castling squares; after the move, every castling right held before and not after,
  and the new en passant file. A debug build asserts the result equals the from-scratch key on
  every move; the property tests check it from the printed FEN, where `NDEBUG` removes the
  assertion.

The pinned values (the table's first and last, the start position, after 1. e4, and the en
passant position after 1. e4 d5 2. e5 f5) are in `core/tests/key_test.cpp`, agreed by
independent implementations of §5.2.

## Position

`Position` wraps a `chess::Board` and the key. It is copyable (a copy allocates its own board);
a moved-from `Position` may only be assigned to or destroyed.

- `start()`, and `from_fen(text)`: four fields, or six with the counters, which must be
  decimal and are ignored.
- `key()`, `fen()` (the normalized four-field FEN, `getFen(false)`), `side_to_move()`,
  `in_check()`, `legal_moves_uci()` (sorted bytewise, so an index into it means the same move on
  every platform).
- `play_san(san)` and `play_uci(uci)` return a `PlayedMove`: the move's UCI (§5.3), its SAN as
  chess-library writes it before the move (check and mate suffixes included), and the child
  key. `play_san_fast(san)` is `play_san` without the SAN: its `san` is empty. `san_of(uci)` and
  `uci_of(san)` convert a legal move without playing it. A failed move leaves the position as it
  was.

**FEN.** `from_fen` checks before the library parses:

| Error | When |
|---|---|
| `malformed` | not 4 or 6 fields on single spaces; a placement that is not eight ranks of eight squares of `PNBRQKpnbrqk` and digits; a side other than `w` or `b`; castling other than `-` or distinct letters of `KQkq`; an en passant field other than `-` or a file and rank 3 or 6; counters that are not decimal digits |
| `kings` | not exactly one `K` and one `k` |
| `illegal` | a castling letter without its king on e1 or e8 and its rook on its corner; a pawn on the first or last rank; the side not to move in check |

It then **normalizes the en passant field** (§5.1): a target is kept only when its double push was
possible, which is that a pawn of the side not to move stands where the push would have put it,
with the target and the square it passed empty, and that the position before the push was legal:
with the pawn back on its origin square and the pushing side to move, the side now to move's king
is not attacked (chess-library answers, on a board of that position). chess-library then drops the
target too unless a capture onto it is legal for the side to move, pins and checks included. The
castling field's letters may come in any order; the library prints them `KQkq`.

**SAN.** Accepted: `O-O`, `O-O-O`, `0-0`, `0-0-0`; a piece move `[KQRBN][a-h]?[1-8]?x?[a-h][1-8]`;
a pawn move `[a-h][1-8]` or `[a-h]x[a-h][1-8]`, either with `=[QRBN]` on the last rank; then an
optional `+` or `#` and up to two of `!` and `?`, which are ignored (a wrong suffix is not an
error). Anything else is `malformed`. A well-formed move that matches no legal move is
`illegal`; one that matches several is `ambiguous`, which includes a pawn reaching the last rank
without a promotion piece (`e8`). Disambiguation is accepted when redundant (`Nb1c3`).

**UCI.** `[a-h][1-8][a-h][1-8][qrbn]?`, else `malformed`; the text must equal the library's
spelling of a legal move, else `illegal`. So castling is only the king's two-square move
(`e1h1` is illegal) and a promotion needs its letter.

**Cost.** Every `Position` allocates its board, and the board keeps a history entry per move
played. `play_san` writes the move's SAN with chess-library's writer, which generates the
mover's legal moves for disambiguation and copies the board for a checking move; the pipeline's
loop over every move of every game uses `play_san_fast`, which skips it. Parsing SAN is one
legal-move check when one piece can reach the target, else a generation of that piece type's
moves. If the pipeline's benchmark finds `Position` the bottleneck, the first candidate is a
play path that does not build the UCI string either.

## chess-library and the wrapper

chess-library (0.9.10, `vcpkg/ports/chess-library`) is the only rules implementation. It is one
header that warns under our flags, so `core/CMakeLists.txt` finds it with
`find_path(... chess-library/chess.hpp)` and adds it as a `SYSTEM PRIVATE` include of `core`
and of the one test that uses it as an oracle (`core_key_property_test`); only `core/src`
includes it. What the wrapper neutralizes:

| The library | The wrapper |
|---|---|
| `Board(fen)` ignores a parse failure; `setFen` returns `false` but accepts ranks of the wrong width, and asserts without a king | `from_fen` checks every field and the kings first, then calls `setFen` |
| `setFen` keeps an en passant target whenever a capture would be legal, without checking that the pawn that double pushed is there (a target without one gives a capture of an empty square) or that the push was possible (the position before it may have had the side now to move in check: a double check, or a line the captured pawn's removal opens) | `from_fen` drops a target whose double push did not happen or could not have: the pawn must be there, and the position with it back on its origin square must not have the side to move's king attacked |
| A castling letter with no rook on its corner grants the right with any rook on that side | `from_fen` refuses it as `illegal` |
| `makeMove<false>` (the default) keeps an en passant target whenever an enemy pawn attacks it | every move is `makeMove<true>`, which keeps it only when the capture is legal: §5.1's rule |
| `parseSan` throws `SanParseError` or `AmbiguousMoveError`, returns `NO_MOVE` (which is `a1a1`, value 0) for an empty string, and reads past the end of some malformed strings (`O`, `e8=`) | the SAN syntax check runs first; the exceptions become `illegal` and `ambiguous`; `NO_MOVE` is compared explicitly |
| `uciToMove` builds a move without checking legality | UCI is matched against the legal move list's spelling |
| Castling is king takes rook internally | `moveToUci` writes `e1g1`; the incremental key uses the standard castling squares |

The static analyzer, following our calls into the header, reports two kinds of false positive
inside it (enum casts and a pawn-attack index on paths where a colour is `NONE`). A `NOLINT`
cannot sit in a header we do not own, so `core/.clang-tidy` turns off
`clang-analyzer-optin.core.EnumCastOutOfRange` and `clang-analyzer-security.ArrayBound` for
core's sources and inherits everything else.

The library's own `hash()` also ignores the counters and keeps en passant under the same rule, so
`core_key_property_test` uses it as an oracle: over batches of random games, two positions have
equal keys exactly when the library's hashes are equal.

## Tiers and badges

`Tier` is an `enum class : std::uint8_t` whose values are the stored indices: `under_1200` (0),
`r1200_1399`, `r1400_1599`, `r1600_1799`, `r1800_1999`, `r2000_2199`, `r2200_up` (6), which is
`top_tier`. `tier_lower_bounds` holds 1200 to 2200, the value of meta's `tiers` key;
`tier_of(white_elo, black_elo)` floors the average, summed in 64 bits; `tier_index` and
`tier_from_index` convert to and from the column; `tier_label` gives `<1200`, `1200-1399`, ...,
`2200+`.

`Badges` decodes the `moves.badges` column: `has(Badge)`, `set(Badge)`, `raw()`, with
`Badge::book` = 1, `popular` = 2, `dubious` = 4. A bit outside the three is kept, so `raw()`
gives the stored value back. The thresholds are the pipeline's (§7); nothing in `core` computes a
badge.

## Names

`load_names(dir)` reads `a.tsv` to `e.tsv` (`opening_tsv_files`, the value of meta's
`openings_tsv`) from `dir`, by §10's rule: each file's header must be `eco`, `name`, `pgn`
separated by tabs; each line's moves, with the move-number tokens skipped, replay from the start
position through `play_san_fast`; the final key gets the line's ECO code and name unless an
earlier line named it. `NameTable` gives `find(key)` (a pointer to the `Opening`, or null) and
the counts the pipeline reports: `lines()` = `size()` + `collisions()` + `failures()`, a failure
being a line that is not three tab-separated fields or whose moves do not replay. A missing file,
an unreadable one and a wrong header are `NameError`s naming the file. The five files are
committed under `data/chess-openings/` with their source and licence (`README.md`,
`COPYING.txt`).

## The opening store

`openings_schema_sql()` is the DDL of §6 (`meta`, then `positions`, `nodes` and `moves` `WITHOUT
ROWID`), the one definition the pipeline's writer executes; it creates tables only.

`OpeningStore::open(path)` opens the file `SQLITE_OPEN_READONLY`, sets `mmap_size` to 1 GiB,
reads `meta` and prepares the three lookups once:

| Error | When |
|---|---|
| `missing_file` | no regular file at the path |
| `unreadable` | SQLite cannot read it (not a database, corrupt, the `meta` table's rows included) |
| `no_meta` | no `meta` table, one without `key` and `value` columns, or no `schema_version` row |
| `wrong_version` | `schema_version` is not exactly `opening_schema_version` (`1`) |
| `schema_mismatch` | a lookup does not prepare: `positions`, `nodes` or `moves` missing or misshapen |

`meta()` is the table, read once, as a bytewise-ordered map with `find`. `position(key)`,
`node(key, tier)` and `moves(key, tier)` return the rows as plain structs mirroring the columns
(`std::optional` for a nullable one; `MoveRow::total()`), `moves` ordered by move bytewise, which
is the primary key's order. Each lookup is one indexed query with the key bound as its int64.
`counts()` gives the whole file's `StoreCounts` for the CLI's `stats`, by SQL rather than a scan:
the `nodes` and `moves` rows per tier (`COUNT(*)` grouped by tier), the start position's `games`
per tier (its `nodes` rows, by its key), and the `positions` rows, those with a name and those
with an evaluation; a tier with no row counts zero. A
store is used from one thread at a time, and a query that fails after a successful open (an I/O
error, a file corrupted on disk) throws `std::runtime_error` (`SQLite::Exception`).

`dump(out)` writes §11's canonical dump. `meta` comes from the map, without `built_at` and
`pipeline_version`. Each table is read in two primary-key ranges, the keys whose int64 is
non-negative and then the negative ones: the order of the keys as unsigned, with nothing sorted
in memory. Every column is written as stored: NULL as `-`, an integer in decimal (a key column
as the unsigned value of its bits), text as it is.

## The recommendation score

`score_moves` ranks the moves of a position at one tier (`PLAN.md` §5.2; the inputs are
`docs/database.md` §13). Every move with a `moves` row at the tier is a candidate, and each
`ScoreComponents` exposes how its score came about:

| Component | Rule |
|---|---|
| `loss` | §7.2's loss from the position's and the child's evaluations, from the mover's side, a mate m as sign(m) × (100000 − \|m\|); undefined when either is unevaluated |
| `sound` | the loss is undefined or at most `soundness_margin_cp`, 50 cp, the Book margin |
| `expected_score` | the mover's wins plus half the draws over the total, shrunk toward 0.5 by total / (total + 30): (wins + draws / 2 + 15) / (total + 30), so a move of 30 games weighs half |
| `replies_needed` | the fewest of the child's most-played replies at the tier whose totals reach 80% of its moves' sum; 0 when the child is off book at the tier (`child_in_book` false) and 0 when it is in book there with no moves (visited only at the cap, or where its games ended), so neither carries a penalty |
| `penalty` | 0.02 per reply beyond the first, capped at 0.3 |
| `score` | max(0, `expected_score` − `penalty`) for a sound move, 0 for an unsound one, which is still listed, with its loss |
| `share` | the move's share of the position's plays at the tier; shown beside the score, not part of it |

The moves come back by score, highest first, equal scores by move text. The numbers (50 cp, 30
games, 80%, 0.02, 0.3) are constants of `score.hpp` and tier A to change.

The function is pure over the store's rows: `score_moves(position_row, moves, lookup)` takes the
position's `positions` row (its evaluation, and the side to move from its FEN's second field) and
`moves` rows at the tier, and a `ChildLookup` that gives a child's `positions` row, `nodes` row
and `moves` rows at the same tier; the overload on an `OpeningStore` reads those rows and returns
nothing for a position without a `positions` row. The arithmetic is integer where the formula
allows (the expected score is one division of (2 × wins + draws + 30) by (2 × total + 60), the
coverage test is `100 × covered >= 80 × sum`), and every floating-point value is one operation in
a statement of its own, so no compiler fuses two into a multiply-add and the values and the order
are the same on every platform.

`evaluation_loss` repeats the rule the pipeline applies for the badges (`pipeline/src/badges.cpp`)
with the same 50 cp margin; moving the pipeline onto `core`'s function is a later refactor.

## The engine

`Engine` is one UCI engine, Stockfish in practice, as a child process over reproc++ (stdin and
stdout piped, stderr discarded). Its calls are synchronous; the app's engine worker runs them on
its own thread. `stop()` may come from any other thread.

| Call | Sent | Awaited, within |
|---|---|---|
| `start(binary, options)` | `uci`; then `setoption name Threads`, `setoption name Hash`; `isready` | every line up to `uciok`, then `readyok`, each within `reply_timeout` |
| `new_game()` | `ucinewgame`, `isready` | `readyok`, within `reply_timeout` |
| `search(position, request)` | `setoption name MultiPV` when the line count, clamped to the engine's maximum, changed; `setoption name UCI_LimitStrength value true` or `false` when that changed, and `UCI_Elo` when its value changed; `isready`; `position fen <normalized FEN> 0 1`; `go movetime <ms>` | `readyok` within `reply_timeout`; then info lines up to `bestmove`, within the movetime plus `reply_timeout` |
| `stop()` during a search | `stop`, written by the searching thread | `bestmove` within `reply_timeout` of the stop, however many lines keep coming |
| destruction | `quit` | the process's exit; then terminate, then kill, a second apart |

- **The handshake** records the `id name`, `UCI_Elo`'s `min` and `max` and `MultiPV`'s `max`
  when the engine advertises them; every other line before `uciok` is ignored. `uciok`,
  `readyok` and `bestmove` are recognised by a line's first token, spaces and a carriage return
  aside. A limited search clamps its Elo to that range (Stockfish's floor is 1320) and reports
  the value applied; an engine without `UCI_Elo` answers a limited request with `unsupported`.
  A request for more lines than the engine's `MultiPV` maximum (Stockfish's is 256) is clamped
  to it, and the result has at most that many lines.
- **A request** asks for at least one line and for more than 0 and at most
  `maximum_search_seconds` (3600) seconds, finite; anything else is `invalid_request`, and
  nothing is sent.
- **Info lines.** A line counts when it has a `depth` and one exact `score cp` or `score mate`
  (and usually a `pv`); a missing `multipv` is 1. `info string`, `currmove` and `nps` lines,
  lines with a `lowerbound` or `upperbound` score or with two scores, unreadable numbers and
  multipvs above the request are ignored. The result keeps the last counted line per multipv,
  ordered by multipv; `best_move` is what `bestmove` names (empty for `(none)`), which need not
  be the first line's first move when the engine's last word on it was a bound.
- **One call at a time.** A mutex marks a call as running: a second `search` or `new_game` from
  another thread returns `busy` without touching the process; `stop()` sets a flag under the
  same mutex only while a search runs, so it reaches the running search or nothing. The
  searching thread polls stdout in 25 ms slices and checks the flag and the deadline between
  slices, so all I/O stays on one thread and there is never a second `go` in flight. The search
  has one deadline, which a stop shortens for every line after it.
- **Failure.** A missed bound is `timeout`, a closed pipe or failed write is `died`; either kills
  the process, `is_alive()` turns false for good, and every later call returns `died`; the
  caller starts a new engine. `start` ignores SIGPIPE process-wide, once, on POSIX, because
  writing to a dead engine's stdin would otherwise end this process.

**The fake engine.** `core/tests/fake_uci.cpp` builds `fake-uci`, an executable `core_engine_test`
drives: `fake-uci <script> [delay_ms] [log_path]`, logging every line it receives so the test
checks exactly what the client sent and in which order. Its scripts: `normal` (the protocol as
Stockfish speaks it, a `go` answered after its movetime or on `stop`), `late-readyok` (every
`readyok` after the first `delay_ms` late), `interleaved-info` (junk, `info string`, multipv 2
before 1, repeated depths, a bound score, a line with two scores, a multipv above the request
and a mate score), `no-bestmove`, `chatty-after-stop` (info every 10 ms after a stop and never
`bestmove`), `multipv-max-2` (a `MultiPV` maximum of 2), `legal` (every `go` answered at once
with the first legal move of the last position sent, through `core`'s `Position`, as one depth-1
info line and `bestmove`, or `bestmove (none)` without a legal move, for tests that play the
engine's replies), `die-mid-search` and `garbage` (non-UCI lines before `uciok`, no options,
`uciok` and `readyok` padded with spaces and a carriage return). The tests set `reply_timeout` to
fractions of a second, so the timeouts are tested in well under one.
`core_engine_stockfish_test` (label `engine`) drives the real Stockfish, found through
`CHESS_COACH_STOCKFISH` or the `PATH`, with one thread and a 16 MB hash, and skips with a message
when there is none.

## The profile store

`ProfileStore::open(data_dir, clock, migrations)` opens `profiles.db` in the data directory,
creating the directory when it is missing, read-write with a 2 s busy timeout and foreign keys
on. The clock (Unix seconds, UTC; `system_clock_seconds` by default) is injected, and the
connection registers `chesscoach_now()`, an SQL function answered from it, so a migration script
can timestamp rows and stay plain SQL.

**Versions** (§12.1). `profile_migrations()` lists one script per version; version 1 creates
`meta`, `profiles` and `engine_cache` and the profile `Default` at tier 2. An absent or
zero-length file is created by running every script; an existing file must have a `meta` table
with a positive decimal `schema_version`, else it is refused as `not_profiles` without being
modified; a version above the last script's is `newer_version`, also unmodified; an older one
runs each later script in its own transaction, which records `schema_version` as its last
statement, so a failing script (`migration_failed`) leaves the file at the last version that
succeeded. Tests pass longer migration lists to exercise all of this. Each version's file is
kept as a fixture (`core/tests/fixtures/profiles-v1.db`), written by the hidden test case
`[.fixture]` and opened unchanged by every later build.

**Profiles.** `profiles()` by id; `create_profile`, `rename_profile`, `set_tier` and
`remove_profile` return the profile or an error: `invalid_name` (empty), `duplicate_name`,
`no_such_profile`, `last_profile` (the only profile cannot be removed), `storage` (the write
failed).

**The engine cache** (§12.2), one row per line, keyed by `(key, lines, line)`:

- `cache_lookup(key, lines, min_depth)` hits when a row exists for `(key, lines)` and the least
  depth among them is at least `min_depth`. Callers do not look up for a limited search.
- `cache_store(key, lines, result)` writes nothing for a limited result, a stopped one or one
  without lines, and returns it as it is. Otherwise, in one transaction, the result's lines
  replace the stored ones when nothing is stored or their least depth is at least the stored
  least depth, and the stored lines are kept and returned when it is not. A result with fewer
  lines than asked for (a position with fewer legal moves) is complete and stored as such.
- A pv is stored as UCI moves separated by spaces; scores stay from the side to move.

## The sampler

`sampler_candidates(moves, config)` turns a position's `moves` rows at a tier into the
candidates of `docs/database.md` §13: the rows with at least `config.min_games` plays (10 by
default), ordered by move text bytewise, each weighted by its total, temperature 1.0 being the
only one the contract defines. `sample_reply(candidates, generator)` draws one: a single raw
output of the `Splitmix64` (`random.hpp`, §5.2's step, here seeded with the session's seed),
modulo the sum of the weights, selects the first candidate whose cumulative weight exceeds it.
With no candidate it returns nothing and uses no output. No standard-library distribution is
involved, so a seed draws the same replies on every platform.

## The spar session

`SparSession` is one sparring game from the start position against the coach at a tier
(`PLAN.md` §5.4), over an `OpeningStore` the caller keeps alive. Its state machine:

| Phase | The coach replies with | Ends |
|---|---|---|
| `book` | `sample_reply` over the position's moves at the tier | when the position reached has no `nodes` row at the tier, or the book position has no candidate |
| `off_book` | the `ReplyProvider` (Stockfish at limited strength in the app, Phase 4b) | never: the book is not re-entered |
| `ended` | nothing | — |

A session starts in `book` when the start position has a `nodes` row at the tier, else in
`off_book`. The user moves with `play_user_move(uci)` and the coach with `coach_reply(provider)`;
calling either out of turn is a programming error (`std::logic_error`), a move that is not legal
is a `MoveError`, and a provider's failure or illegal move is a `SparError` that changes nothing.

**Interruptions.** While in book, a user move that carries the Dubious badge, or whose child
has no `nodes` row at the tier (§7.3, even when the move itself was never played there and a
transposition does not save it), is answered with one `Interruption`: the reason (`dubious`
takes precedence when both apply), the move, its badges (none without a row), its loss
(`score.hpp`'s `evaluation_loss`, when both evaluations exist), and the better choices: the
position's other moves at the tier that are Book, or Popular and not Dubious, by total
descending, then by move text, each with its SAN and its row. The move stands either way; an
off-book child moves the session to `off_book`, and no move is interrupted after that. The
coach's own move may leave the book too, without an interruption.

**The end.** The session counts its own moves (the normalized position drops the counters):
`move_number()` is 1 at the start and one more after each black move. After every move it ends
when the side to move has no legal move, with the result of the mate (to the side that moved)
or `1/2-1/2` for stalemate, or when the user is to move and `move_number()` has reached
`SparConfig::end_move` (25 by default), with no result. `summary()` gives the moves in UCI from
the start, the interruptions, the furthest phase reached (`book` or `off_book`), the result and
the seed.

**Determinism and replay.** The session's `Splitmix64`, seeded with its seed, is used only for
the coach's book replies, one output each. `SparSession::replay(store, tier, colour, seed,
moves, config)` plays a recorded session again: the user's moves and the coach's off-book moves
as recorded (an engine's are not reproducible), the coach's book replies drawn again, each of
which must equal the record (`replay_mismatch` otherwise, and for a record that runs past the
end; `replay_illegal_move` for a recorded move that does not play). The summary it returns,
interruptions and phases included, equals the original's.

## The scheduler

`scheduler.hpp` is SM-2 (`PLAN.md` §5.2, §11) as `constexpr` functions over a card's integer
state, so a history replays to exactly the same state on every platform; FSRS may later replace
the rules behind the same functions. No clock: every time is given, in Unix seconds UTC.

- `CardState`: `repetitions` (passes since the last failure), `interval_days` (0 before the first
  review), `ease_milli` (the E-Factor in thousandths) and `due_at`.
- `Grade` is SM-2's quality, stored as its value: `again` 1 (a failure), `hard` 3, `good` 4,
  `easy` 5.
- `new_card(created_at)` starts at ease 2500 and is due at once, at `created_at`, so never in the
  past.
- `review(card, grade, reviewed_at)`:
  - the interval: on `again`, 1 day and no repetitions; on a pass, 1 day after the first pass, 6
    after the second, then `(interval_days * ease_milli + 500) / 1000` (round(interval × ease)
    half up, in 64-bit integers) with the ease from before this review (SuperMemo's order), at
    most `maximum_interval_days`, and one repetition more;
  - the ease, for every grade q: `ease_milli += 100 - (5 - q) * (80 + (5 - q) * 20)`, so `easy`
    +100, `good` 0, `hard` −140 and `again` −540, at least `minimum_ease_milli`;
  - `due_at = reviewed_at + interval_days * 86400`, so never before the review.
- `replay(created_at, history)` is `new_card` followed by `review` of each entry; `is_due(card,
  now)` is `now >= due_at`.

The numbers are tier A: the starting ease 2500, the floor 1300, the 100, 80 and 20 of the ease
rule, the first intervals 1 and 6, the rounding, 86400 seconds a day, and the cap of 36500 days
(100 years), which keeps the arithmetic bounded however many `easy` reviews a card gets. Good,
good, good, again, good gives intervals of 1, 6, 15, 1 and 1 days with eases 2500, 2500, 2500,
1960 and 1960.

## Tests

`core/tests/`: `key_test` (the pinned values, en passant by hand, transpositions, castling
rights, counters) and `key_property_test` (incremental equals from-scratch; the library-hash
oracle); `position_test` (FEN acceptance, normalization and every error; SAN and UCI in both
directions) and `position_property_test` (FEN and SAN/UCI round trips); `tier_test`,
`badges_test`; `names_test` (the committed files, first wins, the errors); `opening_store_test`
(a small database built from `openings_schema_sql()` with keys either side of the sign bit, the
lookups, the dump against a golden text written from §11, a read-only file left unchanged, and
every refusal); `score_test` (the shrink from either side, the 80% count at its boundary, the
gate with defined, undefined and mate losses, the penalty and its cap, off-book and move-less
children, the floor at zero, the shares and the sort, each number worked out by hand) and
`score_property_test` (over generated rows: every score between 0 and 1, an unsound move at 0, a
sound score equal to max(0, expected − penalty), the penalty within its cap, the shares summing
to one, the order); `engine_test` (every fake script, `busy`, `stop`, the options sent, the
timeouts, a missing binary) and `engine_stockfish_test`; `profile_store_test` (creation, every
error, migrations, the refused files, the cache rules and the version 1 fixture); `random_test`
(`Splitmix64` gives the Zobrist table and the reference outputs); `sampler_test` (min_games, the
order, no draw without a candidate, the draw by cumulative weight, one sequence per seed, and
the frequencies over 200,000 draws within one point of the weights) and `sampler_property_test`
(a draw is a candidate and ignores the moves below min_games); `spar_test` (on a small tree
built from `openings_schema_sql()`, `spar_fixture.hpp`: a scripted session with a Dubious and an
off-book interruption, the sampler's book replies, the provider off book, the end, the summary
and its replay; a session off book from the start, a book position without candidates, a mate's
result, the provider's failures, the replay's refusals, the calls out of turn) and
`spar_property_test` (sessions from generated seeds, colours, end moves and user moves replay to
the same summary); `scheduler_test` (a hand-computed sequence, also checked at compile time,
`hard` and `easy`, the ease floor, the interval cap, when a card is due) and
`scheduler_property_test` (over random histories: intervals grow on passes and reset on a
failure, due never before creation or review, the bounds, replay equals the fold). The
property tests' random games (`random_games.hpp`) start from the start position or from
positions that soon castle, promote or meet en passant, and pick moves by index into the sorted
legal moves.

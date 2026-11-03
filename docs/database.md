# The data contract

This document binds the pipeline that writes `openings.db`, `core`'s stores that read it and keep
`profiles.db`, the CLI's `dump`, and the app, which reads both databases only through `core`. Each
rule is stated once, in the section that owns it; other sections refer to it by number. Every
statement here is tier A to change (§14).

## 1. Scope

Covered: the sources (§2), which games count (§3), the tiers (§4), the position key (§5), the
schema and invariants of `openings.db` (§6), the badges and the off-book rule (§7), the knobs
(§8), the evaluation merge (§9), opening names (§10), the canonical dump (§11), `profiles.db` and
its versioning (§12), and the numbers the sampler and the recommendation score read (§13).

Not covered: how the pipeline meets the contract (threads, buffers, the sketch's dimensions) is
`docs/reference/pipeline.md`; the score's formula, the scheduler and the UCI client are
`docs/reference/core.md`; what the screens show is `docs/reference/app.md`.

## 2. Sources and licences

| Source | File | Licence | Shape relied on |
|---|---|---|---|
| Lichess standard rated games | `lichess_db_standard_rated_YYYY-MM.pgn.zst` from database.lichess.org, one month per file, not cumulative | CC0 ("Database exports are released under the Creative Commons CC0 license") | §3 |
| Lichess evaluation database | `lichess_db_eval.jsonl.zst` from database.lichess.org | CC0 | §9 |
| Opening names | `a.tsv` to `e.tsv` from `lichess-org/chess-openings` | CC0-1.0 | §10 |
| Piece set | cburnett, from lila's public piece directory | CC BY-SA 3.0 | Not data; attributed in the About dialog and `NOTICE` |

The pipeline reads these files where the owner put them and never downloads anything.

## 3. Games admitted

A game is admitted when it passes every check below. The header checks run before any move is
parsed. A game that fails a check is skipped, never aborts the run, and increments the counter of
the first check it fails, so the counters partition the skipped games.

| Order | Counter | The game is skipped when |
|---|---|---|
| 1 | `malformed` | its header section cannot be read as PGN |
| 2 | `unrated` | `Event` is absent or does not begin with `Rated ` (casual games) |
| 3 | `speed` | the word after `Rated ` in `Event` is not exactly `Blitz`, `Rapid` or `Classical` |
| 4 | `variant` | `Variant` is present and is not `Standard`, or a `SetUp` or `FEN` header is present |
| 5 | `rating` | `WhiteElo` or `BlackElo` is absent or is not a decimal integer of at most nine digits (Lichess writes `?`) |
| 6 | `result` | `Result` is not `1-0`, `0-1` or `1/2-1/2` (an unfinished game is `*`) |
| 7 | `moves` | any of the game's first `max_ply` moves (§8) cannot be read as legal SAN in its position |

Check 3 admits tournament games (`Rated Blitz tournament https://lichess.org/tournament/...`) and
excludes `Bullet`, `UltraBullet` and `Correspondence`. The speed is the one Lichess wrote into
`Event` when the game was played; `TimeControl` is not read. A game skipped for `moves`
contributes nothing, not even the positions before the bad move; moves past the cap are never
parsed, so a bad move there does not skip the game. A SAN token may carry, after an optional `+`
or `#`, one of the annotation glyphs `!`, `?`, `!?`, `?!`, `!!` and `??`, which the scanner strips
before parsing. Both passes apply the same checks and admit the same games. The result of an
admitted game is its `Result` header.

## 4. Tiers

A game's tier comes from the average of the two ratings, `r = floor((WhiteElo + BlackElo) / 2)`:

| Tier | Average rating |
|---|---|
| 0 | r < 1200 |
| 1 | 1200 ≤ r ≤ 1399 |
| 2 | 1400 ≤ r ≤ 1599 |
| 3 | 1600 ≤ r ≤ 1799 |
| 4 | 1800 ≤ r ≤ 1999 |
| 5 | 2000 ≤ r ≤ 2199 |
| 6 | r ≥ 2200 |

Columns named `tier` hold the index. Tier 6 is the top tier, the one the Book badge reads (§7.3).
Blitz, rapid and classical games are merged within a tier. Every count is per tier: there is no
all-tiers row, and no lookup falls back to another tier.

## 5. Normalization and the key

### 5.1 The normalized position

A position is its piece placement, side to move, castling rights and en passant target. The en
passant target is kept only when the side to move has a legal en passant capture onto it in a
position whose double push was legal: the pushed pawn stands just past the target, the target and
the pawn's origin square are empty, the position with the pawn back on its origin square does not
have the side to move's king in check, and the capture is not pinned and does not leave the
mover's king in check, as chess-library's legal move generation decides once the push is known to
have been legal; otherwise it is absent. The halfmove clock and the fullmove number are not
part of the position. Castling rights are those chess-library holds.

The **normalized FEN** is the first four FEN fields of the normalized position, separated by
single spaces, as chess-library prints them except that the en passant field follows the rule
above: the start position is `rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -`, and the
position after 1. e4 is `rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -`, because no black
pawn can capture on e3. The parser accepts four fields or six, ignores the counters and normalizes
the en passant field, so printing a parsed FEN gives its normalized form and a normalized FEN
parses and prints to itself.

### 5.2 The key

The key is a 64-bit Zobrist hash of the normalized position: the XOR of the piece-square value of
every piece on its square, the black-to-move value when black is to move, the castling value of
each right held, and the en passant value of the target's file when the target is kept. Two
positions are the same node exactly when their keys are equal; the pipeline stores one FEN per
key. A collision between distinct positions is not detected, being negligible at 64 bits. Only
`core`'s tables compute the key, and its incremental update after a move equals the from-scratch
hash of the resulting position.

The tables are 781 consecutive outputs of splitmix64, all arithmetic modulo 2^64:

```
state = 0x434845535320434F                      ("CHESS CO" in ASCII)
next():  state = state + 0x9E3779B97F4A7C15
         z = state
         z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
         z = (z ^ (z >> 27)) * 0x94D049BB133111EB
         return z ^ (z >> 31)
```

drawn in this order:

| Outputs | Values |
|---|---|
| 0 to 767 | piece-square, output `piece * 64 + square`: `piece` 0 to 11 is white P, N, B, R, Q, K then black P, N, B, R, Q, K; `square` is `rank * 8 + file`, a1 = 0, b1 = 1, ..., h1 = 7, a2 = 8, ..., h8 = 63 |
| 768 | black to move |
| 769 to 772 | castling rights K, Q, k, q (white king side, white queen side, black king side, black queen side) |
| 773 to 780 | en passant file a to h |

The key of the start position is `0x8b26ecc3ca52d0bb` (unsigned decimal 10026961946044190907,
stored as the int64 -8419782127665360709), and the key after 1. e4, which has no en passant
value, is `0xa355c969b7c91f2c`; the first value of the table is `0xf777eaa02d011cb2`. A unit test
pins all three.

A key is stored in SQLite as the signed 64-bit integer with the same bits (a bit cast), compared
as an integer and never as text, and shown in the CLI and the dump (§11) as unsigned decimal.
`ORDER BY key` in SQL sorts by the signed value, which is not the dump's order.

### 5.3 Moves

A move is written in UCI: the from square, the to square, and a lowercase promotion letter
(`e7e8q`); castling is the king's two-square move (`e1g1`, `e1c1`, `e8g8`, `e8c8`). Every move
column in both databases uses this form. The pipeline reads moves from PGN SAN, and the names from
TSV SAN, through chess-library.

### 5.4 Plies and transpositions

Ply 0 is the start position; ply n is the position after the first n half-moves of a game. Move
orders that reach the same normalized position reach the same key: they share one `positions` row
and, per tier, one `nodes` row whose counts include every move order. A position's ply is not
stored, because it differs between move orders.

## 6. `openings.db`

The pipeline writes the file once; nothing modifies it afterwards. The store opens it with
`SQLITE_OPEN_READONLY` and refuses a file with no `meta` or a `schema_version` other than the one
below. Keys are §5.2's, moves §5.3's, tiers §4's.

### 6.1 `meta`

`meta(key TEXT PRIMARY KEY, value TEXT NOT NULL)`, one row per key:

| Key | Value |
|---|---|
| `schema_version` | `1` |
| `data_version` | A decimal integer: `0` for every development build, the sample corpus included; a published database carries a positive number, incremented per publication and shown in the About dialog |
| `months` | The input months as `YYYY-MM`, ascending, comma-separated, read from the input file names (`lichess_db_standard_rated_YYYY-MM.pgn.zst`); the pipeline refuses an input not named so and refuses two inputs of the same month, so the sample corpus and any hand-made corpus are named as a month file |
| `min_games` | The knob (§8), decimal |
| `max_ply` | The knob (§8), decimal |
| `tiers` | The lower bounds of tiers 1 to 6 (§4), comma-separated |
| `built_at` | The build time in UTC, `YYYY-MM-DDTHH:MM:SSZ` |
| `pipeline_version` | The version of `chess-coach-pipeline` that wrote the file |
| `eval_db` | The base name of the evaluation file merged (§9), or `none` |
| `openings_tsv` | The base names of the TSV files read (§10), comma-separated in reading order, or `none` |
| `games_admitted` | The number of admitted games over every month and tier, decimal; the sum of the `admitted_tier_` keys |
| `admitted_tier_0` to `admitted_tier_6` | The number of admitted games of that tier (§4), decimal, one key per tier |
| `games_skipped` | The number of skipped games, decimal; the sum of the `skipped_` keys |
| `skipped_malformed`, `skipped_unrated`, `skipped_speed`, `skipped_variant`, `skipped_rating`, `skipped_result`, `skipped_moves` | §3's counter of the same name, decimal, one key each |

### 6.2 `positions`

`positions(...) WITHOUT ROWID`, one row for every key that has at least one `nodes` row and for no
other key:

| Column | Type | Null | Meaning |
|---|---|---|---|
| `key` | `INTEGER PRIMARY KEY` | no | The key |
| `fen` | `TEXT` | no | The normalized FEN, for display; never a key |
| `eco` | `TEXT` | yes | ECO code (§10); present exactly when `name` is |
| `name` | `TEXT` | yes | Opening name (§10) |
| `eval_cp` | `INTEGER` | yes | Centipawns from white's side |
| `eval_mate` | `INTEGER` | yes | Mate in n moves from white's side: n > 0 white mates, n < 0 black mates |
| `eval_depth` | `INTEGER` | yes | Depth of the evaluation (§9) |
| `best_move` | `TEXT` | yes | The evaluation's first move (§9) |

A position is evaluated exactly when `eval_depth` is not null; then exactly one of `eval_cp` and
`eval_mate` is not null and `best_move` is not null. An unevaluated position has all four null.

### 6.3 `nodes`

`nodes(key INTEGER NOT NULL, tier INTEGER NOT NULL, games INTEGER NOT NULL, white INTEGER NOT
NULL, draws INTEGER NOT NULL, black INTEGER NOT NULL, PRIMARY KEY (key, tier)) WITHOUT ROWID`.

A `(key, tier)` survives, and has a row, exactly when its visits (§6.4) at the tier number at
least `min_games`. `games` is the visits; `white`, `draws` and `black` split them by the game's
result. A position visited only at the depth cap has a `nodes` row and no `moves` rows, which is
why the threshold and the off-book rule (§7.3) read this table.

### 6.4 `moves` and the counting rule

`moves(key INTEGER NOT NULL, tier INTEGER NOT NULL, move TEXT NOT NULL, child INTEGER NOT NULL,
white INTEGER NOT NULL, draws INTEGER NOT NULL, black INTEGER NOT NULL, badges INTEGER NOT NULL,
PRIMARY KEY (key, tier, move)) WITHOUT ROWID`.

One row for every move played from a surviving `(key, tier)` at that tier, whether or not its
child survives. `child` is the key of the position after the move; it may have no `nodes` row at
the tier, or no `positions` row at all. `white`, `draws` and `black` count the plays of the move
by the game's result; their sum is the move's **total**. `badges` is §7.3's bit set. A lookup of a
position's moves at a tier is a range of the primary key.

**Counting.** An admitted game of tier t visits each of its positions at plies 0 to `max_ply` and
plays its moves from the positions at plies 0 to `max_ply` − 1; a game that ends earlier stops at
its last position. Each visit adds one to the position's `games` at t and to the column of the
game's result; each play adds one to the move's column of the game's result. A game that repeats
a position visits it once per occurrence.

**Invariants**, which the pipeline's and the store's tests check:

1. `games = white + draws + black ≥ min_games` in every `nodes` row, and every `moves` row has a
   total of at least one and a `nodes` row for its `(key, tier)`.
2. Outflow: for every node, the sums of its moves' `white`, `draws` and `black` are at most the
   node's; the difference is the visits at which the game ended or reached the cap.
3. Inflow: a node's visits at t, column by column, equal the admitted games of tier t that start
   there (every one of them for the start position, whose number is `admitted_tier_t` in `meta`;
   none elsewhere) plus the plays into it at t from every parent. The database stores only the
   plays from surviving parents, so there the stored inflow is at most the node's counts; with
   `min_games` = 1 every visited position survives and inflow holds with equality in the
   database.

## 7. Badges and off book

The pipeline computes the badges once, after the evaluation merge; the app reads them.

### 7.1 Thresholds

| Threshold | Value |
|---|---|
| `popular_share` | 10% |
| `book_share` | 5% |
| `book_margin` | 50 cp |
| `dubious_margin` | 100 cp |

### 7.2 Share and loss

For a `moves` row of parent p, tier t and move m, with child c:

- **Share** at t is the move's total divided by the sum of the totals of p's `moves` rows at t.
  A share whose sum is zero is zero, as is the share at t of a move with no row at t. "Share ≥
  x%" is evaluated in integers as `sum > 0 and 100 * total >= x * sum`.
- **Value** of an evaluated position, in centipawns from white's side: `eval_cp`, or for
  `eval_mate` m, `sign(m) * (100000 − |m|)`.
- **Loss** is `max(0, s * value(p) − s * value(c))`, where s is +1 when white is to move at p and
  −1 when black is, so both values are from the mover's side. Loss is defined only when p and c
  are both evaluated.

### 7.3 The badges and the off-book rule

`badges` is the sum of the bits that apply, every other bit zero:

| Bit | Badge | Applies when |
|---|---|---|
| 1 | Book | p has a `nodes` row at tier 6, the totals of p's `moves` rows at tier 6 sum to more than zero, the move's share at tier 6 is at least `book_share`, and the loss is defined and at most `book_margin` |
| 2 | Popular | the move's share at t is at least `popular_share` |
| 4 | Dubious | Popular applies, and the loss is defined and at least `dubious_margin` |

A move with a missing evaluation on either side is therefore neither Book nor Dubious; a
position with no legal moves has no usable first move, so §9 rule 5 never evaluates it, and a move
into a checkmate or stalemate is neither. Book reads only tier 6, so a move carries the same Book
bit at every tier it was played at. Dubious implies Popular, and Book and Dubious never apply
together.

**Off book.** A position is off book at tier t when it has no `nodes` row at t; a move leads off
book at t when its child does. This is the pipeline's survival threshold (§6.3), so the app and
the pipeline agree on what the book is.

## 8. Knobs and the size budget

| Knob | Meaning | Development | Release |
|---|---|---|---|
| `min_games` | Visits a `(key, tier)` needs to survive (§6.3) | 100 | 100 |
| `max_ply` | The depth cap in plies (§6.4) | 30 | 30 |
| `months` | Months of dumps read | 3 | 12 |

The knobs are the pipeline's arguments and are recorded in `meta` (§6.1). The sample corpus is
built at lower knobs, which its golden dump's `meta` records.

`min_games` is one number with two uses: the threshold at which pass one's count-min sketch makes
a `(key, tier)` a candidate, and the survival threshold applied to the exact count of pass two.
The sketch never underestimates, so every `(key, tier)` that survives is a candidate and its
counts are exact; a candidate whose exact visits fall below `min_games` is discarded. Which
`(key, tier)` survive therefore depends only on the exact counts, never on the sketch's
dimensions.

**Size budget.** The database and Stockfish ship inside the installer while the installer stays
under about 500 MB; past 1 GB the database becomes a separate, versioned, one-time download the
app imports, and between the two the owner decides. GitHub Releases caps an asset at 2 GB. The
knobs are tuned against this budget.

## 9. The evaluation merge

**Input.** One JSON object per line (verified against the file's head on 2026-11-03 and the
format notes at database.lichess.org):

```
{"fen":"<placement> <side> <castling> <ep>",
 "evals":[{"pvs":[{"cp":<int>,"line":"<uci> <uci> ..."}, {"mate":<int>,"line":"..."}, ...],
           "knodes":<int>,"depth":<int>}, ...]}
```

The FEN has four fields. `evals` is ordered by its number of principal variations; each pv has
exactly one of `cp` and `mate`. Scores are from white's side (the file's head shows it, below), so
the merge stores them unchanged. Lichess documents `line` as UCI_Chess960 notation, where
castling is the king moving onto its own rook (`e1h1`); the file also carries castling in the
standard two-square form (`e1g1`), so rule 3 accepts both.

**Rule.** After pass two, when the surviving keys are known, the pipeline streams the file once:

1. Each line's FEN is parsed and normalized like any other (§5.1), so an en passant target
   without a legal capture is dropped, and keyed (§5.2). A line whose key has no `nodes` row at
   any tier is ignored.
2. Of the line's evaluations, the one with the greatest `depth` is chosen; ties go to the greater
   `knodes`, then to the earlier in the list.
3. The chosen evaluation's first pv gives `eval_cp` or `eval_mate`; its `depth` gives
   `eval_depth`; the first move of its `line` gives `best_move`, written as §5.3's castling move
   when it is a king move onto its own rook and kept as it is when it is already in the
   two-square form, and it must be legal in the position after that rewrite.
4. When two lines that rule 5 did not skip normalize to the same key, rule 2 chooses between their
   chosen evaluations, and the earlier line wins a full tie.
5. A line is skipped whole and counted in the pipeline's report, and the run continues, when it
   is malformed JSON, when its FEN cannot be read, or when the chosen evaluation's first pv is
   unusable under rule 3 (no score, no `line`, or a first move that is not legal); the merge never
   falls back to another evaluation of the line.

**Score perspective, as verified.** Lichess's notes do not state the perspective. The file does:
in `6k1/6p1/6N1/4K3/4N3/8/8/8 b - -`, where black has a king and a pawn against two knights and
cannot win, the score is `mate 23`, positive; and over the first 300,000 lines, positions in which
one side leads by nine or more points of material score in that side's favour from white's side
whichever side is to move.

## 10. Names

**Input** (verified against the five files on 2026-11-03): `a.tsv` to `e.tsv`, UTF-8 with LF line
ends, a header line `eco`, `name`, `pgn`, then one line per opening of three tab-separated
fields: an ECO code `A00` to `E99`, the name, and the moves in SAN with move numbers from the start
position (`1. e4 e5 2. Nf3`), without comments or a result.

**Rule.** The files are read in the order `a` to `e`, each in line order, the header skipped. Each
line's moves are replayed from the start position through chess-library, and the key of the final
position gets the line's `eco` and `name`; the positions on the way get nothing from that line.
When two lines reach the same key, the first in reading order wins and each later one is counted
as a collision and ignored. A line whose moves do not replay is skipped and counted. Both counts
are in the pipeline's report only. Names are stored only in `positions` rows (§6.2), so only
surviving positions carry one.

The UI shows the name of the deepest named position along the current move sequence: of the
positions from the start to the current one, the last that has a name. Names match by position, so
a transposition into a named position shows its name whatever the move order.

## 11. The canonical dump

`chess-coach-cli dump` writes the whole database as UTF-8 text, one record per line, every line
ending in LF (the last included), fields separated by one tab, no header. Each line's first field
is its record name, `meta`, `position`, `node` or `move`, and the record's fields follow it. The
records come in four groups, in this order:

| Record | Fields after the name | Order |
|---|---|---|
| `meta` | `<key> <value>` | By key, bytewise; every key but `built_at` and `pipeline_version` |
| `position` | `<key> <fen> <eco> <name> <eval_cp> <eval_mate> <eval_depth> <best_move>` | By key as unsigned |
| `node` | `<key> <tier> <games> <white> <draws> <black>` | By key as unsigned, then tier |
| `move` | `<key> <tier> <move> <child> <white> <draws> <black> <badges>` | By key as unsigned, then tier, then move bytewise |

One line of each record, with `<TAB>` standing for the tab character and the values illustrative:

```
meta<TAB>schema_version<TAB>1
position<TAB>10026961946044190907<TAB>rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -<TAB>-<TAB>-<TAB>19<TAB>-<TAB>75<TAB>e2e4
node<TAB>10026961946044190907<TAB>2<TAB>48210<TAB>23190<TAB>1904<TAB>23116
move<TAB>10026961946044190907<TAB>2<TAB>e2e4<TAB>11769534657094819628<TAB>13410<TAB>1102<TAB>12915<TAB>3
```

Keys, `key` and `child` alike, are unsigned decimal (§5.2); other integers are decimal with a
leading `-` when negative; text is written as stored; an absent value is `-`. No field holds a tab
or a line end.

Two databases are the same build exactly when their dumps are byte-equal. The same months, knobs,
evaluation file and TSV files give byte-equal dumps; this is how the sample corpus's golden dump
is checked and how a published database is verified. `built_at` and `pipeline_version` are left
out because they change with every build.

## 12. `profiles.db` and versioning

`profiles.db` lives in the data directory (`--data-dir`) and belongs to `core`'s profile store;
every read and write goes through its API. Timestamps are `INTEGER` seconds since the Unix epoch,
UTC.

### 12.1 Versioning

`meta(key TEXT PRIMARY KEY, value TEXT NOT NULL)` holds `schema_version`. Each version has one SQL
migration script that takes a file from the previous version to it. At open the store creates an
absent or zero-length file at version 1, by version 1's script, and refuses, without modifying
it, an existing non-empty file that has no `meta` table, or whose `meta` has no `schema_version`
row or one that is not a positive decimal integer. It then reads `schema_version`, migrates
an older file by running each later script in order, each in its own transaction that sets
`schema_version` as its last statement, and refuses a file newer than itself without modifying
it. A test opens a file from every earlier version.

### 12.2 Version 1 (Phase 2)

`profiles(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, tier INTEGER NOT NULL, created_at
INTEGER NOT NULL)`. Version 1's script creates the profile `Default` at tier 2.

`engine_cache(...) WITHOUT ROWID`, one row per line of one search:

| Column | Type | Null | Meaning |
|---|---|---|---|
| `key` | `INTEGER` | no | The searched position's key |
| `lines` | `INTEGER` | no | The search's line count (`MultiPV`) |
| `line` | `INTEGER` | no | This line's `multipv` index, from 1 to the number of lines the search returned |
| `depth` | `INTEGER` | no | The depth of this line's last `info` with an exact score (one marked `lowerbound` or `upperbound` does not count) |
| `score_cp` | `INTEGER` | yes | Centipawns from the side to move, as UCI gives it |
| `score_mate` | `INTEGER` | yes | Mate in moves from the side to move, as UCI gives it; exactly one of the two scores is not null |
| `pv` | `TEXT` | no | The principal variation, UCI moves separated by spaces |
| `engine` | `TEXT` | no | The engine's `id name` |
| `created_at` | `INTEGER` | no | When the search finished |

`PRIMARY KEY (key, lines, line)`. The engine cache's scores are from the side to move, unlike
`openings.db`'s, which are from white's side. Only full-strength searches are cached: a
limited-strength search (`UCI_LimitStrength`, as Spar runs) neither reads nor writes the cache,
and a search stopped before its time is not stored, since it may lack lines its position has.
A search stores exactly the lines it returned, which are fewer than `lines` when the position has
fewer legal moves, so a complete result has between one and `lines` rows. A lookup for `(key,
lines)` with a minimum depth hits when at least one row exists for the pair and the least depth
among its rows is at least the minimum. Otherwise the search runs, and its rows replace the
stored ones, in one transaction, only when nothing is stored or its least depth is at least the
stored least depth; when it is not, the stored rows are kept and returned. The cache, like the
key, ignores the move counters and the history.

### 12.3 Version 2 (Phase 4): `spar_sessions`, provisional

Columns as provisional until Phase 4 lands and specifies them here: `id INTEGER PRIMARY KEY`,
`profile_id` referencing `profiles(id)`, `tier`, `colour`, `seed`, `moves` (UCI, space-separated),
`started_at`, `ended_at`, and the summary's columns.

### 12.4 Version 3 (Phase 5): `repertoire_nodes`, `cards`, `reviews`, provisional

Columns as provisional until Phase 5 lands and specifies them here:

- `repertoire_nodes`: `profile_id`, `colour`, `key`, `fen`, `move` (the chosen move), primary key
  `(profile_id, colour, key)`; the FEN beside the key is what a re-keying migration reads (§14).
- `cards`: `id INTEGER PRIMARY KEY`, `profile_id`, `colour`, `key`, `move`, and the SM-2 state
  (ease, interval, repetitions, due); a card carries no FEN and is re-keyed through its repertoire
  node `(profile_id, colour, key)`.
- `reviews`: `card_id`, `reviewed_at`, `grade`, enough to replay a card's history to its state.

## 13. Sampler and score inputs

**Sampler defaults.** At a position and a tier, the candidates are the position's `moves` rows at
the tier whose total is at least 10. Each candidate has a whole-number weight, and is drawn with
probability proportional to it. The only temperature this contract defines is T = 1.0, at which
the weight is the move's total; it is the one Phase 4 ships. The intent of a temperature is a
weight of `total^(1/T)`, but floating-point powers differ between platforms and would break
replay, so any other temperature is exposed only after this section first defines its weight by
an integer-only rule. The generator is §5.2's splitmix64 with `state` set to the session's seed,
one output per draw, so the same seed gives the same draws. With no candidate the sampler returns
no move.

**The draw.** The candidates are ordered by move text, bytewise. One raw 64-bit output of the
generator, taken modulo the sum of the weights, selects the first candidate whose cumulative
weight exceeds it. The draw never goes through a standard-library distribution, whose results
differ between libstdc++ and libc++, because a session must replay identically on every
platform.

**Score inputs.** For each candidate move at a position and a tier, the recommendation score reads
the move's `moves` row and the position's `moves` rows at the tier, the evaluations of the
position and the child (for §7.2's loss), and the child's `nodes` row and `moves` rows at the
tier. Its soundness margin is `book_margin` (§7.1), applied where the loss is defined. The
formula, the simplicity penalty and the components the UI shows are in `docs/reference/core.md`.

## 14. Change procedure

- Every statement in this document is tier A to change, and the change to the document lands in
  the same series as the code that implements it.
- A change to `openings.db`'s schema increments `schema_version` (§6.1); the database is rebuilt,
  never migrated, and the store refuses every other version.
- A change to what the pipeline writes for the same inputs (a threshold, a knob default, the
  counting rule, normalization, the merge, the names) regenerates the sample corpus's golden dump
  in that series, under a tier A brief that says so.
- A change to `profiles.db`'s schema adds a version with its migration script and a test that
  opens a file of the previous version.
- A change to the key (the tables, the seed, the draw order or normalization) changes every key in
  both databases: it needs a new `openings.db` `schema_version` and a `profiles.db` migration that
  re-keys every stored key from the FEN stored beside it; a card, which carries no FEN, is
  re-keyed through its repertoire node `(profile_id, colour, key)`, and engine cache rows are
  dropped instead.

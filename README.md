# Chess - A simple, no clutter chess engine to practice

A native, offline chess desktop application: play against Stockfish, analyse games, train
with puzzles, opening repertoires and endgames, review your games with the engine and keep a
personal game library (see `PRIMER.txt` for the feature list and phases).

## Status

All five phases are implemented: core application, analysis, training, game review and the
advanced features.

Play:

- Playable board: drag and drop or click to move, legal move hints, last move and check
  highlights, promotion picker, board flip, sounds.
- Full rules: castling, en passant, promotion, checkmate, stalemate, draws by repetition,
  fifty-move rule and insufficient material.
- Move history, captured pieces, undo and redo, resign, mark the game as a draw, restart.
- Time controls from bullet to classical (or custom), with increments. The engine plans its
  own time; a player whose clock runs out loses, or draws when the other side cannot mate.
- Move badges: tick "Move badges" and every move is judged as it is played (best, excellent,
  good, inaccuracy, mistake, blunder), in the move list and on the board.
- Coach mode: after a mistake or blunder the computer waits, shows the stronger move and
  offers to take yours back. The clocks stop while the coach talks.
- Opening practice: in "New game", pick a named opening or one of your repertoires. The
  computer answers from it and the panel tells you when you leave it.
- Play against Stockfish as White or Black, at 8 levels or a chosen Elo, with a time and
  depth limit per move, from the start position or any FEN. The engine runs as a separate
  process and never blocks the window.
- Blindfold mode: the pieces are hidden and you play from the move list (B switches it,
  holding Space peeks). It also works in Puzzles.
- Hints: "Hint" (or H) marks the piece Stockfish would move; pressing it again shows the move
  as an arrow. Hints come from a second engine process at full strength, whatever level the
  opponent plays at.
- Six colour themes (Forest, Walnut, Midnight, Rosewood, Paper, Sand), each with matching
  board colours, the Source Serif 4 font (or the system font), translucent panels and a
  slow, softly animated background that can be turned off in Settings.
- Fullscreen with F11 or in Settings.
- Your own sound effects: in Settings, "Choose..." picks a file (ogg, mp3, wav or flac) for the
  move, capture and checkmate sounds, for example the ones from Lichess; "Reset" brings back the
  built-in sound.
- Settings (engine, dark or light mode, board colours, hints, animations, sound) saved to
  `%APPDATA%\ChessDesktop\settings.ini`.

Analysis:

- Live engine analysis with 1 to 5 lines (MultiPV), depth and speed, an evaluation bar and
  a best-move arrow. Click a line to play its first move.
- A game tree with nested variations: make moves on the board to branch, right-click a move
  to promote a variation, make it the main line or delete it. Comments per move.
- "Analyse" evaluates every move of the main line, fills the evaluation graph (click it to
  jump to a move) and marks each move as best, excellent, good, inaccuracy, mistake or
  blunder with a badge on the board, in the move list and on the graph.
- FEN and PGN: paste either from the clipboard (Ctrl+V), copy them, open and save PGN
  files. Variations, comments, annotation glyphs and `[%eval]` values are read and written.
- Right-drag on the board draws arrows, right-click marks squares.
- Explorer tab: the moves played from the current position in your library games, with how
  those games ended. "Only my games" shows your own results.
- Board overlays: square control (blue where White attacks a square more often, red for
  Black), pieces that can be won, and piece activity (red for pieces without moves, green
  for active ones).
- Engine comparison: set a second UCI engine in Settings and Analysis shows its evaluation,
  depth and line under the main engine's, and whether the two want the same move. Each
  engine takes its own list of UCI options (`Name=Value; Name=Value`).
- Endgame tablebases: point Settings at a folder of Syzygy files and the engine uses them in
  Play, Analysis and Game Review. In positions they cover, Analysis shows the exact result
  ("Tablebase: White wins" or "draw") and the engine's moves are the tablebase moves. Mate
  distances come from the engine's search; Syzygy tables do not store them. The files are not
  bundled; see <https://tablebase.lichess.ovh/tables/standard/>.

Game Library:

- Finished games from Play are stored automatically; games from Analysis on request.
- Search by player or event, filter by result, open a game in Analysis, delete, import PGN
  files (in the background) and export one game or all of them.
- Games live in an SQLite database at `%APPDATA%\ChessDesktop\games.db`.

Puzzles:

- A starter set of about 4,800 Lichess puzzles is imported on first start. The full
  [Lichess puzzle database](https://database.lichess.org/#puzzles) can be imported from
  its CSV file (unpack the `.zst` download first).
- A personal puzzle rating that moves with each rated attempt, a rating graph, streaks, a
  history you can replay and a daily puzzle.
- Spaced repetition: a missed puzzle comes back the next day under "Review due", then
  further apart each time you solve it, and from the start again when you miss it.
- Pick the difficulty relative to your rating and a theme (fork, pin, mate in 2, ...).
  Hints and "Solution" are available; using them counts the puzzle as missed.

Openings:

- Repertoires for White or Black, grouped by folder, built by playing moves on the board
  or by importing PGN files. Lines can be reordered, deleted and exported.
- Training: the app plays the opponent's moves and asks for yours. Each position is
  scheduled with spaced repetition, so known moves return after days and missed ones
  sooner. Transpositions count as one position.
- Opening names (ECO code and name) are shown in Openings and Analysis.
- Three starter repertoires are added on first start, in the folder "Starter": 1. e4 for
  White (Italian Game and answers to the main defences), the Caro-Kann against 1. e4 and the
  Queen's Gambit Declined against 1. d4. Edit or delete them like your own.

Guess the Move:

- Play through a game from one side and guess each move: the move from the game earns 5
  points, any other move up to 5 by how close the engine rates it. Six classic games are
  bundled; any library game with ten or more moves works too.

Endgames:

- 16 training positions (basic mates, king and pawn, opposition, rook and queen endings),
  each checked with Stockfish, played against the engine at full strength with a stated
  goal (win or hold the draw). Progress is kept per position.

Game Review:

- Pick a game from the library (or press "Review game" after a game in Play, or "Review" in
  the Game Library) and the engine checks every move in the background.
- Accuracy for both players, every move classified as best, excellent, good, inaccuracy,
  mistake or blunder, and a list of key moments (mistakes, blunders, missed wins, missed
  mates). Up and Down jump between them.
- On a weak move the board shows the move played and the move the engine preferred as
  arrows, with a line of text saying how much it cost.
- Each move comes with a short explanation built from what is on the board: what the move
  attacks or wins, what a weak move allows, and why the engine's move was stronger. The
  "Position" tab lists loose pieces, pawn weaknesses, passed pawns and king cover for both
  sides. These are rule-based descriptions, not deep plans.
- "Puzzles" turns the moments where a mistake could be punished into puzzles. The engine
  keeps a move only while it is clearly better than every alternative, so each solution is
  verified and unambiguous. Puzzles get a difficulty estimate and themes and appear in
  Puzzles under the theme "From my games".
- "Review all new games" works through every library game without a review.
- Stepping through the moves plays the move and capture sounds.
- Reviews are stored with the game (evaluations and engine moves go into its PGN as
  `[%eval]` and `[%best]` comments), so opening it again needs no engine.

Training:

- Dashboard: blunders and mistakes per game (with the last ten games), average centipawn
  loss, puzzle and endgame success, results against each engine level and strength, mistakes
  per game phase, and what is due today.
- Weaknesses: every mistake and blunder in your reviewed games is checked for the tactic the
  engine's move had ("missed a fork") and the one your move let the opponent play ("got
  pinned", "left a piece hanging", "allowed a mate", ...). Patterns are ranked, each with a
  tip, its examples (click one to open the game in Game Review at that move) and buttons to
  practise it with puzzles or drill your own examples. Detection looks at the first move of
  each line, so deeper combinations count as "positional or deeper".
- Mistake drill: the positions where you went wrong, again. Any move the engine rates about
  as good as its own counts. Missed positions come back the next day, solved ones on a
  spaced schedule.

Statistics:

- Built from the reviewed games you played: results, average accuracy overall and per
  colour, accuracy game by game, accuracy per game phase with the weakest one pointed out,
  how often each kind of move occurs, and results and accuracy per opening.
- Time spent in the app, today and in total, a day streak and a calendar of the last year.
- Your side is found by name. Games played here use "You"; add the names you use elsewhere
  (comma separated) at the bottom of the Statistics section to include imported games.

## Building

Requirements: [xmake](https://xmake.io) and a C++20 compiler (Visual Studio with the C++
workload on Windows, Xcode command line tools on macOS).
Dependencies (SFML 3, Dear ImGui, ImGui-SFML, nanosvg, SQLite, doctest) are downloaded by xmake.

```
xmake f -m release
xmake build chess
xmake run chess
```

All operating system specifics (engine process, user folders, file dialogs, the connection to
Discord, high-DPI windows, redrawing during a window resize) live in `src/platform`, with one
implementation for Windows and one for macOS and Linux (plus a few macOS-only parts in
`src/platform/macos`). Windows and macOS are built and tested; the Linux build has not been
compiled or run yet.

On macOS and Linux, user data goes to `~/Library/Application Support/ChessDesktop` or
`~/.local/share/ChessDesktop`, and the engine is any executable file whose name starts with
`stockfish` inside `engines/`.

Run the unit tests with `xmake build tests` and `xmake run tests`.

## Engine

The app looks for a `stockfish*.exe` anywhere inside an `engines/` folder next to the
executable or in one of its parent folders. Download a Windows build from
<https://stockfishchess.org/download/> and unpack it there, or set a path to any UCI
engine in Settings. The `engines/` folder is not committed.

To make a release with the engine and every license included, run
`python3 tools/package_release.py`. It builds the release version, bundles the first Stockfish
executable in `engines/` (or `--engine <path>`) with its license (`Copying.txt`) and a
`SOURCE.txt` pointing to the source of that exact version, as the GPL requires, and writes to
`release/`:

- Windows and Linux: `Chess-<version>-<platform>.zip`, a folder with the program, `data/` and
  `engines/`.
- macOS: `Chess-<version>-macos-arm64.dmg` with `Chess.app` (Apple Silicon, macOS 13 or later).
  The build targets macOS 13 and FreeType is compiled in, so the app runs on Macs without
  developer tools. It is signed ad hoc: on another Mac, the first launch needs System Settings >
  Privacy & Security > "Open Anyway". With an Apple Developer ID, pass
  `--sign "Developer ID Application: Name (TEAMID)"` and `--notarize <profile>` (a
  `xcrun notarytool store-credentials` profile) to get an app that opens without that step.

`xmake build ucicheck` builds a small console tool that starts an engine and asks it for a
move: `xmake run ucicheck <path-to-engine>`.

## Discord Rich Presence

The app can show what you are doing ("Playing Stockfish 19 (level 3) - Move 12",
"Analysing a game", ...) in your Discord status. It talks directly to the Discord client's local
RPC connection (`discord-ipc-N`, a Unix socket on macOS and Linux, a named pipe on Windows), so
no Discord library is needed. Without Discord running, the app works as usual and keeps checking
in the background. Presence can be switched off in Settings.

## Layout

| Path | Contents |
| --- | --- |
| `src/chess` | Rules, move history, FEN, the game tree, PGN, move classification, game review and statistics, position facts (attacks, loose pieces, pawn structure), move explanations, puzzle solving and building, repertoire logic, endgame positions. No UI or engine dependencies. |
| `src/platform` | Operating system layer: child processes, user folders, file dialogs, local connections (Discord), high-DPI windows. Windows and macOS/Linux implementations. |
| `src/engine` | The asynchronous UCI client. |
| `src/storage` | SQLite stores: games, game reviews, puzzles and attempts, repertoires and training progress. |
| `src/app` | Window, main loop, settings, theme, assets, background game analysis, puzzle generation, the comparison engine, Discord presence. |
| `src/ui` | Board view, move tree view and one screen per section. |
| `tests` | Unit tests for the rules, PGN, move classification, game review, statistics, puzzles, spaced repetition, the opening book, the UCI parser and the stores. |
| `tools` | `ucicheck` (engine check) and `make_badges.py`, which draws the move-quality badges in `data/badges` (needs Python with Pillow; only to regenerate them). |
| `third_party/chess` | [chess-library](https://github.com/Disservin/chess-library) by Disservin (MIT). |
| `data` | Piece set, sounds, opening names, starter puzzles, starter repertoires and the classic games for Guess the Move. The opening tables and puzzles come from [Lichess](https://database.lichess.org) (CC0). The cburnett pieces (by Colin M.L. Burnett, GPLv2+) come from the [lichess](https://github.com/lichess-org/lila) repository and are rasterized from SVG at startup. The sounds are from [Kenney](https://kenney.nl) (CC0). |

## Shortcuts

| Key | Action |
| --- | --- |
| Left / Right | Play: undo / redo. Analysis and Game Review: previous / next move |
| Up / Down | Analysis: switch between variations. Game Review: previous / next key moment |
| Home / End | Analysis and Game Review: start / end of the line |
| Ctrl+V | Analysis: paste a FEN or PGN |
| F | Flip the board |
| H | Play: hint (first the piece, then the move) |
| F11 | Fullscreen on / off |
| B / hold Space | Play and Puzzles: blindfold on / off, peek at the pieces |
| Ctrl+N | New game |
| Right click / right drag | Mark a square / draw an arrow (a left click clears them) |

## License

Copyright (C) 2026 Moonscorched Productions.

This program is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. See `LICENSE`.

Third-party code, data and assets keep their own licenses; see `THIRD_PARTY_NOTICES.md`.

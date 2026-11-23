# Chess - A simple, no clutter chess engine to practice

A native, offline chess desktop application: play against Stockfish, analyse games, train
with puzzles, opening repertoires and endgames, and keep a personal game library. Full game review is planned
(see `PRIMER.txt` for the feature list and phases).

## Status

Phases 1 (core application), 2 (analysis) and 3 (training) are implemented.

Play:

- Playable board: drag and drop or click to move, legal move hints, last move and check
  highlights, promotion picker, board flip, sounds.
- Full rules: castling, en passant, promotion, checkmate, stalemate, draws by repetition,
  fifty-move rule and insufficient material.
- Move history, captured pieces, undo and redo, resign, draw offers, restart.
- Play against Stockfish as White or Black, at 8 levels or a chosen Elo, with a time and
  depth limit per move, from the start position or any FEN. The engine runs as a separate
  process and never blocks the window.
- Settings (engine, board colours, hints, sound) saved to `%APPDATA%\ChessDesktop\settings.ini`.

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
  history you can replay, a daily puzzle and a retry queue for missed puzzles.
- Pick the difficulty relative to your rating and a theme (fork, pin, mate in 2, ...).
  Hints and "Solution" are available; using them counts the puzzle as missed.

Openings:

- Repertoires for White or Black, grouped by folder, built by playing moves on the board
  or by importing PGN files. Lines can be reordered, deleted and exported.
- Training: the app plays the opponent's moves and asks for yours. Each position is
  scheduled with spaced repetition, so known moves return after days and missed ones
  sooner. Transpositions count as one position.
- Opening names (ECO code and name) are shown in Openings and Analysis.

Endgames:

- 16 training positions (basic mates, king and pawn, opposition, rook and queen endings),
  each checked with Stockfish, played against the engine at full strength with a stated
  goal (win or hold the draw). Progress is kept per position.

## Building

Requirements: [xmake](https://xmake.io) and a C++20 compiler (Visual Studio with the C++
workload on Windows, Xcode command line tools on macOS).
Dependencies (SFML 3, Dear ImGui, ImGui-SFML, nanosvg, SQLite, doctest) are downloaded by xmake.

```
xmake f -m release
xmake build chess
xmake run chess
```

All operating system specifics (engine process, user folders, file dialogs, loading the
Discord library, redrawing during a window resize) live in `src/platform`, with one
implementation for Windows and one for macOS and Linux. Development and testing so far
happened on Windows only: the macOS and Linux implementation is written but has not been
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

`xmake build ucicheck` builds a small console tool that starts an engine and asks it for a
move: `xmake run ucicheck <path-to-engine>`.

## Discord Rich Presence (optional)

The app can show what you are doing ("Playing Stockfish 19 (level 3) - Move 12",
"Analysing a game", ...) in your Discord status. It uses the legacy Discord Game SDK, which
is not committed. To enable it, unpack
[discord_game_sdk.zip](https://dl-game-sdk.discordapp.net/3.2.1/discord_game_sdk.zip) so
that these files exist, then rebuild:

```
third_party/discord_game_sdk/c/discord_game_sdk.h
third_party/discord_game_sdk/lib/x86_64/discord_game_sdk.dll
```

The library (`.dll` on Windows, `.dylib` on macOS, from `lib/x86_64` or `lib/aarch64`) is copied next
to the executable and loaded at run time. Without the SDK, without
the DLL or without Discord running, the app works as usual. Presence can be switched off in
Settings.

## Layout

| Path | Contents |
| --- | --- |
| `src/chess` | Rules, move history, FEN, the game tree, PGN, move classification, puzzle and repertoire logic, endgame positions. No UI or engine dependencies. |
| `src/platform` | Operating system layer: child processes, user folders, file dialogs, shared libraries. Windows and macOS/Linux implementations. |
| `src/engine` | The asynchronous UCI client. |
| `src/storage` | SQLite stores: games, puzzles and attempts, repertoires and training progress. |
| `src/app` | Window, main loop, settings, assets, Discord presence. |
| `src/ui` | Board view, move tree view and one screen per section. |
| `tests` | Unit tests for the rules, PGN, move classification, puzzles, spaced repetition, the opening book, the UCI parser and the stores. |
| `third_party/chess` | [chess-library](https://github.com/Disservin/chess-library) by Disservin (MIT). |
| `data` | Piece set, font, sounds, opening names and starter puzzles. The opening tables and puzzles come from [Lichess](https://database.lichess.org) (CC0). The cburnett pieces (by Colin M.L. Burnett, GPLv2+) come from the [lichess](https://github.com/lichess-org/lila) repository and are rasterized from SVG at startup. |

## Shortcuts

| Key | Action |
| --- | --- |
| Left / Right | Play: undo / redo. Analysis: previous / next move |
| Up / Down | Analysis: switch between variations |
| Home / End | Analysis: start / end of the line |
| Ctrl+V | Analysis: paste a FEN or PGN |
| F | Flip the board |
| Ctrl+N | New game |
| Right click / right drag | Mark a square / draw an arrow (a left click clears them) |

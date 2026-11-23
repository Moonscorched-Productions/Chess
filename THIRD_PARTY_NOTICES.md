# Third-party notices

This program is licensed under the GNU General Public License, version 3 (see `LICENSE`).
It uses the following third-party code, data and assets, under their own licenses.

## Code

| Component | Author | License | Used for |
| --- | --- | --- | --- |
| [SFML](https://www.sfml-dev.org) 3 | Laurent Gomila and contributors | zlib | Window, graphics, audio |
| [Dear ImGui](https://github.com/ocornut/imgui) | Omar Cornut and contributors | MIT | User interface |
| [ImGui-SFML](https://github.com/SFML/imgui-sfml) | Elias Daler and contributors | MIT | ImGui backend for SFML |
| [nanosvg](https://github.com/memononen/nanosvg) | Mikko Mononen | zlib | Rasterizing the piece set |
| [SQLite](https://sqlite.org) | D. Richard Hipp and contributors | Public domain | Game, puzzle and training database |
| [chess-library](https://github.com/Disservin/chess-library) (`third_party/chess`) | Disservin | MIT | Move generation |
| [doctest](https://github.com/doctest/doctest) | Viktor Kirilov | MIT | Unit tests only, not part of the program |

Libraries built into SFML or pulled in by it:

| Component | License |
| --- | --- |
| [FreeType](https://freetype.org) | FreeType License. Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved. |
| [libogg, libvorbis](https://xiph.org) | BSD 3-clause, copyright Xiph.Org Foundation |
| [libFLAC](https://xiph.org/flac/) | BSD 3-clause, copyright Xiph.Org Foundation and contributors |
| [zlib](https://zlib.net) | zlib, copyright Jean-loup Gailly and Mark Adler |
| [miniaudio](https://miniaud.io) | Public domain or MIT-0, David Reid |
| [minimp3](https://github.com/lieff/minimp3) | CC0 |
| [stb_image](https://github.com/nothings/stb) | Public domain or MIT, Sean Barrett |
| [glad](https://github.com/Dav1dde/glad) | MIT (generated code: public domain) |

The full license texts are distributed with each project's source code.

## Chess engine

[Stockfish](https://stockfishchess.org) is not part of this repository. Release builds that include it
ship it as a separate program in `engines/`, together with its license (GPL version 3, `Copying.txt`)
and a link to the source code of that exact version.

## Data and assets

| Files | Source | License |
| --- | --- | --- |
| `data/piece_set/cburnett` | Colin M.L. Burnett, via the [lichess](https://github.com/lichess-org/lila) repository | GPL version 2 or later |
| `data/openings` | [lichess-org/chess-openings](https://github.com/lichess-org/chess-openings) | CC0 1.0 |
| `data/puzzles/starter.csv` | [Lichess puzzle database](https://database.lichess.org/#puzzles) | CC0 1.0 |
| `data/sfx` | [Kenney](https://kenney.nl) sound packs | CC0 1.0 |
| `data/fonts/SourceSerif4-Regular.ttf` | Source Serif 4 by Frank Grießhammer, [Adobe](https://github.com/adobe-fonts/source-serif) | SIL Open Font License 1.1 (`data/fonts/OFL.txt`) |
| `data/games/classics.pgn`, `data/repertoires` | Historical games and opening lines (this project) | GPL version 3, like the program |
| `data/badges` | Drawn by `tools/make_badges.py` (this project) | GPL version 3, like the program |
| `data/moon-pawn-icon.png` | Moonscorched Productions (this project) | GPL version 3, like the program |

Lichess is not affiliated with and does not endorse this program.

## Optional components (not distributed)

- Interface fonts are read from the operating system at run time; none are bundled.

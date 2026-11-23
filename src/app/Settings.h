#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace app {

enum class StrengthMode { Level, Elo };
enum class ColorChoice { White, Black, Random };

struct BoardTheme {
    const char* name;
    std::array<float, 3> light;
    std::array<float, 3> dark;
};

// Built-in board colour presets. The last entry of the settings UI is "Custom".
const std::array<BoardTheme, 4>& boardThemes();

// Application colour themes. Each comes with board colours that suit it.
struct UiTheme {
    const char* name;
    bool light;
    std::array<float, 3> boardLight;
    std::array<float, 3> boardDark;
};
const std::array<UiTheme, 6>& uiThemes();

// Engine strength behind each difficulty level (1..8).
struct LevelPreset {
    const char* name;
    int skillLevel;  // UCI "Skill Level", 0..20
    int depth;       // search depth cap, 0 = none
};
const LevelPreset& levelPreset(int level);

// User preferences. Plain data; persisted as a small key=value text file.
struct Settings {
    // Engine
    std::string enginePath;  // empty = use the bundled engine from the engines/ folder
    // Extra UCI options for the engine, "Name=Value; Name=Value". Applied after the app's own.
    std::string engineOptions;
    std::string syzygyPath;  // folder with Syzygy endgame tablebases; empty = none
    // A second engine for comparison in Analysis, with its own option list.
    std::string compareEnginePath;
    std::string compareEngineOptions;
    bool compareEngines = true;  // show the comparison engine's line when one is set
    StrengthMode strengthMode = StrengthMode::Level;
    int level = 3;           // 1..8
    int elo = 1500;          // used with StrengthMode::Elo
    int thinkTimeMs = 1000;  // maximum time per engine move
    int depthLimit = 0;      // 0 = no extra depth limit
    // Time control for games in Play: base time per side and increment per move (0 = untimed).
    int clockBaseSeconds = 0;
    int clockIncrementSeconds = 0;

    // Analysis
    int analysisLines = 3;       // engine lines shown (MultiPV), 1..5
    int analysisThreads = 0;     // 0 = choose from the number of CPU cores
    int analysisHashMb = 256;    // engine hash table size
    int analysisDepth = 16;      // depth per move when analysing a whole game
    int analysisMoveTimeMs = 1000;  // ... and the most time to spend on one move
    bool showBestMoveArrow = true;
    bool saveGamesToLibrary = true;  // store finished games from Play in the library
    bool discordPresence = true;     // show what you are doing in Discord (when it is running)
    // Names (comma separated) under which the user appears in game files; "You" is used by Play.
    std::string playerNames = "You";
    std::string displayName;  // how the home page greets the user; empty = not asked yet

    // Appearance
    int uiTheme = 0;                // index into uiThemes()
    bool lightMode = false;         // follows the theme (kept for older settings files)
    bool serifFont = true;          // the bundled Source Serif; false = the system's interface font
    bool animatedBackground = true; // slow, soft light behind the panels
    bool fullscreen = false;
    bool blindfold = false;  // hide the pieces in Play and Puzzles
    bool playMoveBadges = false;  // judge every move in Play as it is made (Best, Mistake, ...)
    bool coachMode = false;       // in Play, stop after a mistake or blunder and offer a take-back

    // New game defaults
    ColorChoice playAs = ColorChoice::White;
    bool playAgainstEngine = true;

    // Board
    std::array<float, 3> lightSquare = boardThemes()[0].light;
    std::array<float, 3> darkSquare = boardThemes()[0].dark;
    bool showCoordinates = true;
    bool showLegalMoves = true;
    bool highlightLastMove = true;
    bool animations = true;  // pieces slide, captures fade, the evaluation bar glides

    // Sound
    bool soundEnabled = true;
    int soundVolume = 70;  // 0..100
    // The user's own sound files (ogg, mp3, wav or flac); empty = the built-in sound.
    std::string moveSoundFile;
    std::string captureSoundFile;
    std::string gameEndSoundFile;

    // Reads settings from the file. Missing or malformed entries keep their defaults.
    static Settings load(const std::filesystem::path& file);
    // Writes the settings. Returns false if the file could not be written.
    bool save(const std::filesystem::path& file) const;

    // Brings every value into its valid range.
    void sanitize();
};

}  // namespace app

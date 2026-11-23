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
    StrengthMode strengthMode = StrengthMode::Level;
    int level = 3;           // 1..8
    int elo = 1500;          // used with StrengthMode::Elo
    int thinkTimeMs = 1000;  // maximum time per engine move
    int depthLimit = 0;      // 0 = no extra depth limit

    // Analysis
    int analysisLines = 3;       // engine lines shown (MultiPV), 1..5
    int analysisThreads = 0;     // 0 = choose from the number of CPU cores
    int analysisHashMb = 256;    // engine hash table size
    int analysisDepth = 16;      // depth per move when analysing a whole game
    int analysisMoveTimeMs = 1000;  // ... and the most time to spend on one move
    bool showBestMoveArrow = true;
    bool saveGamesToLibrary = true;  // store finished games from Play in the library
    bool discordPresence = true;     // show what you are doing in Discord (when it is running)

    // New game defaults
    ColorChoice playAs = ColorChoice::White;
    bool playAgainstEngine = true;

    // Board
    std::array<float, 3> lightSquare = boardThemes()[0].light;
    std::array<float, 3> darkSquare = boardThemes()[0].dark;
    bool showCoordinates = true;
    bool showLegalMoves = true;
    bool highlightLastMove = true;

    // Sound
    bool soundEnabled = true;
    int soundVolume = 70;  // 0..100

    // Reads settings from the file. Missing or malformed entries keep their defaults.
    static Settings load(const std::filesystem::path& file);
    // Writes the settings. Returns false if the file could not be written.
    bool save(const std::filesystem::path& file) const;

    // Brings every value into its valid range.
    void sanitize();
};

}  // namespace app

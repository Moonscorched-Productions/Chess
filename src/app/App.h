#pragma once

#include <filesystem>
#include <array>
#include <memory>
#include <optional>
#include <string>

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>
#include <chrono>
#include <SFML/Window/Cursor.hpp>
#include <imgui.h>

#include "app/Assets.h"
#include "chess/OpeningBook.h"
#include "platform/LiveResize.h"
#include "app/RichPresence.h"
#include "app/Settings.h"

namespace storage {
class GameStore;
class PuzzleStore;
class ReviewStore;
class TrainingStore;
}

namespace ui {
class AnalysisScreen;
class LibraryScreen;
class OpeningsScreen;
class EndgameScreen;
class PlayScreen;
class PuzzleScreen;
class GuessScreen;
class TrainingScreen;
class ReviewScreen;
class StatisticsScreen;
class SettingsScreen;
}  // namespace ui

namespace app {

enum class Section { Play, Analysis, Review, Training, Puzzles, Guess, Openings, Endgames, Library, Statistics, Settings };

// Command line options. All of them are aids for automated checks.
struct Options {
    std::string screenshotPath;      // --screenshot <file>: save the window contents, then exit
    unsigned width = 1280;           // --size <width>x<height>: initial window size
    unsigned height = 800;
    float screenshotAfter = 2.0f;    // --after <seconds>: when to take the screenshot
    bool autoplay = false;           // --autoplay: the human side plays random legal moves
    int hintLevel = 0;               // --hint <1|2>: ask for a hint in Play (1 the piece, 2 the move)
    bool openSettings = false;       // --settings: start on the Settings section
    bool openNewGame = false;        // --new-game: start with the new game dialog open
    bool openAnalysis = false;       // --analysis: start on the Analysis section
    bool openLibrary = false;        // --library: start on the Game Library section
    bool openPuzzles = false;        // --puzzles: start on the Puzzles section
    int guessGame = -1;              // --guess <n>: start guessing bundled game n
    int theme = -1;                  // --theme <n>: colour theme (index into uiThemes())
    int trainingTab = -1;            // --training <tab>: open Training on a tab (0..2)
    bool autoGuess = false;          // --autoguess: guess random moves (test aid)
    bool openOpenings = false;       // --openings: start on the Openings section
    bool openEndgames = false;       // --endgames: start on the Endgames section
    bool openReview = false;         // --review: start on Game Review (and review the --pgn game, if given)
    bool reviewAll = false;          // --review-all: review every library game without a review
    bool openStatistics = false;     // --statistics: start on the Statistics section
    std::string playerNames;         // --player <names>: the user's names in game files
    bool lightMode = false;          // --light: use the light colour scheme
    bool fullscreen = false;         // --fullscreen: start in fullscreen
    bool blindfold = false;          // --blindfold: hide the pieces in Play and Puzzles
    bool moveBadges = false;         // --badges: judge every move in Play
    bool coach = false;              // --coach: coach mode in Play
    bool explorer = false;           // --explorer: open Analysis on the Explorer tab
    std::string practise;            // --practise <name>: practise the first opening matching the name
    std::string clock;               // --clock 300+3: time control for Play (seconds + increment)
    bool makePuzzles = false;        // --make-puzzles: make puzzles from the reviewed --pgn game
    int overlay = 0;                 // --overlay <n>: board overlay in Analysis (1 control, 2 loose pieces, 3 activity)
    std::string fen;                 // --fen <fen>: load this position into Analysis
    std::string compareEngine;       // --compare <path>: comparison engine for Analysis
    std::string syzygyPath;          // --syzygy <folder>: Syzygy tablebase folder
    bool trainOpenings = false;      // --train: start an opening training session that answers itself
    std::string endgameId;           // --endgame <id>: start this endgame training position
    std::string repertoireFile;      // --repertoire <pgn>: create a White repertoire from this file
    bool autoSolve = false;          // --autosolve: puzzles solve themselves
    std::string pgnFile;             // --pgn <file>: load this game into Analysis
    std::string importFile;          // --import <file>: import this PGN file into the library
    bool analyseGame = false;        // --analyse-game: analyse the loaded game right away
    int ply = -1;                    // --ply <n>: show this ply of the loaded game
    bool temporarySettings = false;  // --no-save: ignore the settings file and use a throwaway game database
    bool forceDiscord = false;       // --discord: keep Discord Rich Presence on even with --no-save

    static Options parse(int argc, char** argv);
};

class App {
public:
    explicit App(const Options& options);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Runs the main loop until the window is closed. Returns the process exit code.
    int run();

private:
    // Updates and draws one frame. Does not pump window events.
    void frame();
    // Window setup shared by the first window and every re-creation.
    void configureWindow();
    // Starts the ImGui frame. ImGui works in points; the window may report pixels.
    void beginImGuiFrame();
    void updateMouseCursor();
    void renderImGui();
    void drawSidebar(float width, float height);
    void drawContent(float x, float width, float height);
    // Counts the time the app is open; `flush` writes it out right away.
    void trackUsage(bool flush);
    void saveSettings();
    void routeRequests();
    void updatePresence();
    void applyAppearance();
    void applyWindowMode();
    bool saveScreenshot();

    Options options_;
    std::filesystem::path settingsFile_;
    Settings settings_;

    sf::RenderWindow window_;
    float pixelsPerPoint_ = 1.0f;  // window pixels per ImGui unit (2 on a Retina display)
    std::array<std::optional<sf::Cursor>, ImGuiMouseCursor_COUNT> cursors_;
    ImGuiMouseCursor lastCursor_ = ImGuiMouseCursor_COUNT;
    bool imguiReady_ = false;
    platform::LiveResize liveResize_;
    RichPresence presence_;
    sf::Clock frameClock_;
    // Time the app has been open, written to the database every so often.
    std::chrono::steady_clock::time_point usageTick_ = std::chrono::steady_clock::now();
    double usagePending_ = 0.0;
    bool screenshotRequested_ = false;
    bool screenshotSaved_ = false;
    Assets assets_;
    std::string assetWarning_;

    std::unique_ptr<storage::GameStore> store_;
    std::unique_ptr<storage::PuzzleStore> puzzleStore_;
    std::unique_ptr<storage::ReviewStore> reviewStore_;
    std::unique_ptr<storage::TrainingStore> trainingStore_;
    core::OpeningBook openingBook_;
    std::unique_ptr<ui::PlayScreen> play_;
    std::unique_ptr<ui::AnalysisScreen> analysis_;
    std::unique_ptr<ui::LibraryScreen> library_;
    std::unique_ptr<ui::PuzzleScreen> puzzles_;
    std::unique_ptr<ui::GuessScreen> guess_;
    std::unique_ptr<ui::TrainingScreen> training_;
    Section shownSection_ = Section::Play;  // to notice when a section is opened
    std::unique_ptr<ui::OpeningsScreen> openings_;
    std::unique_ptr<ui::EndgameScreen> endgames_;
    std::unique_ptr<ui::ReviewScreen> review_;
    std::unique_ptr<ui::StatisticsScreen> statistics_;
    std::unique_ptr<ui::SettingsScreen> settingsScreen_;
    int appliedTheme_ = -1;
    float sidebarIndicatorY_ = -1.0f;  // screen y of the sidebar highlight, gliding to the selected entry
    sf::Clock backgroundClock_;  // drives the animated background
    bool appliedFullscreen_ = false;
    sf::Vector2u windowedSize_{1280u, 800u};
    Section section_ = Section::Play;
};

}  // namespace app

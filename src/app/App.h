#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>

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
class ReviewScreen;
class StatisticsScreen;
class SettingsScreen;
}  // namespace ui

namespace app {

enum class Section { Play, Analysis, Review, Puzzles, Openings, Endgames, Library, Statistics, Settings };

// Command line options. All of them are aids for automated checks.
struct Options {
    std::string screenshotPath;      // --screenshot <file>: save the window contents, then exit
    unsigned width = 1280;           // --size <width>x<height>: initial window size
    unsigned height = 800;
    float screenshotAfter = 2.0f;    // --after <seconds>: when to take the screenshot
    bool autoplay = false;           // --autoplay: the human side plays random legal moves
    bool openSettings = false;       // --settings: start on the Settings section
    bool openNewGame = false;        // --new-game: start with the new game dialog open
    bool openAnalysis = false;       // --analysis: start on the Analysis section
    bool openLibrary = false;        // --library: start on the Game Library section
    bool openPuzzles = false;        // --puzzles: start on the Puzzles section
    bool openOpenings = false;       // --openings: start on the Openings section
    bool openEndgames = false;       // --endgames: start on the Endgames section
    bool openReview = false;         // --review: start on Game Review (and review the --pgn game, if given)
    bool reviewAll = false;          // --review-all: review every library game without a review
    bool openStatistics = false;     // --statistics: start on the Statistics section
    std::string playerNames;         // --player <names>: the user's names in game files
    bool lightMode = false;          // --light: use the light colour scheme
    bool fullscreen = false;         // --fullscreen: start in fullscreen
    bool blindfold = false;          // --blindfold: hide the pieces in Play and Puzzles
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
    void drawSidebar(float width, float height);
    void drawContent(float x, float width, float height);
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
    bool imguiReady_ = false;
    platform::LiveResize liveResize_;
    RichPresence presence_;
    sf::Clock frameClock_;
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
    std::unique_ptr<ui::OpeningsScreen> openings_;
    std::unique_ptr<ui::EndgameScreen> endgames_;
    std::unique_ptr<ui::ReviewScreen> review_;
    std::unique_ptr<ui::StatisticsScreen> statistics_;
    std::unique_ptr<ui::SettingsScreen> settingsScreen_;
    bool appliedLightMode_ = false;
    bool appliedFullscreen_ = false;
    sf::Vector2u windowedSize_{1280u, 800u};
    Section section_ = Section::Play;
};

}  // namespace app

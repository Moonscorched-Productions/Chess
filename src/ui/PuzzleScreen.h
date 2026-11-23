#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "app/RichPresence.h"
#include "app/Settings.h"
#include "chess/GameTree.h"
#include "chess/Puzzle.h"
#include <string_view>

#include "storage/PuzzleStore.h"
#include "ui/BoardView.h"

namespace app {
class Assets;
}

namespace ui {

// The Puzzles section: tactical training with a personal rating and history.
class PuzzleScreen {
public:
    // `store` may be null when the database could not be opened. `starterCsv` is a small
    // bundled puzzle set that is imported the first time the database is empty.
    PuzzleScreen(app::Settings& settings, app::Assets& assets, storage::PuzzleStore* store,
                 std::filesystem::path databaseFile, const std::filesystem::path& starterCsv);
    ~PuzzleScreen();
    PuzzleScreen(const PuzzleScreen&) = delete;
    PuzzleScreen& operator=(const PuzzleScreen&) = delete;

    // Plays delayed replies and picks up a finished import. Call once per frame.
    void update();
    // Draws the section into the current ImGui window.
    void draw();

    // The puzzle the user asked to look at in Analysis, if any (cleared by the call).
    std::optional<core::GameRecord> takeAnalysisRequest();
    // True once after this screen changed the settings (so they can be saved).
    bool consumeSettingsChanged();
    app::PresenceText presence() const;

    // Switches the theme filter (a Lichess theme name such as "fork") and loads a puzzle.
    void practiseTheme(std::string_view theme);
    // Loads the most overdue puzzle for review, if any.
    void startReview() { loadDue(); }

    // Test aid: the solution is played automatically, puzzle after puzzle.
    void setAutoSolve(bool enabled) { autoSolve_ = enabled; }

private:
    using Clock = std::chrono::steady_clock;
    enum class Phase { Idle, Solving, Solved, Revealed };

    void loadNext();
    void loadDaily();
    void loadDue();
    bool begin(const core::Puzzle& puzzle, bool rated);
    void onMove(core::Move move);
    void recordOnce(bool solved);
    void useHint();
    void showSolution();
    void refreshStats();
    void startImport(const std::filesystem::path& file);
    void finishImport();
    double elapsedSeconds() const;

    void drawSidePanel(float width, float height);
    void drawHistory(float height);
    void drawRatingGraph(ImVec2 size);

    app::Settings& settings_;
    bool settingsChanged_ = false;
    app::Assets& assets_;
    storage::PuzzleStore* store_;
    std::filesystem::path databaseFile_;

    core::PuzzleSession session_;
    std::optional<core::Puzzle> puzzle_;
    Phase phase_ = Phase::Idle;
    bool rated_ = false;
    bool recorded_ = false;   // an attempt at this puzzle has been written to the history
    bool unaided_ = true;     // no mistake, hint or solution so far
    int hintLevel_ = 0;       // 0 none, 1 piece shown, 2 move shown
    BoardView board_;
    std::string feedback_;
    std::string notice_;
    Clock::time_point startedAt_{};
    double finalSeconds_ = 0.0;
    Clock::time_point replyAt_{};
    Clock::time_point revealAt_{};

    // Selection.
    int difficulty_ = 1;  // 0 easier, 1 normal, 2 harder
    int theme_ = 0;       // index into the theme list; 0 = any
    std::mt19937_64 rng_{std::random_device{}()};

    // Cached database views.
    storage::PuzzleStats stats_;
    std::vector<storage::PuzzleAttempt> history_;
    std::vector<int> ratingHistory_;
    int puzzleCount_ = 0;
    storage::ReviewQueue reviewQueue_;

    // Background import.
    std::thread importThread_;
    std::atomic<bool> importRunning_{false};
    std::atomic<bool> importDone_{false};
    std::atomic<bool> importCancel_{false};
    std::atomic<int> importProgress_{0};
    storage::PuzzleImportResult importResult_;

    std::optional<core::GameRecord> analysisRequest_;
    bool autoSolve_ = false;
    Clock::time_point autoAt_{};
};

}  // namespace ui

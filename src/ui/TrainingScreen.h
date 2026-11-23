#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "app/MoveJudge.h"
#include "app/RichPresence.h"
#include "app/Settings.h"
#include "chess/Game.h"
#include "chess/Repertoire.h"
#include "chess/Weakness.h"
#include "storage/PuzzleStore.h"
#include "ui/BoardView.h"

namespace app {
class Assets;
}

namespace storage {
class TrainingStore;
}

namespace ui {

// The Training section: what the user's games say about their play and what to practise.
//  - Dashboard: blunders per game, centipawn loss, puzzle and endgame success, results
//    against each engine level, the most common mistake patterns and what is due today.
//  - Weaknesses: the tactical patterns behind the user's mistakes, with examples.
//  - Mistake drill: the positions where the user went wrong, again, on a spaced schedule.
class TrainingScreen {
public:
    // `databaseFile` is read on a background thread; `training` may be null.
    TrainingScreen(app::Settings& settings, app::Assets& assets, std::filesystem::path databaseFile,
                   storage::TrainingStore* training);
    ~TrainingScreen();
    TrainingScreen(const TrainingScreen&) = delete;
    TrainingScreen& operator=(const TrainingScreen&) = delete;

    void update();
    void draw();
    // Rebuilds the numbers the next time the section is shown.
    void markDirty() { dirty_ = true; }
    // Call when the section is opened: the numbers are refreshed unless a drill is under way.
    void onShow() {
        if (drillPhase_ == DrillPhase::Idle || drillPhase_ == DrillPhase::Answered) dirty_ = true;
    }
    app::PresenceText presence() const;

    // Requests for other sections (each cleared by the call).
    std::optional<std::string> takePuzzleThemeRequest();  // "" = due puzzle reviews
    struct GameRequest {
        std::int64_t gameId = 0;
        int ply = 0;
    };
    std::optional<GameRequest> takeGameRequest();
    bool takeEndgameRequest();

    // Test aid: opens a tab (0 dashboard, 1 weaknesses, 2 drill).
    void showTab(int tab) { wantTab_ = tab; }

private:
    struct EngineResults {
        std::string label;  // "level 3", "1500 Elo"
        int games = 0;
        int wins = 0;
        int draws = 0;
        int losses = 0;
    };
    struct Data {
        core::TrainingReport report;
        std::vector<int> examplePlies;  // ply of each example's move, for opening it in Review
        storage::PuzzleStats puzzles;
        storage::ReviewQueue puzzleQueue;
        int endgameAttempts = 0;
        int endgameSuccesses = 0;
        std::vector<EngineResults> engine;
        int reviewedGames = 0;  // reviewed games of the user before the cap
    };

    // Mistake drill.
    enum class DrillPhase { Idle, Solving, Judging, Answered };

    void startBuild();
    void collectResult();
    void buildDrillQueue();
    void nextDrill();
    void answerDrill(core::Move move);
    void finishDrill(bool correct, const std::string& message);
    std::string cardKey(const core::MistakeExample& example) const;

    void drawDashboard();
    void drawWeaknesses();
    void drawDrill();

    app::Settings& settings_;
    app::Assets& assets_;
    std::filesystem::path databaseFile_;
    storage::TrainingStore* training_;

    // Data, built on a worker thread.
    std::unique_ptr<Data> data_;
    std::unique_ptr<Data> result_;
    std::thread worker_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> cancel_{false};
    bool dirty_ = true;

    int wantTab_ = -1;
    int selectedMotif_ = -1;  // Weaknesses tab: whose examples are listed (-1 = all)

    // Drill state.
    std::unordered_map<std::string, core::CardState> cards_;
    std::vector<std::size_t> queue_;  // indexes into data_->report.examples
    int dueCount_ = 0;                // cards due today
    int newCount_ = 0;                // mistakes never drilled
    std::optional<std::size_t> current_;
    int drillMotif_ = -1;             // only mistakes with this motif (-1 = any)
    DrillPhase drillPhase_ = DrillPhase::Idle;
    core::Game drillPosition_;
    BoardView board_;
    app::MoveJudge judge_;
    std::string tried_;  // UCI of the move being judged
    std::optional<core::Move> triedMove_;
    std::string drillMessage_;
    bool drillCorrect_ = false;
    int sessionRight_ = 0;
    int sessionTotal_ = 0;

    std::optional<std::string> puzzleThemeRequest_;
    std::optional<GameRequest> gameRequest_;
    bool endgameRequest_ = false;
};

}  // namespace ui

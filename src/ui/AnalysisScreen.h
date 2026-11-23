#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "app/ComparisonEngine.h"
#include "app/RichPresence.h"
#include "app/Settings.h"
#include "chess/Game.h"
#include "chess/GameTree.h"
#include "chess/OpeningBook.h"
#include "chess/Review.h"
#include "engine/UciEngine.h"
#include "storage/Explorer.h"
#include "ui/BoardView.h"
#include "ui/MoveTreeView.h"

namespace app {
class Assets;
}

namespace ui {

// The Analysis section: explore a game tree with live engine evaluation.
class AnalysisScreen {
public:
    // `book` (optional) names the opening of the line on the board.
    AnalysisScreen(app::Settings& settings, app::Assets& assets, const core::OpeningBook* book = nullptr);
    ~AnalysisScreen();
    AnalysisScreen(const AnalysisScreen&) = delete;
    AnalysisScreen& operator=(const AnalysisScreen&) = delete;

    // The game database the explorer reads (opened separately on a background thread).
    void setLibrary(std::filesystem::path databaseFile);
    // The explorer is rebuilt the next time it is shown (call after the library changed).
    void markLibraryDirty() { explorerDirty_ = true; }
    // Test aid: shows the Explorer tab.
    void showExplorer() { wantExplorerTab_ = true; }

    // Processes engine output and keeps the right search running. Call once per frame.
    // `visible` tells whether the Analysis section is on screen; the engine rests otherwise.
    void update(bool visible);

    // Draws the section into the current ImGui window.
    void draw();

    // Replaces the game being analysed.
    // `libraryId` is the library entry it came from, if any, so saving updates that entry.
    void load(core::GameRecord record, std::optional<std::int64_t> libraryId = std::nullopt);

    // Call after settings were edited elsewhere.
    void onSettingsChanged(bool enginePathChanged);
    // True once after this screen changed the settings (so they can be saved).
    bool consumeSettingsChanged();

    // The game the user asked to store in the library, if any (cleared by the call).
    struct SaveRequest {
        core::GameRecord record;
        std::optional<std::int64_t> libraryId;  // entry to replace; empty = add a new one
    };
    std::optional<SaveRequest> takeSaveRequest();
    // Reports the outcome of a save request.
    void onSaved(std::optional<std::int64_t> libraryId);

    // Test aid: starts analysing the whole game as soon as the engine is ready.
    void requestGameAnalysis() { wantBatch_ = true; }
    // Two short lines describing what is being analysed, for Discord Rich Presence.
    app::PresenceText presence() const;
    // Test aid: chooses the board overlay (0 = none).
    void setOverlay(int overlay) { overlay_ = overlay; }
    // Test aid: jumps to the given ply of the main line.
    void goToPly(int ply);

private:
    struct Line {
        int depth = 0;
        core::Eval eval;        // from White's point of view
        std::string san;        // principal variation in SAN
        core::Move firstMove;   // first move of the variation
        bool valid = false;
    };

    // Engine control.
    void startEngine();
    void configureEngine();
    void syncSearch(bool visible);
    void handleInfo(const engine::SearchInfo& info);
    void startBatch();
    void stopBatch();

    // Navigation and editing.
    void goTo(core::NodeId id);
    void playMove(core::Move move);
    void removeNode(core::NodeId id);
    void pasteFromClipboard();
    void openFile();
    void saveFile();
    core::Color sideToMoveAt(core::NodeId id) const;
    std::optional<core::Eval> currentEval() const;
    // Quality of the move leading to `id`, once both positions are analysed deeply enough.
    core::MoveQuality qualityOf(core::NodeId id) const;

    // Drawing.
    void drawEnginePanel();
    void drawComparison();
    // Square tints of the chosen board overlay for the current position.
    const std::vector<SquareTint>& overlayTints();
    void drawEvalGraph(ImVec2 size);
    void drawExplorer(float height);
    void startExplorerBuild();
    void stopExplorerBuild();
    void drawButtons();
    void handleShortcuts();

    app::Settings& settings_;
    app::Assets& assets_;
    const core::OpeningBook* book_ = nullptr;

    core::GameRecord record_;
    core::NodeId current_ = core::kRootNode;
    core::Game position_;  // the position at `current_`
    BoardView board_;
    MoveTreeView treeView_;
    bool flipped_ = false;

    // Engine state.
    engine::UciEngine engine_;
    bool engineStarted_ = false;
    bool engineEnabled_ = true;
    std::string engineError_;
    int searchId_ = 0;                        // 0 = no search running
    core::NodeId searchNode_ = core::kNoNode;  // node the running search belongs to
    bool searchIsBatch_ = false;
    int appliedMultiPv_ = 0;
    std::array<Line, 5> lines_{};
    int depth_ = 0;
    std::uint64_t nodesPerSecond_ = 0;

    std::uint64_t tablebaseHits_ = 0;  // reported by the live search

    // A second engine on the same position, for comparison.
    app::ComparisonEngine compare_;
    std::string comparePath_;
    std::string compareOptions_;

    // Board overlay (heatmap).
    int overlay_ = 0;
    std::string overlayKey_;
    std::vector<SquareTint> overlayTints_;

    // Whole-game analysis.
    bool wantBatch_ = false;
    std::vector<core::NodeId> batchQueue_;
    std::size_t batchIndex_ = 0;
    std::size_t batchTotal_ = 0;

    // UI state.
    std::string notice_;
    std::array<char, 512> comment_{};
    core::NodeId commentNode_ = core::kNoNode;
    core::NodeId pendingRemove_ = core::kNoNode;
    core::NodeId pendingPromote_ = core::kNoNode;
    core::NodeId pendingMainLine_ = core::kNoNode;
    std::optional<SaveRequest> saveRequest_;
    std::optional<std::int64_t> libraryId_;
    bool settingsChanged_ = false;

    // Explorer over the user's library, built on a worker thread.
    std::filesystem::path libraryFile_;
    std::unique_ptr<storage::ExplorerIndex> explorer_;
    std::unique_ptr<storage::ExplorerIndex> explorerResult_;  // written by the worker
    std::thread explorerThread_;
    std::atomic<bool> explorerReady_{false};
    std::atomic<bool> explorerCancel_{false};
    bool explorerDirty_ = true;
    bool explorerMineOnly_ = false;
    bool wantExplorerTab_ = false;
    std::string explorerNames_;  // the player names the index was built with
    std::optional<core::Move> explorerMove_;  // clicked in the explorer, played after drawing
};

}  // namespace ui

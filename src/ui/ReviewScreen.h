#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/GameAnalyzer.h"
#include "app/PuzzleGenerator.h"
#include "app/RichPresence.h"
#include "app/Settings.h"
#include "chess/Game.h"
#include "chess/GameTree.h"
#include "chess/OpeningBook.h"
#include "chess/Puzzle.h"
#include "chess/Review.h"
#include "storage/GameStore.h"
#include "storage/ReviewStore.h"
#include "ui/BoardView.h"
#include "ui/MoveTreeView.h"

namespace app {
class Assets;
}

namespace ui {

// The Game Review section: the engine checks every move of a finished game, then the
// screen shows accuracy, move classifications, the key moments and what should have
// been played instead. Reviews of library games are stored and feed the statistics.
class ReviewScreen {
public:
    struct AnalysisRequest {
        core::GameRecord record;
        std::optional<storage::GameId> libraryId;
    };

    // The stores may be null when the database could not be opened.
    ReviewScreen(app::Settings& settings, app::Assets& assets, storage::GameStore* games, storage::ReviewStore* reviews,
                 const core::OpeningBook* book);

    // Advances the engine work. Call once per frame, also while another section is shown.
    void update();
    // Draws the section into the current ImGui window.
    void draw();

    // Starts reviewing a game. `libraryId` is the library entry it came from, if any;
    // only library games are stored and counted in the statistics.
    void load(core::GameRecord record, std::optional<storage::GameId> libraryId = std::nullopt);
    // Reviews every library game that has no review yet, one after the other.
    void reviewAll();
    // Reloads the game list on the next frame (call after the library changed).
    void markDirty() { listDirty_ = true; }

    // The game the user asked to open in Analysis, if any (cleared by the call).
    std::optional<AnalysisRequest> takeAnalysisRequest();
    // Puzzles made from the game under review, once the search for them has finished
    // (cleared by the call). The caller stores them and reports back with onPuzzlesStored().
    std::optional<std::vector<core::Puzzle>> takeGeneratedPuzzles();
    void onPuzzlesStored(int found, int added);
    // Test aid: makes puzzles from the game as soon as its review is complete.
    void requestPuzzles() { wantPuzzles_ = true; }
    // True once after a review was stored.
    bool consumeReviewsChanged();
    // Two short lines for Discord Rich Presence.
    app::PresenceText presence() const;
    // Test aid: jumps to the given ply of the game under review.
    void goToPly(int ply);

private:
    void reloadList();
    void reviewFromLibrary(storage::GameId id);
    void finishReview();
    void startPuzzles();
    void closeGame();
    // Shows a position; `sound` plays the sound of the move that led there.
    void goTo(core::NodeId id, bool sound = true);
    void refreshCurrent();
    void stepMoment(int direction);
    core::MoveQuality qualityOf(core::NodeId id) const;
    std::string moveLabel(core::NodeId id) const;

    void drawList();
    void drawGame();
    void drawSummary();
    void drawExplanation();
    void drawMoments(float height);
    void drawPositionNotes(float height);
    void drawButtons();
    void handleShortcuts();

    app::Settings& settings_;
    app::Assets& assets_;
    storage::GameStore* games_;
    storage::ReviewStore* reviews_;
    const core::OpeningBook* book_;

    // The game under review.
    std::optional<core::GameRecord> record_;
    std::optional<storage::GameId> libraryId_;
    core::NodeId current_ = core::kRootNode;
    core::Game position_;
    core::GameReview review_;
    std::string opening_;      // "C50 Italian Game", for display
    std::string openingName_;  // "Italian Game", stored with the review
    std::string bestSan_;      // the engine's move in the position before the current move
    std::string explanation_;  // sentences about the current move
    std::optional<core::Move> bestMove_;
    BoardView board_;
    MoveTreeView treeView_;
    bool flipped_ = false;
    std::string notice_;

    app::GameAnalyzer analyzer_;
    std::size_t lastDone_ = 0;

    // Puzzles from this game.
    app::PuzzleGenerator generator_;
    std::optional<std::vector<core::Puzzle>> generated_;
    bool wantPuzzles_ = false;

    // Reviewing several games in a row.
    std::vector<storage::GameId> pending_;
    bool batchActive_ = false;
    int batchReviewed_ = 0;

    // The list of library games.
    std::vector<storage::GameSummary> list_;
    std::unordered_map<storage::GameId, storage::ReviewAccuracy> accuracies_;
    int unreviewed_ = 0;
    bool listDirty_ = true;
    storage::GameId selected_ = 0;
    std::string listNotice_;

    std::optional<AnalysisRequest> analysisRequest_;
    bool reviewsChanged_ = false;
};

}  // namespace ui

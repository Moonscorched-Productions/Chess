#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <random>
#include <string>

#include "app/RichPresence.h"
#include "app/Settings.h"
#include "chess/Endgames.h"
#include "chess/Game.h"
#include "chess/GameTree.h"
#include "engine/UciEngine.h"
#include "ui/BoardView.h"

namespace storage {
class GameStore;
}

namespace app {
class Assets;
}

namespace ui {

// The Play section: a game against the engine (or two players on one board).
class PlayScreen {
public:
    // `store` may be null; finished games are then not saved.
    PlayScreen(app::Settings& settings, app::Assets& assets, storage::GameStore* store);

    // Processes engine output and plays engine moves. Call once per frame,
    // whether or not the Play section is visible.
    void update();

    // Draws the section into the current ImGui window.
    void draw();

    // Call after settings were edited elsewhere.
    void onSettingsChanged(bool enginePathChanged);

    // True once after this screen changed the settings (so they can be saved).
    bool consumeSettingsChanged();

    // Test aid: the human side plays random legal moves.
    void setAutoplay(bool enabled) { autoplay_ = enabled; }

    // Two short lines describing the current game, for Discord Rich Presence.
    app::PresenceText presence() const;

    // The game the user asked to look at in Analysis, if any (cleared by the call).
    std::optional<core::GameRecord> takeAnalysisRequest();
    // The finished game the user asked to review, with its library entry if it was stored.
    struct ReviewRequest {
        core::GameRecord record;
        std::optional<std::int64_t> libraryId;
    };
    std::optional<ReviewRequest> takeReviewRequest();
    // True once after a game was written to the library.
    bool consumeLibraryChanged();

    // Starts an endgame training position against the engine at full strength.
    void startChallenge(const core::EndgamePosition& position);
    struct ChallengeResult {
        std::string id;
        bool success = false;
    };
    // The outcome of a finished training position, if any (cleared by the call).
    std::optional<ChallengeResult> takeChallengeResult();

    // Opens the "New game" dialog on the next frame.
    void openNewGameDialog();

private:
    using Clock = std::chrono::steady_clock;

    void startEngine();
    void applyEngineOptions();
    void requestEngineMove();
    void cancelEngineMove();

    void startNewGame(bool againstEngine, app::ColorChoice playAs, const std::string& fen);
    void playMove(core::Move move);
    void playSoundForLastMove();
    void undo();
    void redo();
    void offerDraw();
    core::GameRecord currentRecord() const;
    void saveFinishedGame();
    void checkChallenge();

    bool engineToMove() const;
    bool humanCanMove() const;
    std::string engineLabel() const;
    std::string statusText() const;

    void drawPlayerBar(core::Color color, float width);
    void drawSidePanel(float width, float height);
    void drawMoveList(float height);
    void drawNewGameDialog();
    void drawResignDialog();
    void handleShortcuts();

    app::Settings& settings_;
    app::Assets& assets_;

    core::Game game_;
    BoardView board_;
    bool flipped_ = false;

    // Current game setup.
    bool againstEngine_ = true;
    core::Color humanColor_ = core::Color::White;

    // Engine.
    engine::UciEngine engine_;
    std::string engineError_;
    int pendingSearch_ = 0;                  // id of the search we are waiting for, 0 = none
    std::optional<std::string> pendingMove_;  // engine move waiting for the minimum delay
    Clock::time_point moveNotBefore_{};
    std::optional<int> engineScoreCp_;  // last evaluation, from the engine's point of view

    // Transient UI state.
    std::string notice_;  // e.g. the answer to a draw offer
    int lastListedPly_ = -1;
    bool wantNewGameDialog_ = false;
    bool wantResignDialog_ = false;
    bool settingsChanged_ = false;

    // Library.
    storage::GameStore* store_ = nullptr;
    std::optional<std::int64_t> savedGameId_;  // library entry of the current game
    core::Result savedResult_ = core::Result::None;
    int savedPly_ = -1;
    bool libraryChanged_ = false;

    // Endgame training.
    struct Challenge {
        std::string id;
        std::string title;
        core::EndgameGoal goal = core::EndgameGoal::Win;
    };
    std::optional<Challenge> challenge_;
    bool challengeReported_ = false;
    std::optional<ChallengeResult> challengeResult_;
    std::optional<core::GameRecord> analysisRequest_;
    std::optional<ReviewRequest> reviewRequest_;

    // New game dialog draft.
    struct Draft {
        bool againstEngine = true;
        app::ColorChoice playAs = app::ColorChoice::White;
        app::StrengthMode strengthMode = app::StrengthMode::Level;
        int level = 3;
        int elo = 1500;
        int thinkTimeMs = 1000;
        int depthLimit = 0;
        bool customPosition = false;
        std::array<char, 128> fen{};
        std::string error;
    } draft_;

    bool autoplay_ = false;
    Clock::time_point autoplayAt_{};
    std::mt19937 rng_{std::random_device{}()};
};

}  // namespace ui

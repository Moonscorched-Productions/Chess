#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "app/GameAnalyzer.h"
#include "app/RichPresence.h"
#include "app/Settings.h"
#include "chess/Endgames.h"
#include "chess/Game.h"
#include "chess/GameTree.h"
#include "chess/Review.h"
#include "engine/UciEngine.h"
#include "ui/BoardView.h"

namespace storage {
class GameStore;
class TrainingStore;
}

namespace core {
class OpeningBook;
struct OpeningLine;
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

    // Hints: the first request marks the piece to move, the second shows the move.
    void requestHint();

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

    // Where the New game dialog finds openings to practise. Either may be null.
    void setOpeningSources(const core::OpeningBook* book, storage::TrainingStore* repertoires);
    // Test aid: starts a game practising the first opening whose name contains `search`.
    bool practiseOpening(std::string_view search);

private:
    using Clock = std::chrono::steady_clock;

    void startEngine();
    void applyEngineOptions();
    void requestEngineMove();
    void cancelEngineMove();

    void clearHint();
    void updateHint();
    std::string hintButtonLabel() const;

    void startNewGame(bool againstEngine, app::ColorChoice playAs, const std::string& fen);
    void playMove(core::Move move);
    void playSoundForLastMove();
    void afterMove();  // clocks and sound after any move
    void undo();
    void redo();
    void markDraw();
    core::GameRecord currentRecord() const;
    void saveFinishedGame();
    void checkChallenge();
    void updatePlayTime();

    // Move badges: keeps badgeTree_ in step with the game and the analyzer working on it.
    void syncBadges();
    // Quality of the move at `index` of the game's history (None while it is not judged yet).
    core::MoveQuality badgeQuality(std::size_t index) const;
    // The same, whether or not the badges are shown (the coach uses it).
    core::MoveQuality judgedQuality(std::size_t index) const;

    // Clocks.
    bool timed() const { return clockBaseMs_ > 0.0; }
    void resetClocks(int baseSeconds, int incrementSeconds);
    void updateClocks(bool paused);
    void recordClocks();   // after a move: increment, and remember the times for undo
    void restoreClocks();  // after undo/redo
    engine::SearchLimits clockLimits() const;
    std::string timeControlText() const;  // PGN TimeControl, e.g. "300+3"
    void drawClock(core::Color color, float right);

    // Coach: true while the engine's reply waits for the verdict on the user's move,
    // or for the user to decide after a mistake.
    bool coachHolds();
    void drawCoach();

    // Opening practice.
    core::NodeId bookNode() const;  // the book position of the game, or kNoNode once it left the book
    std::optional<std::string> bookMove();
    void checkBookAfterUserMove();
    void drawOpeningChoice();

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

    // Hints come from a separate engine at full strength, started on the first request:
    // the opponent engine may be playing at a reduced level.
    engine::UciEngine hintEngine_;
    std::string hintError_;
    int hintLevel_ = 0;                 // 0 none, 1 piece, 2 move
    std::string hintFen_;               // the position the hint is for
    int hintSearch_ = 0;                // id of the running hint search, 0 = none
    std::optional<core::Move> hintMove_;

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
        int clockBaseSeconds = 0;
        int clockIncrementSeconds = 0;
        // Opening practice: 0 none, 1 a named opening, 2 one of the user's repertoires.
        int openingMode = 0;
        int openingIndex = -1;          // into OpeningBook::lines()
        std::int64_t repertoireId = 0;  // storage::RepertoireId
        std::array<char, 64> openingFilter{};
        std::vector<std::pair<std::int64_t, std::string>> repertoires;  // id, name
        std::vector<core::Color> repertoireColors;
        bool customPosition = false;
        std::array<char, 128> fen{};
        std::string error;
    } draft_;

    // Move badges while playing: the game as a tree that a background engine evaluates.
    core::GameTree badgeTree_;
    std::vector<core::NodeId> badgeLine_;  // badgeTree_ node for each move of the history
    app::GameAnalyzer badgeAnalyzer_;
    bool badgesOn_ = false;

    // Clocks, in milliseconds; index 0 is White. clockAtPly_[n] = the times after n moves.
    double clockBaseMs_ = 0.0;
    double clockIncrementMs_ = 0.0;
    std::array<double, 2> remainingMs_{};
    std::vector<std::array<double, 2>> clockAtPly_;
    Clock::time_point clockTick_{};
    bool clocksPaused_ = false;  // the coach is talking

    enum class CoachState { Checking, Warned, Cleared };
    struct Coach {
        core::NodeId node = core::kNoNode;  // badgeTree_ node of the move being judged
        CoachState state = CoachState::Cleared;
        Clock::time_point since{};
        core::MoveQuality quality = core::MoveQuality::None;
        std::optional<core::Move> better;
        std::string betterSan;
    } coach_;

    // The opening being practised: the engine plays from it while the game follows it.
    struct Book {
        std::string name;
        core::GameTree tree;
        bool fromRepertoire = false;
        bool left = false;  // the game has left it
        std::string note;   // what happened when it did
    };
    std::optional<Book> book_;
    static Book bookFromLine(const core::OpeningLine& line);
    const core::OpeningBook* openingBook_ = nullptr;
    storage::TrainingStore* repertoires_ = nullptr;

    // Time spent on the current game (counted from the first move until it ends).
    double playSeconds_ = 0.0;
    Clock::time_point lastTick_{};

    bool autoplay_ = false;
    Clock::time_point autoplayAt_{};
    std::mt19937 rng_{std::random_device{}()};
};

}  // namespace ui

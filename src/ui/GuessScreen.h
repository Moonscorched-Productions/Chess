#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/RichPresence.h"
#include "app/Settings.h"
#include "chess/Game.h"
#include "chess/GameTree.h"
#include "engine/UciEngine.h"
#include "storage/GameStore.h"
#include "ui/BoardView.h"

namespace app {
class Assets;
}

namespace ui {

// The Guess the Move section: play through a strong game from one side and try to find
// each move. The move from the game earns full points; any other move earns points by how
// close the engine rates it to the game move.
class GuessScreen {
public:
    // `games` may be null. `classicsFile` is a PGN file of bundled games.
    GuessScreen(app::Settings& settings, app::Assets& assets, storage::GameStore* games,
                const std::filesystem::path& classicsFile);

    // Advances the engine work and the opponent's moves. Call once per frame while shown.
    void update();
    void draw();
    // Reloads the library list on the next frame (call after the library changed).
    void markDirty() { listDirty_ = true; }
    app::PresenceText presence() const;

    // Test aids: starts a bundled game, guessing the given side's moves, and guesses randomly.
    void startClassic(std::size_t index, core::Color side);
    void setAutoGuess(bool enabled) { autoGuess_ = enabled; }

private:
    using Clock = std::chrono::steady_clock;
    enum class Phase { Choosing, Guessing, Judging, Finished };

    struct Choice {
        std::string title;   // "Morphy - Duke Karl / Count Isouard"
        std::string detail;  // "1858  ·  The Opera Game"
        std::optional<storage::GameId> libraryId;
        std::size_t classic = 0;  // index into classics_ when libraryId is empty
    };
    struct Attempt {
        std::string label;  // "12. Nf3"
        std::string guessSan;
        std::string gameSan;
        int points = 0;
        bool judged = true;  // false: no engine verdict (only the game move scores)
    };

    void reloadList();
    void start(core::GameRecord record, core::Color side, std::string title);
    void onGuess(core::Move move);
    void advance();  // plays the game's next move
    void finishJudging();
    void startNextSearch();
    bool guessersTurn() const;
    int maxPoints() const;

    void drawChooser();
    void drawGame();
    void drawPanel(float width, float height);

    app::Settings& settings_;
    app::Assets& assets_;
    storage::GameStore* games_;

    // Choosing a game.
    std::vector<core::GameRecord> classics_;
    std::vector<Choice> choices_;
    int selected_ = -1;
    int sideChoice_ = 0;  // 0 = the winner (White if drawn), 1 = White, 2 = Black
    bool listDirty_ = true;

    // The game being guessed.
    Phase phase_ = Phase::Choosing;
    core::GameRecord record_;
    std::string title_;
    std::vector<core::NodeId> line_;
    std::size_t index_ = 0;  // the next move of line_ to be played
    core::Game position_;    // the position before line_[index_]
    core::Color side_ = core::Color::White;
    BoardView board_;
    Clock::time_point opponentAt_{};
    std::vector<Attempt> attempts_;
    int points_ = 0;
    std::string feedback_;
    std::optional<core::Move> lastGuess_;  // shown as an arrow next to the game move

    // Judging a guess that differs from the game: two searches, after each move.
    engine::UciEngine engine_;
    std::string engineError_;
    std::vector<std::string> jobs_;           // UCI move lists from the start position
    std::vector<std::optional<core::Eval>> results_;
    std::size_t job_ = 0;
    int searchId_ = 0;
    int searchDepth_ = 0;
    std::optional<core::Move> pendingGuess_;
    std::string pendingGuessSan_;

    bool autoGuess_ = false;
    Clock::time_point autoGuessAt_{};
};

}  // namespace ui

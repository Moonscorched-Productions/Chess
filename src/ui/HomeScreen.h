#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "app/Settings.h"
#include "chess/Statistics.h"
#include "storage/GameStore.h"
#include "storage/PuzzleStore.h"

namespace app {
class Assets;
}

namespace storage {
class TrainingStore;
}

namespace ui {

// The home page, opened from the "Chess" title in the sidebar: a greeting, the streak,
// how the last games went, what is due today and a tip, with shortcuts into the sections.
class HomeScreen {
public:
    // Any store may be null.
    HomeScreen(app::Settings& settings, app::Assets& assets, storage::GameStore* games, storage::PuzzleStore* puzzles,
               storage::TrainingStore* training);

    // Call when the page is opened: the numbers are read again.
    void onShow() { dirty_ = true; }
    void draw();

    enum class Action { None, NewGame, DailyPuzzle, ReviewPuzzles, Openings, Training, Guess, Statistics };
    Action takeAction();
    // True once after the page changed the settings (the user's name).
    bool consumeSettingsChanged();

private:
    void reload();
    void drawHeader();
    void drawTiles();
    void drawActions(float width, float height);
    void drawWeek(float width, float height);
    void drawFooter();

    app::Settings& settings_;
    app::Assets& assets_;
    storage::GameStore* games_;
    storage::PuzzleStore* puzzles_;
    storage::TrainingStore* training_;

    bool dirty_ = true;
    storage::Usage usage_;
    core::Streaks streaks_;
    std::array<std::int64_t, 7> week_{};  // seconds per day, oldest first, today last
    storage::PuzzleStats puzzleStats_;
    storage::ReviewQueue puzzleQueue_;
    int openingsDue_ = 0;
    int gamesPlayed_ = 0;
    std::string recentResults_;  // "WWDLW", oldest first, the user's last games in Play

    std::array<char, 41> nameInput_{};
    Action action_ = Action::None;
    bool settingsChanged_ = false;
};

}  // namespace ui

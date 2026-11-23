#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "app/Settings.h"
#include "chess/Statistics.h"
#include "storage/GameStore.h"
#include "storage/ReviewStore.h"

namespace ui {

// The Statistics section: what the reviewed games say about the user's play.
class StatisticsScreen {
public:
    // The stores may be null when the database could not be opened.
    StatisticsScreen(app::Settings& settings, storage::ReviewStore* reviews, storage::GameStore* games = nullptr);

    // Draws the section into the current ImGui window.
    void draw();
    // Recomputes the numbers on the next frame (call after a review was stored or a game deleted).
    void markDirty() { dirty_ = true; }

    // True once after this screen changed the settings (so they can be saved).
    bool consumeSettingsChanged();
    // True once after the user asked to go to Game Review.
    bool consumeReviewRequest();

private:
    void reload();
    void drawTiles();
    void drawTrend(ImVec2 size);
    void drawPhases();
    void drawQualities();
    void drawOpenings();
    void drawNames();
    void drawActivity();

    app::Settings& settings_;
    storage::ReviewStore* reviews_;
    storage::GameStore* gameStore_;
    storage::Usage usage_;
    std::unordered_map<int, std::int64_t> secondsByDay_;  // day number -> seconds in the app
    core::Streaks streaks_;
    std::vector<core::PlayerGame> games_;
    core::PlayerStats stats_;
    bool dirty_ = true;
    std::array<char, 256> names_{};
    bool settingsChanged_ = false;
    bool reviewRequest_ = false;
};

}  // namespace ui

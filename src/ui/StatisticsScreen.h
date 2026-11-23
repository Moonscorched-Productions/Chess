#pragma once

#include <array>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/Settings.h"
#include "chess/Statistics.h"
#include "storage/ReviewStore.h"

namespace ui {

// The Statistics section: what the reviewed games say about the user's play.
class StatisticsScreen {
public:
    // `reviews` may be null when the database could not be opened.
    StatisticsScreen(app::Settings& settings, storage::ReviewStore* reviews);

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

    app::Settings& settings_;
    storage::ReviewStore* reviews_;
    std::vector<core::PlayerGame> games_;
    core::PlayerStats stats_;
    bool dirty_ = true;
    std::array<char, 256> names_{};
    bool settingsChanged_ = false;
    bool reviewRequest_ = false;
};

}  // namespace ui

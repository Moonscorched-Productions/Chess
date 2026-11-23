#pragma once

#include <array>
#include <string>

#include "app/RichPresence.h"
#include "app/Settings.h"

namespace ui {

// The Settings section. Edits the settings in place and reports what changed.
class SettingsScreen {
public:
    struct Changes {
        bool any = false;         // something changed and should be saved
        bool enginePath = false;  // the engine has to be restarted
    };

    SettingsScreen(app::Settings& settings, const app::RichPresence& presence);

    // Draws the section into the current ImGui window.
    Changes draw();

private:
    app::Settings& settings_;
    const app::RichPresence& presence_;
    std::array<char, 512> enginePath_{};
    std::string detectedEngine_;
};

}  // namespace ui

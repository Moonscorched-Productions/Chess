#pragma once

#include <array>
#include <string>

#include "app/Assets.h"
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

    SettingsScreen(app::Settings& settings, app::Assets& assets, const app::RichPresence& presence);

    // Draws the section into the current ImGui window.
    Changes draw();

private:
    // One "Move sound  [Choose...] [Play] [Reset]  file" row. Returns true when the file changed.
    bool soundRow(const char* label, app::SoundId id, std::string& file);

    app::Settings& settings_;
    app::Assets& assets_;
    const app::RichPresence& presence_;
    std::string soundError_;  // why the last chosen sound file was refused
    std::array<char, 512> enginePath_{};
    std::array<char, 512> engineOptions_{};
    std::array<char, 512> syzygyPath_{};
    std::array<char, 512> comparePath_{};
    std::array<char, 512> compareOptions_{};
    std::array<char, 512> displayName_{};
    bool nameEditing_ = false;
    std::string detectedEngine_;
};

}  // namespace ui

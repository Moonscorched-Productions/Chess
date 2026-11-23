#include "ui/SettingsScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "app/Paths.h"
#include "app/RichPresence.h"

namespace ui {

namespace {

constexpr float kLabelWidth = 190.0f;
constexpr float kFieldWidth = 360.0f;

// Starts a "label    [field]" row and leaves the cursor at the field.
void row(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(kFieldWidth);
}

void fill(std::array<char, 512>& buffer, const std::string& text) {
    const std::size_t length = std::min(text.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), text.data(), length);
    buffer[length] = '\0';
}

// A text field bound to a string setting. Returns true when an edit was committed.
bool textField(const char* id, const char* hint, std::array<char, 512>& buffer, std::string& target) {
    ImGui::InputTextWithHint(id, hint, buffer.data(), buffer.size());
    if (!ImGui::IsItemDeactivatedAfterEdit() || target == buffer.data()) return false;
    target = buffer.data();
    return true;
}

}  // namespace

SettingsScreen::SettingsScreen(app::Settings& settings, const app::RichPresence& presence)
    : settings_(settings), presence_(presence) {
    const std::size_t length = std::min(settings_.enginePath.size(), enginePath_.size() - 1);
    std::memcpy(enginePath_.data(), settings_.enginePath.data(), length);
    enginePath_[length] = '\0';
    detectedEngine_ = app::pathToUtf8(app::findBundledEngine());
    fill(engineOptions_, settings_.engineOptions);
    fill(syzygyPath_, settings_.syzygyPath);
    fill(comparePath_, settings_.compareEnginePath);
    fill(compareOptions_, settings_.compareEngineOptions);
}

SettingsScreen::Changes SettingsScreen::draw() {
    Changes changes;
    app::Settings& s = settings_;

    ImGui::TextUnformatted("Settings");
    ImGui::Spacing();
    ImGui::BeginChild("##settings", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);

    // --- Engine ---------------------------------------------------------------
    ImGui::SeparatorText("Engine and opponent");

    row("Engine executable");
    ImGui::InputTextWithHint("##enginepath", "Leave empty to use the bundled Stockfish", enginePath_.data(), enginePath_.size());
    if (ImGui::IsItemDeactivatedAfterEdit() && s.enginePath != enginePath_.data()) {
        s.enginePath = enginePath_.data();
        changes.any = changes.enginePath = true;
    }
    if (s.enginePath.empty()) {
        ImGui::SetCursorPosX(kLabelWidth);
        if (detectedEngine_.empty()) {
            ImGui::TextDisabled("No bundled engine found in the engines folder.");
        } else {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Using %s", detectedEngine_.c_str());
            ImGui::PopTextWrapPos();
        }
    }

    row("Engine options");
    changes.any |= textField("##engineoptions", "Optional, e.g. Move Overhead=50; Ponder=false", engineOptions_, s.engineOptions);
    ImGui::SetItemTooltip("Extra UCI options for the engine, written as Name=Value and separated by semicolons");
    row("Endgame tablebases");
    changes.any |= textField("##syzygy", "Folder with Syzygy tablebase files (optional)", syzygyPath_, s.syzygyPath);
    ImGui::SetItemTooltip("With tablebases the engine knows the exact result of positions with few pieces.\n"
                          "Analysis then shows it as \"Tablebase: White wins\", \"draw\" and so on.");

    row("Strength");
    int mode = s.strengthMode == app::StrengthMode::Elo ? 1 : 0;
    if (ImGui::Combo("##strengthmode", &mode, "Level\0Elo rating\0")) {
        s.strengthMode = mode == 1 ? app::StrengthMode::Elo : app::StrengthMode::Level;
        changes.any = true;
    }
    if (s.strengthMode == app::StrengthMode::Level) {
        row("Level");
        char format[48];
        std::snprintf(format, sizeof(format), "%%d  \xC2\xB7  %s", app::levelPreset(s.level).name);
        changes.any |= ImGui::SliderInt("##level", &s.level, 1, 8, format, ImGuiSliderFlags_AlwaysClamp);
    } else {
        row("Elo rating");
        changes.any |= ImGui::SliderInt("##elo", &s.elo, 1320, 3190, "%d", ImGuiSliderFlags_AlwaysClamp);
    }

    row("Thinking time");
    float seconds = static_cast<float>(s.thinkTimeMs) / 1000.0f;
    if (ImGui::SliderFloat("##time", &seconds, 0.1f, 10.0f, "Up to %.1f s per move", ImGuiSliderFlags_AlwaysClamp)) {
        s.thinkTimeMs = static_cast<int>(std::lround(seconds * 10.0f)) * 100;
        changes.any = true;
    }

    row("Search depth");
    changes.any |= ImGui::SliderInt("##depth", &s.depthLimit, 0, 30, s.depthLimit == 0 ? "No limit" : "%d plies",
                                    ImGuiSliderFlags_AlwaysClamp);

    // --- Analysis -------------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Analysis");

    row("Engine lines");
    changes.any |= ImGui::SliderInt("##lines", &s.analysisLines, 1, 5, "%d", ImGuiSliderFlags_AlwaysClamp);
    // Threads and hash restart the engine's search, so they apply when the slider is released.
    row("Threads");
    ImGui::SliderInt("##threads", &s.analysisThreads, 0, 32, s.analysisThreads == 0 ? "Automatic" : "%d",
                     ImGuiSliderFlags_AlwaysClamp);
    changes.any |= ImGui::IsItemDeactivatedAfterEdit();
    row("Hash memory");
    ImGui::SliderInt("##hash", &s.analysisHashMb, 16, 4096, "%d MB", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
    changes.any |= ImGui::IsItemDeactivatedAfterEdit();
    row("Game analysis depth");
    changes.any |= ImGui::SliderInt("##analysisdepth", &s.analysisDepth, 6, 30, "%d plies", ImGuiSliderFlags_AlwaysClamp);
    row("Game analysis time");
    float analysisSeconds = static_cast<float>(s.analysisMoveTimeMs) / 1000.0f;
    if (ImGui::SliderFloat("##analysistime", &analysisSeconds, 0.1f, 10.0f, "Up to %.1f s per move", ImGuiSliderFlags_AlwaysClamp)) {
        s.analysisMoveTimeMs = static_cast<int>(std::lround(analysisSeconds * 10.0f)) * 100;
        changes.any = true;
    }
    row("Best move arrow");
    changes.any |= ImGui::Checkbox("##arrow", &s.showBestMoveArrow);
    row("Comparison engine");
    changes.any |= textField("##comparepath", "Path to a second UCI engine (optional)", comparePath_, s.compareEnginePath);
    ImGui::SetItemTooltip("Analysis shows this engine's evaluation and line next to the main engine's");
    row("Its options");
    changes.any |= textField("##compareoptions", "Optional, e.g. Threads=1; Hash=64", compareOptions_, s.compareEngineOptions);

    // --- Library --------------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Game Library");
    row("Save finished games");
    changes.any |= ImGui::Checkbox("##savegames", &s.saveGamesToLibrary);

    // --- Discord --------------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Discord");
    row("Rich Presence");
    changes.any |= ImGui::Checkbox("##discord", &s.discordPresence);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", !s.discordPresence        ? "Off."
                              : presence_.connected() ? "Connected to Discord."
                                                      : "Waiting for Discord to be running.");

    // --- Appearance -----------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Appearance");
    row("Light mode");
    changes.any |= ImGui::Checkbox("##lightmode", &s.lightMode);
    row("Fullscreen");
    changes.any |= ImGui::Checkbox("##fullscreen", &s.fullscreen);
    ImGui::SameLine();
    ImGui::TextDisabled("F11 switches at any time.");
    row("Blindfold");
    changes.any |= ImGui::Checkbox("##blindfold", &s.blindfold);
    ImGui::SameLine();
    ImGui::TextDisabled("Hides the pieces in Play and Puzzles. B switches, holding Space peeks.");

    // --- Board ----------------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Board");

    const auto& themes = app::boardThemes();
    int themeIndex = static_cast<int>(themes.size());  // "Custom"
    for (std::size_t i = 0; i < themes.size(); ++i) {
        if (themes[i].light == s.lightSquare && themes[i].dark == s.darkSquare) themeIndex = static_cast<int>(i);
    }
    row("Colours");
    const char* preview = themeIndex < static_cast<int>(themes.size()) ? themes[static_cast<std::size_t>(themeIndex)].name : "Custom";
    if (ImGui::BeginCombo("##theme", preview)) {
        for (std::size_t i = 0; i < themes.size(); ++i) {
            if (ImGui::Selectable(themes[i].name, themeIndex == static_cast<int>(i))) {
                s.lightSquare = themes[i].light;
                s.darkSquare = themes[i].dark;
                changes.any = true;
            }
        }
        ImGui::EndCombo();
    }
    const ImGuiColorEditFlags colorFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha;
    row("Light squares");
    changes.any |= ImGui::ColorEdit3("##light", s.lightSquare.data(), colorFlags);
    row("Dark squares");
    changes.any |= ImGui::ColorEdit3("##dark", s.darkSquare.data(), colorFlags);

    row("Coordinates");
    changes.any |= ImGui::Checkbox("##coords", &s.showCoordinates);
    row("Legal move hints");
    changes.any |= ImGui::Checkbox("##hints", &s.showLegalMoves);
    row("Highlight last move");
    changes.any |= ImGui::Checkbox("##lastmove", &s.highlightLastMove);
    row("Animations");
    changes.any |= ImGui::Checkbox("##animations", &s.animations);

    // --- Sound ----------------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Sound");
    row("Sound effects");
    changes.any |= ImGui::Checkbox("##sound", &s.soundEnabled);
    ImGui::BeginDisabled(!s.soundEnabled);
    row("Volume");
    changes.any |= ImGui::SliderInt("##volume", &s.soundVolume, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();

    ImGui::EndChild();

    if (changes.any) s.sanitize();
    return changes;
}

}  // namespace ui

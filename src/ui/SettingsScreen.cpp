#include "ui/SettingsScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "app/Assets.h"
#include "app/FileDialog.h"
#include "app/Theme.h"
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

SettingsScreen::SettingsScreen(app::Settings& settings, app::Assets& assets, const app::RichPresence& presence)
    : settings_(settings), assets_(assets), presence_(presence) {
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
    row("Move badges in Play");
    changes.any |= ImGui::Checkbox("##playbadges", &s.playMoveBadges);
    ImGui::SameLine();
    ImGui::TextDisabled("Judges every move while you play (Best, Mistake, ...).");
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
    row("Your name");
    // The home page may have set it meanwhile.
    if (!nameEditing_ && s.displayName != displayName_.data()) fill(displayName_, s.displayName);
    changes.any |= textField("##displayname", "How the home page greets you", displayName_, s.displayName);
    nameEditing_ = ImGui::IsItemActive();
    row("Theme");
    {
        const auto& themes = app::uiThemes();
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("##uitheme", themes[static_cast<std::size_t>(s.uiTheme)].name, ImGuiComboFlags_HeightLargest)) {
            // Dark themes first, then the light ones.
            for (const bool light : {false, true}) {
                ImGui::SeparatorText(light ? "Light" : "Dark");
                for (std::size_t i = 0; i < themes.size(); ++i) {
                    if (themes[i].light != light) continue;
                    if (ImGui::Selectable(themes[i].name, s.uiTheme == static_cast<int>(i))) {
                        s.uiTheme = static_cast<int>(i);
                        s.lightMode = themes[i].light;
                        // Each theme comes with matching board colours; they can still be changed below.
                        s.lightSquare = themes[i].boardLight;
                        s.darkSquare = themes[i].boardDark;
                        changes.any = true;
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Also sets matching board colours.");
    }
    row("Font");
    {
        int font = s.serifFont ? 0 : 1;
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("##font", &font, "Source Serif\0System font\0")) {
            s.serifFont = font == 0;
            changes.any = true;
        }
    }
    row("Animated background");
    changes.any |= ImGui::Checkbox("##animatedbg", &s.animatedBackground);
    ImGui::SameLine();
    ImGui::TextDisabled("Slow, soft light behind the panels.");
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
    changes.any |= soundRow("Move sound", app::SoundId::Move, s.moveSoundFile);
    changes.any |= soundRow("Capture sound", app::SoundId::Capture, s.captureSoundFile);
    changes.any |= soundRow("Checkmate sound", app::SoundId::GameEnd, s.gameEndSoundFile);
    if (!soundError_.empty()) {
        ImGui::SetCursorPosX(kLabelWidth);
        ImGui::TextDisabled("%s", soundError_.c_str());
    }
    ImGui::EndDisabled();

    // --- About ----------------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("About");
    if (assets_.hasLogo()) {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(64.0f, 64.0f));
        ImGui::GetWindowDrawList()->AddImageRounded(assets_.logoTexture(), pos, ImVec2(pos.x + 64.0f, pos.y + 64.0f), ImVec2(0, 0),
                                                    ImVec2(1, 1), IM_COL32_WHITE, 14.0f);
        ImGui::SameLine(0.0f, 16.0f);
    }
    ImGui::BeginGroup();
    ImGui::PushFont(app::titleFont());
    ImGui::TextUnformatted("Chess");
    ImGui::PopFont();
    ImGui::TextUnformatted("Made by Am\xC3\xA9lie Heinrich, Moonscorched Productions.");
    ImGui::TextDisabled("Free software under the GNU GPL version 3. Engine: Stockfish (GPL 3). Pieces: cburnett (GPL 2+).");
    ImGui::TextDisabled("Opening names and puzzles from Lichess (CC0), sounds by Kenney (CC0), Source Serif 4 by Adobe (OFL).");
    ImGui::TextDisabled("See THIRD_PARTY_NOTICES.md for the full list.");
    ImGui::EndGroup();

    ImGui::EndChild();

    if (changes.any) s.sanitize();
    return changes;
}

bool SettingsScreen::soundRow(const char* label, app::SoundId id, std::string& file) {
    bool changed = false;
    ImGui::PushID(label);
    row(label);
    if (ImGui::Button("Choose...")) {
        if (const auto chosen = app::openSoundDialog()) {
            if (assets_.setSound(id, *chosen)) {
                file = app::pathToUtf8(*chosen);
                soundError_.clear();
                changed = true;
                assets_.play(id, settings_.soundVolume);
            } else {
                soundError_ = "Could not read " + app::pathToUtf8(chosen->filename()) + " as a sound (use ogg, mp3, wav or flac).";
                assets_.setSound(id, app::pathFromUtf8(file));  // back to the sound in use before
            }
        }
    }
    ImGui::SetItemTooltip("Pick your own sound file: ogg, mp3, wav or flac");
    ImGui::SameLine();
    if (ImGui::Button("Play")) assets_.play(id, settings_.soundVolume);
    if (!file.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            file.clear();
            assets_.setSound(id, {});
            soundError_.clear();
            changed = true;
        }
        ImGui::SetItemTooltip("Use the built-in sound again");
    }
    ImGui::SameLine();
    if (file.empty()) {
        ImGui::TextDisabled("Built-in");
    } else {
        ImGui::TextUnformatted(app::pathToUtf8(app::pathFromUtf8(file).filename()).c_str());
        ImGui::SetItemTooltip("%s", file.c_str());
    }
    ImGui::PopID();
    return changed;
}

}  // namespace ui

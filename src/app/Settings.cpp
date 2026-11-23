#include "app/Settings.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace app {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

std::optional<int> toInt(std::string_view text) {
    int value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size()) return std::nullopt;
    return value;
}

void readInt(std::string_view text, int& target) {
    if (const auto value = toInt(text)) target = *value;
}

void readBool(std::string_view text, bool& target) {
    if (text == "1" || text == "true") target = true;
    if (text == "0" || text == "false") target = false;
}

// Colours are stored as "r,g,b" with 0..255 components.
void readColor(std::string_view text, std::array<float, 3>& target) {
    std::array<float, 3> parsed{};
    for (std::size_t i = 0; i < 3; ++i) {
        const std::size_t comma = text.find(',');
        const auto value = toInt(trim(text.substr(0, comma)));
        if (!value || (i < 2 && comma == std::string_view::npos)) return;
        parsed[i] = static_cast<float>(std::clamp(*value, 0, 255)) / 255.0f;
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
    }
    target = parsed;
}

std::string writeColor(const std::array<float, 3>& color) {
    std::ostringstream out;
    for (std::size_t i = 0; i < 3; ++i) {
        if (i > 0) out << ',';
        out << static_cast<int>(std::clamp(color[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    return out.str();
}

}  // namespace

const std::array<BoardTheme, 4>& boardThemes() {
    // Built from 0..255 components so a theme still matches after a save/load round trip.
    const auto rgb = [](int r, int g, int b) {
        return std::array<float, 3>{static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                                    static_cast<float>(b) / 255.0f};
    };
    static const std::array<BoardTheme, 4> themes = {{
        {"Brown", rgb(240, 217, 181), rgb(181, 136, 99)},
        {"Green", rgb(238, 238, 210), rgb(118, 150, 86)},
        {"Blue", rgb(222, 227, 230), rgb(140, 162, 173)},
        {"Grey", rgb(204, 204, 204), rgb(139, 139, 139)},
    }};
    return themes;
}

const LevelPreset& levelPreset(int level) {
    // Skill and depth rise together so the lower levels make human-looking mistakes.
    static const std::array<LevelPreset, 8> presets = {{
        {"Beginner", 0, 2},
        {"Novice", 2, 3},
        {"Casual", 4, 4},
        {"Club", 7, 6},
        {"Intermediate", 10, 8},
        {"Advanced", 14, 11},
        {"Expert", 17, 15},
        {"Master", 20, 0},
    }};
    return presets[static_cast<std::size_t>(std::clamp(level, 1, 8) - 1)];
}

Settings Settings::load(const std::filesystem::path& file) {
    Settings settings;
    std::ifstream in(file);
    if (!in) return settings;

    std::string line;
    while (std::getline(in, line)) {
        const std::string_view view = trim(line);
        if (view.empty() || view.front() == '#') continue;
        const std::size_t equals = view.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key = trim(view.substr(0, equals));
        const std::string_view value = trim(view.substr(equals + 1));

        if (key == "engine_path") {
            settings.enginePath = std::string(value);
        } else if (key == "strength_mode") {
            settings.strengthMode = value == "elo" ? StrengthMode::Elo : StrengthMode::Level;
        } else if (key == "level") {
            readInt(value, settings.level);
        } else if (key == "elo") {
            readInt(value, settings.elo);
        } else if (key == "think_time_ms") {
            readInt(value, settings.thinkTimeMs);
        } else if (key == "depth_limit") {
            readInt(value, settings.depthLimit);
        } else if (key == "analysis_lines") {
            readInt(value, settings.analysisLines);
        } else if (key == "analysis_threads") {
            readInt(value, settings.analysisThreads);
        } else if (key == "analysis_hash_mb") {
            readInt(value, settings.analysisHashMb);
        } else if (key == "analysis_move_time_ms") {
            readInt(value, settings.analysisMoveTimeMs);
        } else if (key == "analysis_depth") {
            readInt(value, settings.analysisDepth);
        } else if (key == "show_best_move_arrow") {
            readBool(value, settings.showBestMoveArrow);
        } else if (key == "discord_presence") {
            readBool(value, settings.discordPresence);
        } else if (key == "save_games_to_library") {
            readBool(value, settings.saveGamesToLibrary);
        } else if (key == "play_as") {
            settings.playAs = value == "black" ? ColorChoice::Black : value == "random" ? ColorChoice::Random : ColorChoice::White;
        } else if (key == "play_against_engine") {
            readBool(value, settings.playAgainstEngine);
        } else if (key == "light_square") {
            readColor(value, settings.lightSquare);
        } else if (key == "dark_square") {
            readColor(value, settings.darkSquare);
        } else if (key == "show_coordinates") {
            readBool(value, settings.showCoordinates);
        } else if (key == "show_legal_moves") {
            readBool(value, settings.showLegalMoves);
        } else if (key == "highlight_last_move") {
            readBool(value, settings.highlightLastMove);
        } else if (key == "sound_enabled") {
            readBool(value, settings.soundEnabled);
        } else if (key == "sound_volume") {
            readInt(value, settings.soundVolume);
        }
    }
    settings.sanitize();
    return settings;
}

bool Settings::save(const std::filesystem::path& file) const {
    std::ofstream out(file, std::ios::trunc);
    if (!out) return false;

    out << "# Chess settings\n";
    out << "engine_path=" << enginePath << '\n';
    out << "strength_mode=" << (strengthMode == StrengthMode::Elo ? "elo" : "level") << '\n';
    out << "level=" << level << '\n';
    out << "elo=" << elo << '\n';
    out << "think_time_ms=" << thinkTimeMs << '\n';
    out << "depth_limit=" << depthLimit << '\n';
    out << "analysis_lines=" << analysisLines << '\n';
    out << "analysis_threads=" << analysisThreads << '\n';
    out << "analysis_hash_mb=" << analysisHashMb << '\n';
    out << "analysis_depth=" << analysisDepth << '\n';
    out << "analysis_move_time_ms=" << analysisMoveTimeMs << '\n';
    out << "show_best_move_arrow=" << (showBestMoveArrow ? 1 : 0) << '\n';
    out << "save_games_to_library=" << (saveGamesToLibrary ? 1 : 0) << '\n';
    out << "discord_presence=" << (discordPresence ? 1 : 0) << '\n';
    out << "play_as=" << (playAs == ColorChoice::Black ? "black" : playAs == ColorChoice::Random ? "random" : "white") << '\n';
    out << "play_against_engine=" << (playAgainstEngine ? 1 : 0) << '\n';
    out << "light_square=" << writeColor(lightSquare) << '\n';
    out << "dark_square=" << writeColor(darkSquare) << '\n';
    out << "show_coordinates=" << (showCoordinates ? 1 : 0) << '\n';
    out << "show_legal_moves=" << (showLegalMoves ? 1 : 0) << '\n';
    out << "highlight_last_move=" << (highlightLastMove ? 1 : 0) << '\n';
    out << "sound_enabled=" << (soundEnabled ? 1 : 0) << '\n';
    out << "sound_volume=" << soundVolume << '\n';
    return static_cast<bool>(out.flush());
}

void Settings::sanitize() {
    level = std::clamp(level, 1, 8);
    elo = std::clamp(elo, 1320, 3190);
    thinkTimeMs = std::clamp(thinkTimeMs, 100, 30000);
    depthLimit = std::clamp(depthLimit, 0, 40);
    soundVolume = std::clamp(soundVolume, 0, 100);
    analysisLines = std::clamp(analysisLines, 1, 5);
    analysisThreads = std::clamp(analysisThreads, 0, 64);
    analysisHashMb = std::clamp(analysisHashMb, 16, 8192);
    analysisDepth = std::clamp(analysisDepth, 6, 30);
    analysisMoveTimeMs = std::clamp(analysisMoveTimeMs, 100, 30000);
    for (auto* color : {&lightSquare, &darkSquare})
        for (float& component : *color) component = std::clamp(component, 0.0f, 1.0f);
}

}  // namespace app

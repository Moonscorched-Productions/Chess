#include "ui/StatisticsScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include <imgui.h>

#include "app/Theme.h"
#include "ui/Common.h"
#include "ui/QualityBadge.h"

namespace ui {

namespace {

using core::MoveQuality;

constexpr MoveQuality kQualities[] = {MoveQuality::Best,       MoveQuality::Excellent, MoveQuality::Good,
                                      MoveQuality::Inaccuracy, MoveQuality::Mistake,   MoveQuality::Blunder};
constexpr float kTileHeight = 104.0f;

// A panel with a caption, one large value and a line of detail.
void tile(const char* id, float width, const char* caption, const char* value, const char* detail) {
    ImGui::BeginChild(id, ImVec2(width, kTileHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("%s", caption);
    ImGui::PushFont(app::headlineFont());
    ImGui::TextUnformatted(value);
    ImGui::PopFont();
    ImGui::TextDisabled("%s", detail);
    ImGui::EndChild();
}

// A horizontal bar filled to `fraction`, drawn as an item of the given size.
void bar(ImVec2 size, float fraction, ImU32 color) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    drawList->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
    const float filled = size.x * std::clamp(fraction, 0.0f, 1.0f);
    if (filled >= 1.0f) drawList->AddRectFilled(origin, ImVec2(origin.x + filled, end.y), color, 3.0f);
}

}  // namespace

StatisticsScreen::StatisticsScreen(app::Settings& settings, storage::ReviewStore* reviews, storage::GameStore* games)
    : settings_(settings), reviews_(reviews), gameStore_(games) {
    const std::size_t length = std::min(settings_.playerNames.size(), names_.size() - 1);
    std::memcpy(names_.data(), settings_.playerNames.data(), length);
    names_[length] = '\0';
}

bool StatisticsScreen::consumeSettingsChanged() { return std::exchange(settingsChanged_, false); }
bool StatisticsScreen::consumeReviewRequest() { return std::exchange(reviewRequest_, false); }

void StatisticsScreen::reload() {
    dirty_ = false;
    games_.clear();
    if (reviews_ != nullptr) games_ = reviews_->playerGames(settings_.playerNames);
    stats_ = core::summarize(games_);
    playTime_ = gameStore_ != nullptr ? gameStore_->playTime() : storage::PlayTime{};
}

namespace {
// "3 h 12 min", "12 min", "40 s".
std::string formatPlayTime(std::int64_t seconds) {
    char text[48];
    if (seconds >= 3600) {
        std::snprintf(text, sizeof(text), "%lld h %lld min", static_cast<long long>(seconds / 3600),
                      static_cast<long long>(seconds / 60 % 60));
    } else if (seconds >= 60) {
        std::snprintf(text, sizeof(text), "%lld min", static_cast<long long>(seconds / 60));
    } else {
        std::snprintf(text, sizeof(text), "%lld s", static_cast<long long>(seconds));
    }
    return text;
}
}  // namespace

void StatisticsScreen::draw() {
    ImGui::TextUnformatted("Statistics");
    if (reviews_ == nullptr) {
        ImGui::TextColored(kErrorColor, "The game database is not available.");
        return;
    }
    if (dirty_) reload();

    ImGui::SameLine();
    ImGui::TextDisabled("%d reviewed %s", stats_.games, stats_.games == 1 ? "game" : "games");
    ImGui::Spacing();

    ImGui::BeginChild("##statistics", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (stats_.games == 0) {
        ImGui::PushTextWrapPos(std::min(ImGui::GetContentRegionAvail().x, 720.0f));
        if (playTime_.games > 0) {
            ImGui::Text("Time played: %s over %d %s", formatPlayTime(playTime_.seconds).c_str(), playTime_.games,
                        playTime_.games == 1 ? "game" : "games");
            ImGui::Spacing();
        }
        ImGui::TextUnformatted("No reviewed games of yours yet.");
        ImGui::TextDisabled("Statistics are built from the games you review in Game Review. Games you play here "
                            "count automatically; for imported games, enter the names you play under below.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Go to Game Review")) reviewRequest_ = true;
        ImGui::Spacing();
        drawNames();
        ImGui::EndChild();
        return;
    }

    drawTiles();
    ImGui::Spacing();

    ImGui::BeginChild("##trend", ImVec2(0.0f, 190.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted("Accuracy by game");
    ImGui::SameLine();
    ImGui::TextDisabled("oldest to newest");
    drawTrend(ImGui::GetContentRegionAvail());
    ImGui::EndChild();
    ImGui::Spacing();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    const float rowsHeight = ImGui::GetTextLineHeightWithSpacing() * 8.0f + 2.0f * style.WindowPadding.y;
    ImGui::BeginChild("##phases", ImVec2(half, rowsHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    drawPhases();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##qualities", ImVec2(half, rowsHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    drawQualities();
    ImGui::EndChild();
    ImGui::Spacing();

    drawOpenings();
    ImGui::Spacing();
    drawNames();
    ImGui::EndChild();
}

void StatisticsScreen::drawTiles() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = (ImGui::GetContentRegionAvail().x - 4.0f * style.ItemSpacing.x) / 5.0f;
    char value[32];
    char detail[96];

    std::snprintf(value, sizeof(value), "%d", stats_.games);
    std::snprintf(detail, sizeof(detail), "%d won  \xC2\xB7  %d drawn  \xC2\xB7  %d lost", stats_.wins, stats_.draws, stats_.losses);
    tile("##tile-games", width, "Games", value, detail);
    ImGui::SameLine();

    std::snprintf(value, sizeof(value), "%.1f", stats_.accuracy);
    std::snprintf(detail, sizeof(detail), "average over %d moves", stats_.moves);
    tile("##tile-accuracy", width, "Accuracy", value, detail);

    const auto colorTile = [&](const char* id, const char* caption, const core::ColorStats& color) {
        ImGui::SameLine();
        if (color.games == 0) {
            tile(id, width, caption, "-", "no games");
            return;
        }
        std::snprintf(value, sizeof(value), "%.1f", color.accuracy);
        if (color.scoredGames > 0) {
            std::snprintf(detail, sizeof(detail), "%d %s  \xC2\xB7  %.0f%% of the points", color.games,
                          color.games == 1 ? "game" : "games", color.scorePercent);
        } else {
            std::snprintf(detail, sizeof(detail), "%d %s", color.games, color.games == 1 ? "game" : "games");
        }
        tile(id, width, caption, value, detail);
    };
    colorTile("##tile-white", "Accuracy as White", stats_.white);
    colorTile("##tile-black", "Accuracy as Black", stats_.black);

    ImGui::SameLine();
    if (playTime_.games == 0) {
        tile("##tile-time", width, "Time played", "-", "no timed games yet");
    } else {
        const std::string total = formatPlayTime(playTime_.seconds);
        std::snprintf(detail, sizeof(detail), "%d %s  \xC2\xB7  %s per game", playTime_.games,
                      playTime_.games == 1 ? "game" : "games", formatPlayTime(playTime_.seconds / playTime_.games).c_str());
        tile("##tile-time", width, "Time played", total.c_str(), detail);
    }
}

// One point per game on a scale that always includes 100, with the average as a line.
void StatisticsScreen::drawTrend(ImVec2 size) {
    size.y = std::max(size.y, 40.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##trend-plot", size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const std::vector<double>& values = stats_.accuracyTrend;
    if (values.empty()) return;
    const double lowest = *std::min_element(values.begin(), values.end());
    const float low = static_cast<float>(std::max(0.0, std::floor((lowest - 5.0) / 10.0) * 10.0));
    const float high = 100.0f;
    const float labelWidth = ImGui::CalcTextSize("100").x + 8.0f;
    const ImVec2 plotMin(origin.x + labelWidth, origin.y + 6.0f);
    const ImVec2 plotMax(origin.x + size.x - 8.0f, origin.y + size.y - 6.0f);
    const auto yAt = [&](double value) {
        return plotMax.y - (plotMax.y - plotMin.y) * (static_cast<float>(value) - low) / (high - low);
    };
    const auto xAt = [&](std::size_t index) {
        if (values.size() == 1) return (plotMin.x + plotMax.x) * 0.5f;
        return plotMin.x + (plotMax.x - plotMin.x) * static_cast<float>(index) / static_cast<float>(values.size() - 1);
    };

    const ImU32 gridColor = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 labelColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(kAccentColor);
    for (const float mark : {low, (low + high) * 0.5f, high}) {
        const float y = yAt(mark);
        drawList->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y), gridColor, 1.0f);
        char label[8];
        std::snprintf(label, sizeof(label), "%.0f", mark);
        drawList->AddText(ImVec2(origin.x, y - ImGui::GetTextLineHeight() * 0.5f), labelColor, label);
    }
    // The average, dashed.
    const float averageY = yAt(stats_.accuracy);
    for (float x = plotMin.x; x < plotMax.x; x += 10.0f) {
        drawList->AddLine(ImVec2(x, averageY), ImVec2(std::min(x + 5.0f, plotMax.x), averageY), labelColor, 1.0f);
    }

    std::vector<ImVec2> points(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) points[i] = ImVec2(xAt(i), yAt(values[i]));
    if (points.size() > 1) drawList->AddPolyline(points.data(), static_cast<int>(points.size()), lineColor, ImDrawFlags_None, 2.0f);
    if (points.size() <= 60) {
        for (const ImVec2& point : points) drawList->AddCircleFilled(point, 3.5f, lineColor);
    }

    if (!hovered) return;
    // Tooltip for the game nearest to the mouse.
    const float mouseX = ImGui::GetIO().MousePos.x;
    std::size_t nearest = 0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (std::abs(points[i].x - mouseX) < std::abs(points[nearest].x - mouseX)) nearest = i;
    }
    // The trend skips games without judged moves, exactly like this filter.
    std::size_t index = 0;
    for (const core::PlayerGame& game : games_) {
        if (game.review.moves == 0) continue;
        if (index++ != nearest) continue;
        drawList->AddCircle(points[nearest], 6.0f, lineColor, 0, 2.0f);
        ImGui::SetTooltip("%s  %s vs %s\nAccuracy %.1f", game.date.empty() ? "" : game.date.c_str(),
                          game.color == core::Color::White ? "White" : "Black", game.opponent.empty() ? "?" : game.opponent.c_str(),
                          game.review.accuracy);
        break;
    }
}

void StatisticsScreen::drawPhases() {
    ImGui::TextUnformatted("Accuracy by game phase");
    ImGui::Spacing();
    const float lineHeight = ImGui::GetTextLineHeight();
    const float labelWidth = ImGui::GetStyle().WindowPadding.x + ImGui::CalcTextSize("Middlegame").x + 14.0f;
    const float valueWidth = ImGui::CalcTextSize("100.0").x + 10.0f;
    for (int i = 0; i < core::kPhaseCount; ++i) {
        const core::PhaseSummary& phase = stats_.phases[static_cast<std::size_t>(i)];
        const bool weakest = stats_.weakestPhase && static_cast<int>(*stats_.weakestPhase) == i;
        ImGui::TextUnformatted(core::phaseName(static_cast<core::GamePhase>(i)));
        ImGui::SameLine(labelWidth);
        if (phase.moves == 0) {
            ImGui::TextDisabled("no moves yet");
            continue;
        }
        const float barWidth = std::max(ImGui::GetContentRegionAvail().x - valueWidth, 40.0f);
        const ImU32 color = weakest ? qualityColor(MoveQuality::Mistake) : ImGui::ColorConvertFloat4ToU32(kAccentColor);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
        bar(ImVec2(barWidth, lineHeight - 6.0f), static_cast<float>(phase.accuracy / 100.0), color);
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.0f);
        ImGui::Text("%.1f", phase.accuracy);
        ImGui::SetCursorPosX(labelWidth);
        ImGui::TextDisabled("%d %s", phase.moves, phase.moves == 1 ? "move" : "moves");
    }
    if (stats_.weakestPhase) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Most to gain: the %s.", *stats_.weakestPhase == core::GamePhase::Opening      ? "opening"
                                                      : *stats_.weakestPhase == core::GamePhase::Middlegame ? "middlegame"
                                                                                                            : "endgame");
        ImGui::PopTextWrapPos();
    }
}

void StatisticsScreen::drawQualities() {
    ImGui::TextUnformatted("Your moves");
    ImGui::SameLine();
    ImGui::TextDisabled("count and average per game");
    ImGui::Spacing();
    const float lineHeight = ImGui::GetTextLineHeight();
    const float labelWidth = ImGui::GetStyle().WindowPadding.x + lineHeight + 6.0f + ImGui::CalcTextSize("Inaccuracy").x + 14.0f;
    const float valueWidth = ImGui::CalcTextSize("0000   00.0").x + 10.0f;
    for (const MoveQuality quality : kQualities) {
        const int count = stats_.counts[static_cast<std::size_t>(quality)];
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        drawQualityBadge(ImGui::GetWindowDrawList(), ImVec2(pos.x + lineHeight * 0.5f, pos.y + lineHeight * 0.5f),
                         lineHeight * 0.5f, quality);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + lineHeight + 6.0f);
        ImGui::TextUnformatted(core::qualityName(quality));
        ImGui::SameLine(labelWidth);
        const float barWidth = std::max(ImGui::GetContentRegionAvail().x - valueWidth, 40.0f);
        const float share = stats_.moves > 0 ? static_cast<float>(count) / static_cast<float>(stats_.moves) : 0.0f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
        bar(ImVec2(barWidth, lineHeight - 6.0f), share, qualityColor(quality));
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.0f);
        ImGui::Text("%d", count);
        ImGui::SameLine();
        ImGui::TextDisabled("%.1f", static_cast<double>(count) / std::max(stats_.games, 1));
    }
}

void StatisticsScreen::drawOpenings() {
    constexpr std::size_t kShown = 12;
    const std::size_t rows = std::min(stats_.openings.size(), kShown);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(rows) + 2.6f) + 2.0f * style.WindowPadding.y;
    ImGui::BeginChild("##openings", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted("Openings");
    ImGui::SameLine();
    ImGui::TextDisabled("most played first");
    if (ImGui::BeginTable("##opening-table", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Opening", ImGuiTableColumnFlags_WidthStretch, 4.0f);
        ImGui::TableSetupColumn("Games", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Accuracy", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < rows; ++i) {
            const core::OpeningStats& opening = stats_.openings[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(opening.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", opening.games);
            ImGui::TableSetColumnIndex(2);
            if (opening.scoredGames > 0) {
                ImGui::Text("%.0f%%", opening.scorePercent);
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.1f", opening.accuracy);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void StatisticsScreen::drawNames() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Your names in games");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, 360.0f));
    ImGui::InputTextWithHint("##names", "You, your online handle, ...", names_.data(), names_.size());
    if (ImGui::IsItemDeactivatedAfterEdit() && settings_.playerNames != names_.data()) {
        settings_.playerNames = names_.data();
        settings_.sanitize();
        settingsChanged_ = true;
        dirty_ = true;
    }
    ImGui::SetItemTooltip("Separate several names with commas. Games played here use the name \"You\".");
}

}  // namespace ui

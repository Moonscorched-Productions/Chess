#include "ui/HomeScreen.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <utility>

#include <imgui.h>

#include "app/Assets.h"
#include "app/Theme.h"
#include "chess/Repertoire.h"
#include "storage/TrainingStore.h"
#include "ui/Common.h"

namespace ui {

namespace {

constexpr float kTileHeight = 104.0f;

// Short, practical reminders; one per day.
constexpr const char* kTips[] = {
    "Before every move, look at checks, captures and threats: yours first, then your opponent's.",
    "A piece that is not defended is a piece that can be won. Count defenders before you move.",
    "When you see a good move, take a moment to look for a better one.",
    "Knights love squares where no enemy pawn can chase them away.",
    "Rooks belong on open files, and two rooks on the seventh rank are a menace.",
    "In the endgame the king is a fighting piece: bring it to the centre.",
    "If you do not know what to do, improve your worst-placed piece.",
    "A pin is only as strong as what stands behind the pinned piece.",
    "Trade pieces when you are ahead in material, keep them when you are attacking.",
    "Passed pawns must be pushed, and blockaded with a knight when they are the opponent's.",
    "Castle early when the centre opens up; a king in the middle gets caught.",
    "After a mistake, take a breath. The next move matters more than the last one.",
    "Review your losses: each one shows a pattern worth learning.",
    "Every opening move should develop a piece, fight for the centre or get the king safe.",
};

std::tm localNow() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return local;
}

const char* greeting(int hour) {
    if (hour >= 5 && hour < 12) return "Good morning";
    if (hour >= 12 && hour < 18) return "Good afternoon";
    if (hour >= 18 && hour < 23) return "Good evening";
    return "Up late";
}

std::string formatDuration(std::int64_t seconds) {
    char text[32];
    if (seconds >= 3600) {
        std::snprintf(text, sizeof(text), "%lld h %02lld min", static_cast<long long>(seconds / 3600),
                      static_cast<long long>(seconds / 60 % 60));
    } else {
        std::snprintf(text, sizeof(text), "%lld min", static_cast<long long>(seconds / 60));
    }
    return text;
}

void tile(const char* id, float width, const char* caption, const char* value, const char* detail) {
    ImGui::BeginChild(id, ImVec2(width, kTileHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("%s", caption);
    ImGui::PushFont(app::headlineFont());
    ImGui::TextUnformatted(value);
    ImGui::PopFont();
    ImGui::TextDisabled("%s", detail);
    ImGui::EndChild();
}


// The logo with rounded corners, as an item of the given size.
void roundedLogo(const app::Assets& assets, float size, float rounding) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImGui::GetWindowDrawList()->AddImageRounded(assets.logoTexture(), pos, ImVec2(pos.x + size, pos.y + size), ImVec2(0, 0),
                                                ImVec2(1, 1), IM_COL32_WHITE, rounding);
}

}  // namespace

HomeScreen::HomeScreen(app::Settings& settings, app::Assets& assets, storage::GameStore* games, storage::PuzzleStore* puzzles,
                       storage::TrainingStore* training)
    : settings_(settings), assets_(assets), games_(games), puzzles_(puzzles), training_(training) {}

HomeScreen::Action HomeScreen::takeAction() { return std::exchange(action_, Action::None); }
bool HomeScreen::consumeSettingsChanged() { return std::exchange(settingsChanged_, false); }

void HomeScreen::reload() {
    dirty_ = false;
    const int today = core::todayNumber();
    week_ = {};
    usage_ = {};
    streaks_ = {};
    gamesPlayed_ = 0;
    recentResults_.clear();
    if (games_ != nullptr) {
        usage_ = games_->usage();
        std::vector<int> days;
        for (const auto& [date, seconds] : games_->usageByDay()) {
            const std::optional<int> day = core::dayNumberFromIso(date);
            if (!day) continue;
            days.push_back(*day);
            const int back = today - *day;
            if (back >= 0 && back < 7) week_[static_cast<std::size_t>(6 - back)] += seconds;
        }
        streaks_ = core::computeStreaks(std::move(days), today);

        // The user's results in Play, newest first in the list.
        storage::GameFilter filter;
        filter.limit = 1000000;
        for (const storage::GameSummary& game : games_->list(filter)) {
            if (game.source != "play" || game.result == "*") continue;
            const bool white = core::matchesPlayer(game.white, settings_.playerNames);
            const bool black = core::matchesPlayer(game.black, settings_.playerNames);
            if (white == black) continue;
            ++gamesPlayed_;
            if (recentResults_.size() < 8) {
                const char result = game.result == "1/2-1/2" ? 'D' : ((game.result == "1-0") == white ? 'W' : 'L');
                recentResults_.insert(recentResults_.begin(), result);
            }
        }
    }
    if (puzzles_ != nullptr) {
        puzzleStats_ = puzzles_->stats();
        puzzleQueue_ = puzzles_->reviewQueue(today);
    }
    openingsDue_ = 0;
    if (training_ != nullptr) {
        for (const storage::RepertoireInfo& repertoire : training_->repertoires()) {
            for (const auto& [key, card] : training_->cards(repertoire.id)) {
                if (!card.isNew() && card.isDue(today)) ++openingsDue_;
            }
        }
    }
}

void HomeScreen::draw() {
    if (dirty_) reload();
    ImGui::BeginChild("##home", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    // A comfortable reading width on large windows.
    const float maxWidth = 1100.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > maxWidth) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - maxWidth) * 0.5f);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(std::min(avail, maxWidth));
    const float width = std::min(avail, maxWidth);

    drawHeader();
    ImGui::Spacing();
    ImGui::Spacing();
    drawTiles();
    ImGui::Spacing();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float columns = (width - style.ItemSpacing.x) * 0.5f;
    const float height = 300.0f;
    drawActions(columns, height);
    ImGui::SameLine();
    drawWeek(columns, height);
    ImGui::Spacing();
    drawFooter();

    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::EndChild();
}

void HomeScreen::drawHeader() {
    const float logoSize = 72.0f;
    if (assets_.hasLogo()) {
        roundedLogo(assets_, logoSize, 16.0f);
        ImGui::SameLine(0.0f, 18.0f);
    }
    ImGui::BeginGroup();
    const std::tm now = localNow();
    std::string name = settings_.displayName;
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::PushFont(app::headlineFont());
    if (name.empty()) {
        ImGui::Text("%s!", greeting(now.tm_hour));
    } else {
        ImGui::Text("%s, %s!", greeting(now.tm_hour), name.c_str());
    }
    ImGui::PopFont();

    // A line that changes with how things are going.
    std::string line;
    if (streaks_.current >= 2) {
        line = "Day " + std::to_string(streaks_.current) + " of your streak. Nice to see you again.";
        if (streaks_.current >= streaks_.longest && streaks_.longest > 2) line = "Day " + std::to_string(streaks_.current) +
                                                                                 " in a row: your longest streak yet!";
    } else if (usage_.totalSeconds > 0 && streaks_.longest > 0) {
        line = "Welcome back. Every day you open the board counts towards a new streak.";
    } else {
        line = "Welcome. Pick something below to get started.";
    }
    ImGui::TextDisabled("%s", line.c_str());
    ImGui::EndGroup();

    // Asks once for a name to greet by.
    if (name.empty()) {
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("What should I call you?");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        const bool entered = ImGui::InputTextWithHint("##name", "Your name", nameInput_.data(), nameInput_.size(),
                                                      ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Save") || entered) && nameInput_[0] != '\0') {
            settings_.displayName = nameInput_.data();
            settings_.sanitize();
            settingsChanged_ = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(you can change it in Settings)");
    }
}

void HomeScreen::drawTiles() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = (ImGui::CalcItemWidth() - 3.0f * style.ItemSpacing.x) / 4.0f;
    char value[48];
    char detail[96];

    std::snprintf(value, sizeof(value), "%d %s", streaks_.current, streaks_.current == 1 ? "day" : "days");
    std::snprintf(detail, sizeof(detail), "longest %d %s", streaks_.longest, streaks_.longest == 1 ? "day" : "days");
    tile("##h-streak", width, "Streak", value, detail);
    ImGui::SameLine();

    std::snprintf(value, sizeof(value), "%s", formatDuration(usage_.todaySeconds).c_str());
    std::snprintf(detail, sizeof(detail), "%s in total", formatDuration(usage_.totalSeconds).c_str());
    tile("##h-today", width, "Today", value, detail);
    ImGui::SameLine();

    if (puzzleStats_.attempts > 0) {
        std::snprintf(value, sizeof(value), "%d", puzzleStats_.rating);
        std::snprintf(detail, sizeof(detail), "%d solved  \xC2\xB7  best streak %d", puzzleStats_.solved, puzzleStats_.bestStreak);
    } else {
        std::snprintf(value, sizeof(value), "-");
        std::snprintf(detail, sizeof(detail), "no puzzles solved yet");
    }
    tile("##h-puzzles", width, "Puzzle rating", value, detail);
    ImGui::SameLine();

    // Recent results as coloured dots.
    ImGui::BeginChild("##h-games", ImVec2(width, kTileHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("Games played");
    ImGui::PushFont(app::headlineFont());
    ImGui::Text("%d", gamesPlayed_);
    ImGui::PopFont();
    if (recentResults_.empty()) {
        ImGui::TextDisabled("none finished yet");
    } else {
        ImGui::TextDisabled("last:");
        ImGui::SameLine();
        const float radius = ImGui::GetTextLineHeight() * 0.28f;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        pos.y += ImGui::GetTextLineHeight() * 0.5f;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        for (const char result : recentResults_) {
            const ImU32 color = result == 'W'   ? IM_COL32(110, 175, 70, 255)
                                : result == 'L' ? IM_COL32(205, 80, 70, 255)
                                                : IM_COL32(150, 150, 145, 255);
            drawList->AddCircleFilled(ImVec2(pos.x + radius, pos.y), radius, color);
            pos.x += radius * 2.0f + 5.0f;
        }
        ImGui::Dummy(ImVec2(static_cast<float>(recentResults_.size()) * (radius * 2.0f + 5.0f), ImGui::GetTextLineHeight()));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Your last games in Play, oldest to newest: won, drawn (grey), lost");
    }
    ImGui::EndChild();
}

void HomeScreen::drawActions(float width, float height) {
    ImGui::BeginChild("##h-actions", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(app::titleFont());
    ImGui::TextUnformatted("Pick up where you left off");
    ImGui::PopFont();
    ImGui::Spacing();

    const float buttonWidth = ImGui::GetContentRegionAvail().x;
    const auto entry = [&](const char* label, const char* detail, Action action) {
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        const std::string text = std::string(label) + "   ";
        if (ImGui::Button(text.c_str(), ImVec2(buttonWidth, 36.0f))) action_ = action;
        ImGui::PopStyleVar();
        if (detail != nullptr) {
            // The detail sits at the right end of the button.
            const ImVec2 size = ImGui::CalcTextSize(detail);
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddText(ImVec2(max.x - size.x - 12.0f, max.y - 18.0f - size.y * 0.5f),
                                                ImGui::GetColorU32(ImGuiCol_TextDisabled), detail);
        }
    };

    char detail[64];
    entry("Play a game", "against the computer", Action::NewGame);
    entry("Daily puzzle", "the same for everyone today", Action::DailyPuzzle);
    if (puzzleQueue_.due > 0) {
        std::snprintf(detail, sizeof(detail), "%d due", puzzleQueue_.due);
        entry("Review missed puzzles", detail, Action::ReviewPuzzles);
    }
    if (openingsDue_ > 0) {
        std::snprintf(detail, sizeof(detail), "%d %s due", openingsDue_, openingsDue_ == 1 ? "position" : "positions");
        entry("Train your openings", detail, Action::Openings);
    } else {
        entry("Openings", "your repertoires", Action::Openings);
    }
    entry("Training", "weaknesses and mistake drill", Action::Training);
    entry("Guess the Move", "play through a classic", Action::Guess);
    ImGui::EndChild();
}

void HomeScreen::drawWeek(float width, float height) {
    ImGui::BeginChild("##h-week", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(app::titleFont());
    ImGui::TextUnformatted("This week");
    ImGui::PopFont();
    ImGui::Spacing();

    // Seven bars, today on the right.
    const float chartHeight = 110.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float chartWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(chartWidth, chartHeight + ImGui::GetTextLineHeightWithSpacing()));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const std::int64_t most = std::max<std::int64_t>(*std::max_element(week_.begin(), week_.end()), 15 * 60);
    const float slot = chartWidth / 7.0f;
    const float barWidth = std::min(slot * 0.55f, 34.0f);
    const int todayWeekday = (core::todayNumber() + 3) % 7;  // 0 = Monday
    static const char* kDays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    for (int i = 0; i < 7; ++i) {
        const float x = origin.x + slot * (static_cast<float>(i) + 0.5f);
        const float fraction = static_cast<float>(week_[static_cast<std::size_t>(i)]) / static_cast<float>(most);
        const float barHeight = std::max(3.0f, chartHeight * std::min(fraction, 1.0f));
        const ImU32 color = week_[static_cast<std::size_t>(i)] > 0 ? ImGui::ColorConvertFloat4ToU32(kAccentColor)
                                                                  : ImGui::GetColorU32(ImGuiCol_FrameBg);
        drawList->AddRectFilled(ImVec2(x - barWidth * 0.5f, origin.y + chartHeight - barHeight),
                                ImVec2(x + barWidth * 0.5f, origin.y + chartHeight), color, 4.0f);
        const char* label = kDays[(todayWeekday - 6 + i + 14) % 7];
        const ImVec2 size = ImGui::CalcTextSize(label);
        drawList->AddText(ImVec2(x - size.x * 0.5f, origin.y + chartHeight + 4.0f),
                          ImGui::GetColorU32(i == 6 ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (ImGui::IsWindowHovered() && mouse.x > x - slot * 0.5f && mouse.x < x + slot * 0.5f && mouse.y > origin.y &&
            mouse.y < origin.y + chartHeight) {
            ImGui::SetTooltip("%s: %s", i == 6 ? "Today" : label, formatDuration(week_[static_cast<std::size_t>(i)]).c_str());
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(kAccentColor, "Tip of the day");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(kTips[static_cast<std::size_t>(core::todayNumber()) % std::size(kTips)]);
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

void HomeScreen::drawFooter() {
    ImGui::Spacing();
    if (assets_.hasLogo()) {
        roundedLogo(assets_, 20.0f, 5.0f);
        ImGui::SameLine();
    }
    ImGui::TextDisabled("Chess  \xC2\xB7  made by Am\xC3\xA9lie Heinrich  \xC2\xB7  Moonscorched Productions");
}

}  // namespace ui

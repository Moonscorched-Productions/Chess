#include "ui/EndgameScreen.h"

#include <cstring>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

#include "ui/Common.h"

namespace ui {

EndgameScreen::EndgameScreen(storage::TrainingStore* store) : store_(store) {}

const core::EndgamePosition* EndgameScreen::takeStartRequest() { return std::exchange(startRequest_, nullptr); }

void EndgameScreen::draw() {
    if (dirty_) {
        dirty_ = false;
        if (store_ != nullptr) progress_ = store_->endgameProgress();
    }

    ImGui::TextUnformatted("Endgames");
    int mastered = 0;
    for (const core::EndgamePosition& position : core::endgamePositions()) {
        const auto it = progress_.find(position.id);
        if (it != progress_.end() && it->second.successes > 0) ++mastered;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d of %d completed", mastered, static_cast<int>(core::endgamePositions().size()));
    ImGui::TextDisabled("Each position is played against the engine at full strength.");
    ImGui::Spacing();

    ImGui::BeginChild("##endgames", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp;
    const char* category = "";
    bool tableOpen = false;

    for (const core::EndgamePosition& position : core::endgamePositions()) {
        if (std::strcmp(category, position.category) != 0) {
            if (tableOpen) ImGui::EndTable();
            category = position.category;
            ImGui::Spacing();
            ImGui::SeparatorText(category);
            tableOpen = ImGui::BeginTable(category, 4, flags);
            if (tableOpen) {
                ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn("You play", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Idea", ImGuiTableColumnFlags_WidthStretch, 6.0f);
            }
        }
        if (!tableOpen) continue;

        const auto found = progress_.find(position.id);
        const storage::EndgameProgress progress = found != progress_.end() ? found->second : storage::EndgameProgress{};

        // The whole row starts the position.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(position.id);
        // The idea may wrap; the row (and its hover card) is as tall as it.
        float height = 0.0f;
        if (const ImGuiTable* table = ImGui::GetCurrentTable(); table != nullptr && table->ColumnsCount > 3) {
            const float wrap = table->Columns[3].WorkMaxX - table->Columns[3].WorkMinX;
            if (wrap > 0.0f) height = ImGui::CalcTextSize(position.hint, nullptr, false, wrap).y;
        }
        if (tableRow(position.name, false, height).clicked) startRequest_ = &position;
        ImGui::SetItemTooltip("Practise this position against the engine");
        ImGui::PopID();

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%s  \xC2\xB7  %s", position.userColor == core::Color::White ? "White" : "Black",
                    position.goal == core::EndgameGoal::Win ? "win" : "draw");

        ImGui::TableSetColumnIndex(2);
        if (progress.attempts == 0) {
            ImGui::TextDisabled("Not tried");
        } else if (progress.successes > 0) {
            ImGui::TextColored(kAccentColor, "%d of %d", progress.successes, progress.attempts);
        } else {
            ImGui::TextColored(kErrorColor, "0 of %d", progress.attempts);
        }

        ImGui::TableSetColumnIndex(3);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", position.hint);
        ImGui::PopTextWrapPos();
    }
    if (tableOpen) ImGui::EndTable();
    ImGui::EndChild();
}

}  // namespace ui

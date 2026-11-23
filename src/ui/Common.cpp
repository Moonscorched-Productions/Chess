#include "ui/Common.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace ui {

namespace {

ImU32 toU32(const std::array<float, 3>& color) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], 1.0f));
}

}  // namespace

BoardOptions boardOptionsFrom(const app::Settings& settings) {
    BoardOptions options;
    options.showCoordinates = settings.showCoordinates;
    options.showLegalMoves = settings.showLegalMoves;
    options.highlightLastMove = settings.highlightLastMove;
    options.lightSquare = toU32(settings.lightSquare);
    options.darkSquare = toU32(settings.darkSquare);
    return options;
}

std::string formatEval(const core::Eval& eval) {
    char buffer[24];
    if (eval.mate && *eval.mate == 0) {
        return eval.whiteIsAhead() ? "1-0" : "0-1";
    } else if (eval.mate) {
        std::snprintf(buffer, sizeof(buffer), "%sM%d", *eval.mate < 0 ? "-" : "", std::abs(*eval.mate));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%+.2f", static_cast<double>(eval.cp) / 100.0);
    }
    return buffer;
}

const char* nagSymbol(int nag) {
    switch (nag) {
        case 1: return "!";
        case 2: return "?";
        case 3: return "!!";
        case 4: return "??";
        case 5: return "!?";
        case 6: return "?!";
        default: return "";
    }
}

void drawEvalBar(ImVec2 size, const std::optional<core::Eval>& eval, bool flipped) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImU32 white = IM_COL32(235, 235, 232, 255);
    const ImU32 black = IM_COL32(48, 46, 43, 255);
    const float share = eval ? static_cast<float>(eval->whiteWinPercent() / 100.0) : 0.5f;
    const float whiteHeight = std::clamp(share, 0.0f, 1.0f) * size.y;
    const ImVec2 end(origin.x + size.x, origin.y + size.y);

    drawList->AddRectFilled(origin, end, black, 3.0f);
    if (whiteHeight > 0.5f) {
        if (flipped) {
            drawList->AddRectFilled(origin, ImVec2(end.x, origin.y + whiteHeight), white, 3.0f);
        } else {
            drawList->AddRectFilled(ImVec2(origin.x, end.y - whiteHeight), end, white, 3.0f);
        }
    }
    const float middle = origin.y + size.y * 0.5f;
    drawList->AddLine(ImVec2(origin.x, middle), ImVec2(end.x, middle), IM_COL32(200, 80, 60, 160), 1.0f);

    if (!eval) return;
    // The number sits at the end belonging to the side that is ahead.
    std::string text;
    if (eval->mate && *eval->mate == 0) {
        text = "#";
    } else if (eval->mate) {
        text = "M" + std::to_string(std::abs(*eval->mate));
    } else {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%.1f", std::abs(static_cast<double>(eval->cp)) / 100.0);
        text = buffer;
    }
    const bool whiteAhead = eval->whiteIsAhead();
    ImFont* font = ImGui::GetFont();
    const float fontSize = 12.0f;
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, 1000.0f, 0.0f, text.c_str());
    const bool atBottom = whiteAhead != flipped;
    const float y = atBottom ? end.y - textSize.y - 3.0f : origin.y + 3.0f;
    drawList->AddText(font, fontSize, ImVec2(origin.x + (size.x - textSize.x) * 0.5f, y), whiteAhead ? black : white,
                      text.c_str());
}

bool beginCenteredModal(const char* title, ImGuiWindowFlags flags) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    return ImGui::BeginPopupModal(title, nullptr, flags | ImGuiWindowFlags_NoSavedSettings);
}

}  // namespace ui

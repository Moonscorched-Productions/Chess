#include "ui/Common.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <imgui_internal.h>

namespace ui {

namespace {

ImU32 toU32(const std::array<float, 3>& color) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], 1.0f));
}

}  // namespace

void setColorScheme(ImVec4 accent, ImVec4 error) {
    kAccentColor = accent;
    kErrorColor = error;
}

bool blindfoldHidden(const app::Settings& settings) {
    return settings.blindfold && !(ImGui::IsKeyDown(ImGuiKey_Space) && !ImGui::GetIO().WantTextInput);
}

BoardOptions boardOptionsFrom(const app::Settings& settings) {
    BoardOptions options;
    options.showCoordinates = settings.showCoordinates;
    options.showLegalMoves = settings.showLegalMoves;
    options.highlightLastMove = settings.highlightLastMove;
    options.animate = settings.animations;
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
    } else if (std::abs(eval.cp) >= kTablebaseScore) {
        return eval.cp > 0 ? "+TB" : "-TB";  // a win found in the endgame tablebases
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

BoardLayout boardLayout(float panelFraction, float minPanel, float maxPanel, float extraWidth, float extraHeight) {
    constexpr float kRightMargin = 8.0f;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float startX = ImGui::GetCursorPosX();
    BoardLayout layout;
    layout.panelWidth = std::clamp(avail.x * panelFraction, minPanel, maxPanel);
    // Room for the board column, and some air on both sides of it when there is space.
    const float area = avail.x - layout.panelWidth - kRightMargin - style.ItemSpacing.x;
    const float air = std::clamp(area * 0.05f, 0.0f, 40.0f);
    float board = std::min(area - extraWidth - 2.0f * air, avail.y - extraHeight);
    layout.boardSize = std::floor(std::max(board, 240.0f) / 8.0f) * 8.0f;
    const float column = extraWidth + layout.boardSize;
    layout.boardX = startX + std::max(0.0f, std::floor((area - column) * 0.5f));
    layout.panelX = std::max(startX + avail.x - layout.panelWidth - kRightMargin, layout.boardX + column + style.ItemSpacing.x);
    return layout;
}

void tableHeaders() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TableHeadersRow();
    ImGui::PopStyleColor();
    ImGuiTable* table = ImGui::GetCurrentTable();
    if (table == nullptr) return;
    // The rule sits on the boundary between the header row and the first row.
    const float y = ImGui::GetItemRectMax().y + ImGui::GetStyle().CellPadding.y;
    ImGui::TablePushBackgroundChannel();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(table->WorkRect.Min.x, y), ImVec2(table->WorkRect.Max.x, y),
                                        ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
    ImGui::TablePopBackgroundChannel();
}

RowClick tableRow(const char* label, bool selected, float height) {
    RowClick result;
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
    result.clicked = ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                                       ImVec2(0.0f, height));
    ImGui::PopStyleColor(3);
    result.doubleClicked = result.clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const bool hovered = ImGui::IsItemHovered();
    if (!hovered && !selected) return result;

    // The card covers the whole row, cell padding included.
    const float padY = ImGui::GetStyle().CellPadding.y;
    const ImVec2 min(ImGui::GetItemRectMin().x + 2.0f, ImGui::GetItemRectMin().y - padY + 1.0f);
    const ImVec2 max(ImGui::GetItemRectMax().x - 2.0f, ImGui::GetItemRectMax().y + padY - 1.0f);
    ImGui::TablePushBackgroundChannel();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), 6.0f);
    if (selected) {
        drawList->AddRectFilled(ImVec2(min.x + 2.0f, min.y + 5.0f), ImVec2(min.x + 5.0f, max.y - 5.0f),
                                ImGui::GetColorU32(ImGuiCol_CheckMark), 2.0f);
    }
    ImGui::TablePopBackgroundChannel();
    return result;
}

EvalColors evalColors() {
    const ImVec4 accent = kAccentColor;
    const ImVec4 panel = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    const auto mix = [](ImVec4 a, ImVec4 b, float t) {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
    };
    // White and black, each with a touch of the theme so they sit well on its panels.
    EvalColors colors;
    colors.white = ImGui::ColorConvertFloat4ToU32(mix(ImVec4(0.94f, 0.93f, 0.91f, 1.0f), accent, 0.08f));
    colors.black = ImGui::ColorConvertFloat4ToU32(mix(mix(panel, ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.55f), accent, 0.06f));
    colors.midline = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.55f));
    colors.marker = ImGui::ColorConvertFloat4ToU32(accent);
    return colors;
}

void drawEvalBar(ImVec2 size, const std::optional<core::Eval>& eval, bool flipped, bool animate) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const EvalColors colors = evalColors();
    const ImU32 white = colors.white;
    const ImU32 black = colors.black;
    const float target = std::clamp(eval ? static_cast<float>(eval->whiteWinPercent() / 100.0) : 0.5f, 0.0f, 1.0f);
    // The share shown last frame lives in the window's state storage; it eases towards the target.
    float& shown = *ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("##evalbar"), -1.0f);
    if (!animate || shown < 0.0f) {
        shown = target;
    } else {
        shown += (target - shown) * (1.0f - std::exp(-ImGui::GetIO().DeltaTime * 10.0f));
        if (std::abs(target - shown) < 0.001f) shown = target;
    }
    const float whiteHeight = shown * size.y;
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
    drawList->AddLine(ImVec2(origin.x, middle), ImVec2(end.x, middle), colors.midline, 1.0f);

    if (!eval) return;
    // The number sits at the end belonging to the side that is ahead.
    std::string text;
    if (eval->mate && *eval->mate == 0) {
        text = "#";
    } else if (eval->mate) {
        text = "M" + std::to_string(std::abs(*eval->mate));
    } else if (std::abs(eval->cp) >= kTablebaseScore) {
        text = "TB";
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

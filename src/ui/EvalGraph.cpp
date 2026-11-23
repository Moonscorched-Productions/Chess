#include "ui/EvalGraph.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "ui/Common.h"
#include "ui/QualityBadge.h"

namespace ui {

using core::kNoNode;
using core::kRootNode;
using core::NodeId;

NodeId drawEvalGraph(const core::GameTree& tree, NodeId current, ImVec2 size,
                     const std::function<core::MoveQuality(NodeId)>& quality, const char* unanalysedHint) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##graph", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    // The dark area is Black's share, the light area White's, in either colour scheme.
    drawList->AddRectFilled(origin, end, IM_COL32(48, 46, 43, 255), 4.0f);
    const ImU32 hintColor = IM_COL32(150, 150, 145, 255);

    std::vector<NodeId> nodes{kRootNode};
    const std::vector<NodeId> mainLine = tree.mainLine();
    nodes.insert(nodes.end(), mainLine.begin(), mainLine.end());

    const float middle = origin.y + size.y * 0.5f;
    if (nodes.size() < 2) {
        const char* hint = "The evaluation graph appears here once a game has moves.";
        const ImVec2 textSize = ImGui::CalcTextSize(hint);
        if (textSize.x < size.x) {
            drawList->AddText(ImVec2(origin.x + (size.x - textSize.x) * 0.5f, middle - textSize.y * 0.5f), hintColor, hint);
        }
        return kNoNode;
    }

    const std::size_t count = nodes.size();
    const auto xAt = [&](std::size_t i) {
        return origin.x + size.x * static_cast<float>(i) / static_cast<float>(count - 1);
    };
    std::vector<std::optional<float>> share(count);  // White's expected score, 0..1
    int known = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (const std::optional<core::Eval>& eval = tree.node(nodes[i]).eval) {
            share[i] = static_cast<float>(eval->whiteWinPercent() / 100.0);
            ++known;
        }
    }
    const auto yAt = [&](float value) { return origin.y + size.y * (1.0f - value); };

    // White's share is the light area rising from the bottom. Anti-aliasing is off
    // for the fill so neighbouring segments join without visible seams.
    const ImDrawListFlags savedFlags = drawList->Flags;
    drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (std::size_t i = 0; i + 1 < count; ++i) {
        if (!share[i] || !share[i + 1]) continue;
        const ImVec2 a(std::floor(xAt(i)), yAt(*share[i]));
        const ImVec2 b(std::floor(xAt(i + 1)), yAt(*share[i + 1]));
        drawList->AddQuadFilled(a, b, ImVec2(b.x, end.y), ImVec2(a.x, end.y), IM_COL32(225, 225, 220, 255));
    }
    drawList->Flags = savedFlags;
    drawList->AddLine(ImVec2(origin.x, middle), ImVec2(end.x, middle), IM_COL32(200, 80, 60, 150), 1.0f);

    // Mark moves that threw away a large part of the mover's expected score.
    if (quality) {
        for (std::size_t i = 1; i < count; ++i) {
            if (!share[i]) continue;
            const core::MoveQuality moveQuality = quality(nodes[i]);
            if (moveQuality != core::MoveQuality::Inaccuracy && moveQuality != core::MoveQuality::Mistake &&
                moveQuality != core::MoveQuality::Blunder) {
                continue;
            }
            const ImVec2 point(xAt(i), yAt(*share[i]));
            drawList->AddCircleFilled(point, 4.5f, IM_COL32(0, 0, 0, 110));
            drawList->AddCircleFilled(point, 3.5f, qualityColor(moveQuality));
        }
    }

    if (known < 2 && unanalysedHint != nullptr) {
        const ImVec2 textSize = ImGui::CalcTextSize(unanalysedHint);
        if (textSize.x < size.x) {
            drawList->AddText(ImVec2(origin.x + (size.x - textSize.x) * 0.5f, origin.y + 6.0f), hintColor, unanalysedHint);
        }
    }

    const auto found = std::find(nodes.begin(), nodes.end(), current);
    if (found != nodes.end()) {
        const float x = xAt(static_cast<std::size_t>(found - nodes.begin()));
        drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), IM_COL32(153, 204, 102, 255), 2.0f);
    }

    if (!hovered) return kNoNode;
    const float relative = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f);
    const std::size_t index = static_cast<std::size_t>(std::lround(relative * static_cast<float>(count - 1)));
    const NodeId id = nodes[index];
    const float x = xAt(index);
    drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), IM_COL32(255, 255, 255, 70), 1.0f);

    std::string text = "Start position";
    if (id != kRootNode) {
        text = std::to_string(tree.moveNumber(id)) + (tree.mover(id) == core::Color::White ? ". " : "... ") +
               tree.node(id).record.san;
    }
    if (const std::optional<core::Eval>& eval = tree.node(id).eval) text += "   " + formatEval(*eval);
    ImGui::SetTooltip("%s", text.c_str());
    return clicked ? id : kNoNode;
}

}  // namespace ui

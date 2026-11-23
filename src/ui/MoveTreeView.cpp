#include "ui/MoveTreeView.h"

#include <algorithm>
#include <vector>

#include <imgui.h>

#include "ui/Common.h"
#include "ui/QualityBadge.h"

namespace ui {

using core::kRootNode;
using core::NodeId;

MoveTreeActions MoveTreeView::draw(const core::GameTree& tree, NodeId current, float height, bool editable) {
    current_ = current;
    editable_ = editable;
    actions_ = {};

    ImGui::BeginChild("##tree", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

    const std::string& rootComment = tree.node(kRootNode).comment;
    if (!rootComment.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", rootComment.c_str());
        ImGui::PopTextWrapPos();
    }

    if (tree.node(kRootNode).children.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", emptyHint.c_str());
        ImGui::PopTextWrapPos();
    } else {
        flowRight_ = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        flowLineStart_ = true;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 5.0f));
        drawLine(tree, tree.node(kRootNode).children.front(), 0, true);
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    return actions_;
}

void MoveTreeView::drawLine(const core::GameTree& tree, NodeId first, int depth, bool needNumber) {
    const auto indentFor = [](int level) { return 2.0f + static_cast<float>(std::min(level, 8)) * 16.0f; };

    NodeId id = first;
    for (;;) {
        const core::TreeNode& node = tree.node(id);
        flowIndent_ = indentFor(depth);
        const bool white = tree.mover(id) == core::Color::White;
        drawMove(tree, id, white || needNumber, depth > 0);
        needNumber = false;

        if (!node.comment.empty()) {
            ImGui::SetCursorPosX(flowIndent_);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", node.comment.c_str());
            ImGui::PopTextWrapPos();
            flowLineStart_ = true;
            needNumber = true;
        }

        // Alternatives to this move, each on its own indented line.
        const std::vector<NodeId>& siblings = tree.node(node.parent).children;
        if (siblings.front() == id && siblings.size() > 1) {
            for (std::size_t i = 1; i < siblings.size(); ++i) {
                flowLineStart_ = true;
                drawLine(tree, siblings[i], depth + 1, true);
            }
            flowLineStart_ = true;
            flowIndent_ = indentFor(depth);
            needNumber = true;
        }

        if (node.children.empty()) return;
        id = node.children.front();
    }
}

void MoveTreeView::drawMove(const core::GameTree& tree, NodeId id, bool withNumber, bool variation) {
    const core::TreeNode& node = tree.node(id);

    std::string label;
    if (withNumber) label = std::to_string(tree.moveNumber(id)) + (tree.mover(id) == core::Color::White ? ". " : "... ");
    label += node.record.san;
    for (const int nag : node.nags) label += nagSymbol(nag);

    // Notable moves carry a small badge after the move text.
    const core::MoveQuality moveQuality = quality ? quality(id) : core::MoveQuality::None;
    const bool showBadge = moveQuality == core::MoveQuality::Best || moveQuality == core::MoveQuality::Inaccuracy ||
                           moveQuality == core::MoveQuality::Mistake || moveQuality == core::MoveQuality::Blunder;
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    const float badgeRadius = textSize.y * 0.42f;
    const ImVec2 size(textSize.x + (showBadge ? badgeRadius * 2.0f + 5.0f : 0.0f), textSize.y);

    if (flowLineStart_) {
        ImGui::SetCursorPosX(flowIndent_);
        flowLineStart_ = false;
    } else {
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::GetCursorPosX() + size.x > flowRight_) {
            ImGui::NewLine();
            ImGui::SetCursorPosX(flowIndent_);
        }
    }

    ImGui::PushID(id);
    const bool selected = id == current_;
    if (variation && !selected) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None, size)) actions_.clicked = id;
    if (variation && !selected) ImGui::PopStyleColor();

    if (showBadge) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        drawQualityBadge(ImGui::GetWindowDrawList(), ImVec2(max.x - badgeRadius - 1.0f, (min.y + max.y) * 0.5f), badgeRadius,
                         moveQuality);
    }
    if (moveQuality != core::MoveQuality::None && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        std::string tip = core::qualityName(moveQuality);
        if (const std::optional<core::Eval>& eval = node.eval) tip += "   " + formatEval(*eval);
        const std::string& better = tree.node(node.parent).bestMove;
        if (moveQuality != core::MoveQuality::Best && !better.empty()) tip += "\nEngine preferred " + better;
        ImGui::SetTooltip("%s", tip.c_str());
    }
    if (selected && scrollToCurrent_) {
        ImGui::SetScrollHereY(0.5f);
        scrollToCurrent_ = false;
    }

    if (editable_ && ImGui::BeginPopupContextItem("##move-menu")) {
        const std::vector<NodeId>& siblings = tree.node(node.parent).children;
        if (ImGui::MenuItem("Promote variation", nullptr, false, siblings.front() != id)) actions_.promote = id;
        if (ImGui::MenuItem("Make main line", nullptr, false, !tree.isMainLine(id))) actions_.mainLine = id;
        ImGui::Separator();
        if (ImGui::MenuItem("Delete from here")) actions_.remove = id;
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

}  // namespace ui

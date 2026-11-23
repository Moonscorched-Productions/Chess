#include "ui/QualityBadge.h"

#include <array>
#include <cmath>

namespace ui {

namespace {

constexpr float kPi = 3.14159265f;

void drawStar(ImDrawList* drawList, ImVec2 center, float radius, ImU32 color) {
    // Ten points alternating between the outer and inner radius, filled as a fan from the centre.
    std::array<ImVec2, 10> points;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const float angle = -kPi / 2.0f + static_cast<float>(i) * kPi / 5.0f;
        const float r = i % 2 == 0 ? radius : radius * 0.42f;
        points[i] = ImVec2(center.x + std::cos(angle) * r, center.y + std::sin(angle) * r);
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        drawList->AddTriangleFilled(center, points[i], points[(i + 1) % points.size()], color);
    }
}

void drawTick(ImDrawList* drawList, ImVec2 center, float size, ImU32 color, float thickness) {
    const ImVec2 start(center.x - size * 0.55f, center.y + size * 0.02f);
    const ImVec2 corner(center.x - size * 0.15f, center.y + size * 0.42f);
    const ImVec2 end(center.x + size * 0.58f, center.y - size * 0.40f);
    drawList->AddLine(start, corner, color, thickness);
    drawList->AddLine(corner, end, color, thickness);
}

void drawLabel(ImDrawList* drawList, ImVec2 center, float radius, const char* text, ImU32 color) {
    ImFont* font = ImGui::GetFont();
    // Two characters need a slightly smaller size to stay inside the disc.
    const float fontSize = radius * (text[1] == '\0' ? 1.7f : 1.35f);
    const ImVec2 size = font->CalcTextSizeA(fontSize, 1000.0f, 0.0f, text);
    const ImVec2 pos(center.x - size.x * 0.5f, center.y - size.y * 0.5f);
    // Drawn twice, half a pixel apart, for a bolder look.
    drawList->AddText(font, fontSize, pos, color, text);
    drawList->AddText(font, fontSize, ImVec2(pos.x + 0.5f, pos.y), color, text);
}

}  // namespace

ImU32 qualityColor(core::MoveQuality quality) {
    switch (quality) {
        case core::MoveQuality::Best: return IM_COL32(104, 170, 62, 255);
        case core::MoveQuality::Excellent: return IM_COL32(104, 170, 62, 255);
        case core::MoveQuality::Good: return IM_COL32(122, 150, 104, 255);
        case core::MoveQuality::Inaccuracy: return IM_COL32(232, 184, 48, 255);
        case core::MoveQuality::Mistake: return IM_COL32(232, 132, 44, 255);
        case core::MoveQuality::Blunder: return IM_COL32(212, 62, 52, 255);
        case core::MoveQuality::None: break;
    }
    return IM_COL32(0, 0, 0, 0);
}

void drawQualityBadge(ImDrawList* drawList, ImVec2 center, float radius, core::MoveQuality quality) {
    if (quality == core::MoveQuality::None || radius < 2.0f) return;
    const ImU32 white = IM_COL32(255, 255, 255, 255);

    // A thin dark rim keeps the badge readable on light squares and light pieces.
    drawList->AddCircleFilled(center, radius + 1.0f, IM_COL32(0, 0, 0, 90), 28);
    drawList->AddCircleFilled(center, radius, qualityColor(quality), 28);

    switch (quality) {
        case core::MoveQuality::Best:
            drawStar(drawList, center, radius * 0.68f, white);
            break;
        case core::MoveQuality::Excellent:
            drawTick(drawList, ImVec2(center.x - radius * 0.14f, center.y), radius * 0.62f, white, radius * 0.24f);
            drawTick(drawList, ImVec2(center.x + radius * 0.22f, center.y), radius * 0.62f, white, radius * 0.24f);
            break;
        case core::MoveQuality::Good:
            drawTick(drawList, center, radius * 0.78f, white, radius * 0.26f);
            break;
        case core::MoveQuality::Inaccuracy:
            drawLabel(drawList, center, radius, "?!", white);
            break;
        case core::MoveQuality::Mistake:
            drawLabel(drawList, center, radius, "?", white);
            break;
        case core::MoveQuality::Blunder:
            drawLabel(drawList, center, radius, "??", white);
            break;
        case core::MoveQuality::None:
            break;
    }
}

}  // namespace ui

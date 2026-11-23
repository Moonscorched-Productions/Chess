#pragma once

#include <imgui.h>

#include "chess/Review.h"

namespace ui {

// Colour associated with a move quality (transparent for None).
ImU32 qualityColor(core::MoveQuality quality);

// Sets the badge sheet (data/badges/badges.png: six cells in a row, in MoveQuality order).
// Without it the badges are drawn from simple shapes.
void setQualityBadgeSheet(ImTextureID texture);

// Draws the round badge for a move quality: a coloured disc with a symbol
// (star, ticks, "?!", "?", "??").
void drawQualityBadge(ImDrawList* drawList, ImVec2 center, float radius, core::MoveQuality quality);

}  // namespace ui

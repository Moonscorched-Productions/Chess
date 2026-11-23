#pragma once

#include <imgui.h>

#include "chess/Review.h"

namespace ui {

// Colour associated with a move quality (transparent for None).
ImU32 qualityColor(core::MoveQuality quality);

// Draws the round badge for a move quality: a coloured disc with a symbol
// (star, tick, "?!", "?", "??"). Drawn from shapes, so it is sharp at any size.
void drawQualityBadge(ImDrawList* drawList, ImVec2 center, float radius, core::MoveQuality quality);

}  // namespace ui

#pragma once

#include <functional>

#include <imgui.h>

#include "chess/GameTree.h"
#include "chess/Review.h"

namespace ui {

// Draws the evaluation of a game's main line as a graph of White's expected score, with
// dots on inaccuracies, mistakes and blunders and a marker at `current`.
// `quality` classifies moves (may be empty); `unanalysedHint` is shown while the line has
// no evaluations. Returns the node the user clicked, or kNoNode.
core::NodeId drawEvalGraph(const core::GameTree& tree, core::NodeId current, ImVec2 size,
                           const std::function<core::MoveQuality(core::NodeId)>& quality, const char* unanalysedHint);

}  // namespace ui

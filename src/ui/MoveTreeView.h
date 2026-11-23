#pragma once

#include <functional>
#include <string>

#include "chess/GameTree.h"
#include "chess/Review.h"

namespace ui {

// What the user asked for while the tree was drawn. The caller applies it.
struct MoveTreeActions {
    core::NodeId clicked = core::kNoNode;   // go to this move
    core::NodeId remove = core::kNoNode;    // delete this move and what follows
    core::NodeId promote = core::kNoNode;   // move this variation one place up
    core::NodeId mainLine = core::kNoNode;  // make this the main line
};

// Shows a game tree as flowing text: the main line with variations indented below the
// move they replace. Moves are clickable and have a context menu for editing.
class MoveTreeView {
public:
    // Optional: classifies moves so that notable ones get a badge and a tooltip.
    std::function<core::MoveQuality(core::NodeId)> quality;
    // Shown when the tree has no moves.
    std::string emptyHint = "No moves yet.";

    // Brings the current move into view on the next draw.
    void scrollToCurrent() { scrollToCurrent_ = true; }

    // Draws the tree in a child window of the given height.
    MoveTreeActions draw(const core::GameTree& tree, core::NodeId current, float height, bool editable = true);

private:
    void drawLine(const core::GameTree& tree, core::NodeId first, int depth, bool needNumber);
    void drawMove(const core::GameTree& tree, core::NodeId id, bool withNumber, bool variation);

    core::NodeId current_ = core::kNoNode;
    bool editable_ = true;
    bool scrollToCurrent_ = false;
    MoveTreeActions actions_;
    // Flow layout state.
    float flowRight_ = 0.0f;
    float flowIndent_ = 0.0f;
    bool flowLineStart_ = true;
};

}  // namespace ui

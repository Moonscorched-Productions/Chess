#pragma once

#include <optional>
#include <vector>

#include <imgui.h>

#include "chess/Game.h"
#include "chess/Review.h"

namespace app {
class Assets;
}

namespace ui {

struct BoardArrow {
    core::Square from = core::kNoSquare;
    core::Square to = core::kNoSquare;
    ImU32 color = IM_COL32(21, 120, 27, 190);
};

struct BoardBadge {
    core::Square square = core::kNoSquare;
    core::MoveQuality quality = core::MoveQuality::None;
};

// A translucent colour laid over a square, e.g. for a heatmap.
struct SquareTint {
    core::Square square = core::kNoSquare;
    ImU32 color = 0;
};

struct BoardOptions {
    bool flipped = false;      // true: Black at the bottom
    bool interactive = true;   // false: the user cannot move pieces right now
    bool showCoordinates = true;
    bool showLegalMoves = true;
    bool highlightLastMove = true;
    ImU32 lightSquare = IM_COL32(240, 217, 181, 255);
    ImU32 darkSquare = IM_COL32(181, 136, 99, 255);
    std::vector<BoardArrow> arrows;  // drawn over the pieces, e.g. engine suggestions
    std::vector<BoardBadge> badges;  // move-quality badges on the corner of a square
    std::vector<core::Square> highlights;  // squares to point out, e.g. a hint
    std::vector<SquareTint> tints;         // drawn on the squares, below the pieces
    bool hidePieces = false;               // blindfold: the pieces are there but not drawn
};

// Draws a position and turns mouse input into move requests.
// It never changes the game itself: the caller decides what to do with a returned move.
class BoardView {
public:
    // Draws the board as an ImGui item of `size` x `size` pixels at the current cursor position.
    // Returns the move the user completed this frame, if any.
    std::optional<core::Move> draw(const core::Game& game, const app::Assets& assets, const BoardOptions& options,
                                   float size);

    // Drops any selection, drag or pending promotion (call when the position changes externally).
    void clearInteraction();
    // Removes the arrows and square marks the user drew with the right mouse button.
    void clearAnnotations();

private:
    core::Square selected_ = core::kNoSquare;
    bool dragging_ = false;
    bool deselectOnRelease_ = false;
    // User annotations.
    std::vector<core::Square> marks_;
    std::vector<BoardArrow> userArrows_;
    core::Square markStart_ = core::kNoSquare;
    // Promotion waiting for the user to pick a piece.
    core::Square promotionFrom_ = core::kNoSquare;
    core::Square promotionTo_ = core::kNoSquare;
};

// Draws a single piece into a rectangle (used by the board and by the captured-pieces rows).
void drawPiece(ImDrawList* drawList, const app::Assets& assets, core::Piece piece, ImVec2 topLeft, float size,
               ImU32 tint = IM_COL32_WHITE);

}  // namespace ui

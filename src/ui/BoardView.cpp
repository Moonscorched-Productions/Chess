#include "ui/BoardView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "app/Assets.h"
#include "ui/QualityBadge.h"

namespace ui {

namespace {

using core::Square;

constexpr ImU32 kLastMoveTint = IM_COL32(155, 199, 0, 105);
constexpr ImU32 kSelectedTint = IM_COL32(20, 85, 30, 130);
constexpr ImU32 kCheckTint = IM_COL32(220, 30, 30, 170);
constexpr ImU32 kHintColor = IM_COL32(20, 85, 30, 120);
constexpr ImU32 kHoverTint = IM_COL32(20, 85, 30, 70);

constexpr std::array<core::PieceType, 4> kPromotionChoices = {core::PieceType::Queen, core::PieceType::Knight,
                                                              core::PieceType::Rook, core::PieceType::Bishop};

struct Layout {
    ImVec2 origin;
    float square = 0.0f;
    bool flipped = false;

    ImVec2 topLeft(Square sq) const {
        const int file = core::fileOf(sq);
        const int rank = core::rankOf(sq);
        const int column = flipped ? 7 - file : file;
        const int row = flipped ? rank : 7 - rank;
        return ImVec2(origin.x + static_cast<float>(column) * square, origin.y + static_cast<float>(row) * square);
    }

    Square squareAt(ImVec2 point) const {
        const float x = (point.x - origin.x) / square;
        const float y = (point.y - origin.y) / square;
        if (x < 0.0f || y < 0.0f || x >= 8.0f || y >= 8.0f) return core::kNoSquare;
        const int column = static_cast<int>(x);
        const int row = static_cast<int>(y);
        const int file = flipped ? 7 - column : column;
        const int rank = flipped ? row : 7 - row;
        return core::makeSquare(file, rank);
    }
};

constexpr ImU32 kUserArrowColor = IM_COL32(21, 120, 27, 190);

void drawArrow(ImDrawList* drawList, const Layout& layout, const BoardArrow& arrow) {
    if (!core::isValidSquare(arrow.from) || !core::isValidSquare(arrow.to) || arrow.from == arrow.to) return;
    const float sq = layout.square;
    const ImVec2 a = layout.topLeft(arrow.from);
    const ImVec2 b = layout.topLeft(arrow.to);
    const ImVec2 from(a.x + sq * 0.5f, a.y + sq * 0.5f);
    const ImVec2 to(b.x + sq * 0.5f, b.y + sq * 0.5f);
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0f) return;
    const ImVec2 dir(dx / length, dy / length);
    const ImVec2 normal(-dir.y, dir.x);

    const float headLength = sq * 0.36f;
    const float headWidth = sq * 0.22f;
    const float thickness = sq * 0.15f;
    const ImVec2 start(from.x + dir.x * sq * 0.3f, from.y + dir.y * sq * 0.3f);
    const ImVec2 tip(to.x + dir.x * sq * 0.1f, to.y + dir.y * sq * 0.1f);
    const ImVec2 neck(tip.x - dir.x * headLength, tip.y - dir.y * headLength);

    drawList->AddLine(start, neck, arrow.color, thickness);
    drawList->AddTriangleFilled(tip, ImVec2(neck.x + normal.x * headWidth, neck.y + normal.y * headWidth),
                                ImVec2(neck.x - normal.x * headWidth, neck.y - normal.y * headWidth), arrow.color);
}

char pieceLetter(core::Piece piece) {
    const char letters[] = {'P', 'N', 'B', 'R', 'Q', 'K'};
    const char letter = letters[static_cast<int>(piece.type)];
    return piece.color == core::Color::White ? letter : static_cast<char>(letter - 'A' + 'a');
}

bool isLightSquare(Square sq) { return ((core::fileOf(sq) + core::rankOf(sq)) & 1) != 0; }

// Squares of the promotion picker: starts on the promotion square and runs towards the pawn's own side.
std::array<Square, 4> promotionSquares(Square to) {
    const int step = core::rankOf(to) == 7 ? -8 : 8;
    return {to, to + step, to + 2 * step, to + 3 * step};
}

}  // namespace

void drawPiece(ImDrawList* drawList, const app::Assets& assets, core::Piece piece, ImVec2 topLeft, float size, ImU32 tint) {
    if (piece.empty()) return;
    if (assets.hasPieces()) {
        ImVec2 uv0, uv1;
        assets.pieceUv(piece, uv0, uv1);
        drawList->AddImage(assets.piecesTexture(), topLeft, ImVec2(topLeft.x + size, topLeft.y + size), uv0, uv1, tint);
        return;
    }
    // Fallback when the piece image is missing: a letter on a disc.
    const ImVec2 center(topLeft.x + size * 0.5f, topLeft.y + size * 0.5f);
    const bool white = piece.color == core::Color::White;
    drawList->AddCircleFilled(center, size * 0.36f, white ? IM_COL32(245, 245, 245, 255) : IM_COL32(40, 40, 40, 255));
    const char text[2] = {pieceLetter(piece), '\0'};
    const float fontSize = size * 0.5f;
    const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, 1000.0f, 0.0f, text);
    drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                      white ? IM_COL32(30, 30, 30, 255) : IM_COL32(235, 235, 235, 255), text);
}

void BoardView::clearAnnotations() {
    marks_.clear();
    userArrows_.clear();
    markStart_ = core::kNoSquare;
}

void BoardView::clearInteraction() {
    selected_ = core::kNoSquare;
    dragging_ = false;
    deselectOnRelease_ = false;
    promotionFrom_ = core::kNoSquare;
    promotionTo_ = core::kNoSquare;
}

std::optional<core::Move> BoardView::draw(const core::Game& game, const app::Assets& assets, const BoardOptions& options,
                                          float size) {
    std::optional<core::Move> result;

    Layout layout;
    layout.origin = ImGui::GetCursorScreenPos();
    layout.square = std::floor(std::max(size, 80.0f) / 8.0f);
    layout.flipped = options.flipped;
    const float boardSize = layout.square * 8.0f;

    ImGui::InvisibleButton("##board", ImVec2(boardSize, boardSize));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const Square hoverSquare = hovered ? layout.squareAt(mouse) : core::kNoSquare;
    const bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    const bool canInteract = options.interactive && !game.isOver();
    if (!canInteract) clearInteraction();

    const core::Color mover = game.sideToMove();
    const bool promotionPending = promotionFrom_ != core::kNoSquare;

    // Legal targets for the selected piece.
    std::vector<core::Move> selectedMoves;
    if (canInteract && selected_ != core::kNoSquare) {
        selectedMoves = game.legalMovesFrom(selected_);
        if (game.pieceAt(selected_).empty() || game.pieceAt(selected_).color != mover) {
            selected_ = core::kNoSquare;
            dragging_ = false;
            selectedMoves.clear();
        }
    }
    const auto isTarget = [&](Square sq) {
        return std::any_of(selectedMoves.begin(), selectedMoves.end(), [sq](const core::Move& m) { return m.to == sq; });
    };

    // Either completes the move or opens the promotion picker.
    const auto tryMove = [&](Square from, Square to) {
        if (game.isPromotion(from, to)) {
            promotionFrom_ = from;
            promotionTo_ = to;
        } else {
            result = core::Move{from, to, core::PieceType::None};
        }
        selected_ = core::kNoSquare;
        dragging_ = false;
    };

    // --- Input ---------------------------------------------------------------
    if (canInteract && promotionPending) {
        if (pressed) {
            const std::array<Square, 4> squares = promotionSquares(promotionTo_);
            for (std::size_t i = 0; i < squares.size(); ++i) {
                if (hoverSquare == squares[i]) result = core::Move{promotionFrom_, promotionTo_, kPromotionChoices[i]};
            }
            promotionFrom_ = core::kNoSquare;
            promotionTo_ = core::kNoSquare;
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            promotionFrom_ = core::kNoSquare;  // clicked outside the board
            promotionTo_ = core::kNoSquare;
        }
    } else if (canInteract) {
        if (pressed && hoverSquare != core::kNoSquare) {
            const core::Piece piece = game.pieceAt(hoverSquare);
            if (selected_ != core::kNoSquare && isTarget(hoverSquare)) {
                tryMove(selected_, hoverSquare);
            } else if (!piece.empty() && piece.color == mover) {
                deselectOnRelease_ = selected_ == hoverSquare;
                selected_ = hoverSquare;
                dragging_ = true;
            } else {
                selected_ = core::kNoSquare;
            }
        } else if (released && dragging_) {
            dragging_ = false;
            const Square target = layout.squareAt(mouse);
            if (target != core::kNoSquare && target != selected_ && isTarget(target)) {
                tryMove(selected_, target);
            } else if (target == selected_ && deselectOnRelease_) {
                selected_ = core::kNoSquare;
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            selected_ = core::kNoSquare;
            dragging_ = false;
        }
    }

    // Right button: click a square to mark it, drag between squares to draw an arrow.
    // A left click anywhere on the board wipes the marks.
    if (pressed && (!marks_.empty() || !userArrows_.empty())) clearAnnotations();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) markStart_ = hoverSquare;
    if (markStart_ != core::kNoSquare && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const Square end = layout.squareAt(mouse);
        if (end == markStart_) {
            const auto it = std::find(marks_.begin(), marks_.end(), end);
            if (it != marks_.end()) {
                marks_.erase(it);
            } else {
                marks_.push_back(end);
            }
        } else if (end != core::kNoSquare) {
            const BoardArrow arrow{markStart_, end, kUserArrowColor};
            const auto it = std::find_if(userArrows_.begin(), userArrows_.end(), [&](const BoardArrow& a) {
                return a.from == arrow.from && a.to == arrow.to;
            });
            if (it != userArrows_.end()) {
                userArrows_.erase(it);
            } else {
                userArrows_.push_back(arrow);
            }
        }
        markStart_ = core::kNoSquare;
    }

    // The selection may have changed above.
    if (selected_ == core::kNoSquare) {
        selectedMoves.clear();
    } else if (selectedMoves.empty() || selectedMoves.front().from != selected_) {
        selectedMoves = game.legalMovesFrom(selected_);
    }

    // --- Drawing -------------------------------------------------------------
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float sq = layout.square;
    const auto fill = [&](Square square, ImU32 color) {
        const ImVec2 p = layout.topLeft(square);
        drawList->AddRectFilled(p, ImVec2(p.x + sq, p.y + sq), color);
    };

    for (Square square = 0; square < 64; ++square) fill(square, isLightSquare(square) ? options.lightSquare : options.darkSquare);

    if (options.highlightLastMove) {
        if (const std::optional<core::Move> last = game.lastMove()) {
            fill(last->from, kLastMoveTint);
            fill(last->to, kLastMoveTint);
        }
    }
    if (selected_ != core::kNoSquare) fill(selected_, kSelectedTint);
    for (const Square square : options.highlights)
        if (core::isValidSquare(square)) fill(square, IM_COL32(40, 120, 210, 130));
    if (game.inCheck()) {
        const ImVec2 p = layout.topLeft(game.kingSquare(mover));
        const ImVec2 center(p.x + sq * 0.5f, p.y + sq * 0.5f);
        drawList->AddRectFilled(p, ImVec2(p.x + sq, p.y + sq), IM_COL32(220, 30, 30, 70));
        drawList->AddCircleFilled(center, sq * 0.46f, kCheckTint, 32);
    }

    if (options.showCoordinates) {
        ImFont* font = ImGui::GetFont();
        const float fontSize = std::clamp(sq * 0.2f, 10.0f, 18.0f);
        const float pad = sq * 0.05f;
        for (int i = 0; i < 8; ++i) {
            // Rank numbers down the left edge, file letters along the bottom edge.
            const Square rankSquare = layout.squareAt(ImVec2(layout.origin.x + 1.0f, layout.origin.y + (static_cast<float>(i) + 0.5f) * sq));
            const Square fileSquare = layout.squareAt(ImVec2(layout.origin.x + (static_cast<float>(i) + 0.5f) * sq, layout.origin.y + boardSize - 1.0f));
            const char rankText[2] = {static_cast<char>('1' + core::rankOf(rankSquare)), '\0'};
            const char fileText[2] = {static_cast<char>('a' + core::fileOf(fileSquare)), '\0'};
            const ImVec2 rankPos = layout.topLeft(rankSquare);
            const ImVec2 filePos = layout.topLeft(fileSquare);
            const ImVec2 fileSize = font->CalcTextSizeA(fontSize, 1000.0f, 0.0f, fileText);
            drawList->AddText(font, fontSize, ImVec2(rankPos.x + pad, rankPos.y + pad),
                              isLightSquare(rankSquare) ? options.darkSquare : options.lightSquare, rankText);
            drawList->AddText(font, fontSize, ImVec2(filePos.x + sq - fileSize.x - pad, filePos.y + sq - fileSize.y - pad),
                              isLightSquare(fileSquare) ? options.darkSquare : options.lightSquare, fileText);
        }
    }

    const bool showDrag = dragging_ && selected_ != core::kNoSquare && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f);

    for (Square square = 0; square < 64; ++square) {
        const core::Piece piece = game.pieceAt(square);
        if (piece.empty()) continue;
        if (showDrag && square == selected_) continue;
        if (promotionPending && square == promotionFrom_) continue;
        drawPiece(drawList, assets, piece, layout.topLeft(square), sq);
    }

    if (options.showLegalMoves) {
        for (const core::Move& move : selectedMoves) {
            const ImVec2 p = layout.topLeft(move.to);
            const ImVec2 center(p.x + sq * 0.5f, p.y + sq * 0.5f);
            const bool capture = !game.pieceAt(move.to).empty();
            if (capture) {
                drawList->AddCircle(center, sq * 0.44f, kHintColor, 40, sq * 0.09f);
            } else {
                drawList->AddCircleFilled(center, sq * 0.15f, kHintColor, 24);
            }
        }
    }
    if (dragging_ && hoverSquare != core::kNoSquare && hoverSquare != selected_ && isTarget(hoverSquare)) {
        fill(hoverSquare, kHoverTint);
    }

    for (const Square square : marks_) {
        const ImVec2 p = layout.topLeft(square);
        drawList->AddCircle(ImVec2(p.x + sq * 0.5f, p.y + sq * 0.5f), sq * 0.44f, kUserArrowColor, 40, sq * 0.07f);
    }
    for (const BoardArrow& arrow : options.arrows) drawArrow(drawList, layout, arrow);
    for (const BoardArrow& arrow : userArrows_) drawArrow(drawList, layout, arrow);
    for (const BoardBadge& badge : options.badges) {
        if (!core::isValidSquare(badge.square)) continue;
        // Top-right corner of the square, slightly overlapping its edge.
        const ImVec2 p = layout.topLeft(badge.square);
        const float radius = sq * 0.19f;
        const ImVec2 center(std::min(p.x + sq - radius * 0.45f, layout.origin.x + boardSize - radius - 1.0f),
                            std::max(p.y + radius * 0.45f, layout.origin.y + radius + 1.0f));
        drawQualityBadge(drawList, center, radius, badge.quality);
    }
    if (markStart_ != core::kNoSquare && hoverSquare != core::kNoSquare && hoverSquare != markStart_) {
        drawArrow(drawList, layout, BoardArrow{markStart_, hoverSquare, kUserArrowColor});
    }

    if (showDrag) {
        const float dragSize = sq * 1.1f;
        drawPiece(ImGui::GetForegroundDrawList(), assets, game.pieceAt(selected_),
                  ImVec2(mouse.x - dragSize * 0.5f, mouse.y - dragSize * 0.5f), dragSize);
    }

    if (promotionFrom_ != core::kNoSquare) {
        drawList->AddRectFilled(layout.origin, ImVec2(layout.origin.x + boardSize, layout.origin.y + boardSize),
                                IM_COL32(0, 0, 0, 120));
        const std::array<Square, 4> squares = promotionSquares(promotionTo_);
        for (std::size_t i = 0; i < squares.size(); ++i) {
            const ImVec2 p = layout.topLeft(squares[i]);
            const ImVec2 center(p.x + sq * 0.5f, p.y + sq * 0.5f);
            const bool hot = hoverSquare == squares[i];
            drawList->AddCircleFilled(center, sq * (hot ? 0.5f : 0.46f), hot ? IM_COL32(215, 150, 60, 255) : IM_COL32(225, 225, 225, 255), 40);
            drawPiece(drawList, assets, core::Piece{kPromotionChoices[i], mover}, p, sq);
        }
    }

    if (canInteract && hoverSquare != core::kNoSquare) {
        const core::Piece piece = game.pieceAt(hoverSquare);
        const bool grabbable = !piece.empty() && piece.color == mover;
        if (promotionFrom_ == core::kNoSquare && (grabbable || isTarget(hoverSquare) || dragging_))
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    return result;
}

}  // namespace ui

#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "chess/Types.h"

namespace core {

// Static facts about a position, read from its FEN: who attacks what, which pieces
// can be won, how the pawns stand. Used by board overlays, explanations and puzzle themes.
class Position {
public:
    // An invalid FEN gives an empty board.
    explicit Position(std::string_view fen);

    Piece at(Square square) const { return isValidSquare(square) ? board_[static_cast<std::size_t>(square)] : Piece{}; }
    Color sideToMove() const { return sideToMove_; }
    Square kingSquare(Color color) const;
    // Pieces on the board, kings and pawns included.
    int pieceCount() const;

    // Squares the piece on `square` attacks (including squares held by its own side).
    std::vector<Square> attacksFrom(Square square) const;
    // Squares from which pieces of `color` attack `square`.
    std::vector<Square> attackers(Square square, Color color) const;
    int attackerCount(Square square, Color color) const { return static_cast<int>(attackers(square, color).size()); }

    // Pieces of `owner` (kings excluded) that the other side can win: attacked by
    // something cheaper, or attacked more often than defended.
    std::vector<Square> hangingPieces(Color owner) const;

    // How many squares the piece on `square` can go to or capture on, ignoring pins.
    int mobility(Square square) const;

    struct Pawns {
        std::vector<Square> isolated;  // no friendly pawn on a neighbouring file
        std::vector<Square> doubled;   // behind another friendly pawn on the same file
        std::vector<Square> passed;    // no enemy pawn ahead on its own or a neighbouring file
    };
    Pawns pawns(Color color) const;

private:
    std::array<Piece, 64> board_{};
    Color sideToMove_ = Color::White;
};

// Conventional piece values: pawn 1, knight and bishop 3, rook 5, queen 9 (king 0).
int pieceValue(PieceType type);
// "pawn", "knight", ... ("piece" for None).
const char* pieceName(PieceType type);

// Things worth knowing about one side of a position, as short sentences:
// pieces that can be won, pawn weaknesses, passed pawns, an exposed king.
std::vector<std::string> positionNotes(std::string_view fen, Color color);

}  // namespace core

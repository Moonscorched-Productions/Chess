#include "chess/Endgames.h"

#include <cstring>

namespace core {

const std::vector<EndgamePosition>& endgamePositions() {
    static const std::vector<EndgamePosition> positions = {
        // --- Basic checkmates -------------------------------------------------------
        {"kq-k", "Basic checkmates", "Queen against king", "8/8/8/4k3/8/8/8/KQ6 w - - 0 1", Color::White, EndgameGoal::Win,
         "Drive the king to the edge with the queen a knight's move away, then bring your own king."},
        {"krr-k", "Basic checkmates", "Two rooks against king", "8/8/8/4k3/8/8/8/KRR5 w - - 0 1", Color::White,
         EndgameGoal::Win, "The rooks take turns cutting off one rank after another: the ladder mate."},
        {"kr-k", "Basic checkmates", "Rook against king", "8/8/8/4k3/8/8/8/KR6 w - - 0 1", Color::White, EndgameGoal::Win,
         "Shrink the box with the rook and use your king to take the opposition."},
        {"kbb-k", "Basic checkmates", "Two bishops against king", "8/8/8/4k3/8/8/8/KBB5 w - - 0 1", Color::White,
         EndgameGoal::Win, "Keep the bishops side by side to build a wall and force the king into a corner."},
        {"kbn-k", "Basic checkmates", "Bishop and knight against king", "8/8/8/4k3/8/8/8/KBN5 w - - 0 1", Color::White,
         EndgameGoal::Win, "The hardest basic mate: it only works in a corner of the bishop's colour."},

        // --- King and pawn ----------------------------------------------------------
        {"kp-sixth", "King and pawn", "King on the sixth rank", "4k3/8/4K3/4P3/8/8/8/8 w - - 0 1", Color::White,
         EndgameGoal::Win, "With the king on the sixth in front of its pawn the win does not depend on whose move it is."},
        {"kp-start", "King and pawn", "Lead with the king", "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", Color::White, EndgameGoal::Win,
         "March the king ahead of the pawn and keep the pawn moves in reserve to win the opposition."},
        {"kp-square", "King and pawn", "The rule of the square", "8/8/8/8/k7/8/7P/7K w - - 0 1", Color::White,
         EndgameGoal::Win, "The enemy king is outside the pawn's square, so the pawn simply runs."},
        {"kp-breakthrough", "King and pawn", "Pawn breakthrough", "7k/ppp5/8/PPP5/8/8/8/7K w - - 0 1", Color::White,
         EndgameGoal::Win, "Sacrifice two pawns to let the third one through. Start in the middle."},

        // --- Opposition -------------------------------------------------------------
        {"opp-defend", "Opposition", "Hold with the opposition", "8/8/8/4k3/4P3/4K3/8/8 b - - 0 1", Color::Black,
         EndgameGoal::Draw, "Step straight back and meet the advancing king face to face."},
        {"opp-fifth", "Opposition", "Defend the fifth rank", "8/4k3/8/4K3/4P3/8/8/8 w - - 0 1", Color::Black,
         EndgameGoal::Draw, "You have the opposition. Mirror the white king and never let it reach the sixth with tempo."},
        {"opp-rookpawn", "Opposition", "The rook pawn", "7k/8/7K/7P/8/8/8/8 b - - 0 1", Color::Black, EndgameGoal::Draw,
         "Stay in the corner: a rook pawn cannot win against a king in front of it."},

        // --- Rook endings -----------------------------------------------------------
        {"rook-lucena", "Rook endings", "The Lucena position", "1K1k4/1P6/8/8/8/8/r7/2R5 w - - 0 1", Color::White,
         EndgameGoal::Win, "Check the king away, then build a bridge: bring the rook to the fourth rank to shield your king."},
        {"rook-philidor", "Rook endings", "The Philidor defence", "4k3/7R/r7/3KP3/8/8/8/8 b - - 0 1", Color::Black,
         EndgameGoal::Draw, "Keep the rook on the sixth rank. Once the pawn advances, go to the back and check from behind."},

        // --- Queen endings ----------------------------------------------------------
        {"queen-pawn", "Queen endings", "Queen against a pawn on the seventh", "8/1Q6/8/8/8/8/4pk2/7K w - - 0 1", Color::White,
         EndgameGoal::Win, "Check until the king has to step in front of its pawn, then bring your king one step closer."},
        {"queen-rook", "Queen endings", "Queen against rook", "1k6/1r6/2K5/Q7/8/8/8/8 w - - 0 1", Color::White, EndgameGoal::Win,
         "Philidor's position wins if it is Black's move. Lose a tempo with queen checks (e5, a1, back to a5)."},
    };
    return positions;
}

const EndgamePosition* findEndgame(const char* id) {
    for (const EndgamePosition& position : endgamePositions()) {
        if (std::strcmp(position.id, id) == 0) return &position;
    }
    return nullptr;
}

}  // namespace core

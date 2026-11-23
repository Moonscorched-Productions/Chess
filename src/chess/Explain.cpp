#include "chess/Explain.h"

#include <algorithm>
#include <cstdlib>
#include <optional>

#include "chess/Position.h"

namespace core {

namespace {

bool contains(const std::vector<Square>& squares, Square square) {
    return std::find(squares.begin(), squares.end(), square) != squares.end();
}

// Plays a UCI move in a position and returns how it was recorded, if it is legal.
std::optional<MoveRecord> playOn(const std::string& fen, const std::string& uci) {
    if (uci.empty()) return std::nullopt;
    Game game;
    game.setEnforceDraws(false);
    if (!game.reset(fen) || !game.makeUciMove(uci)) return std::nullopt;
    return game.history().back();
}

std::string thePiece(PieceType type, Square square) {
    return std::string("the ") + pieceName(type) + " on " + squareName(square);
}

bool matesFor(const std::optional<Eval>& eval, Color color) {
    return eval && eval->mate && *eval->mate != 0 && (*eval->mate > 0) == (color == Color::White);
}

int mateDistance(const std::optional<Eval>& eval) { return eval && eval->mate ? std::abs(*eval->mate) : 0; }

// True if taking on `record.move.to` wins material: the piece taken was worth more
// than the one taking, or nothing can take back.
bool winsMaterial(const MoveRecord& record, const Position& after) {
    if (record.captured.empty()) return false;
    if (pieceValue(record.captured.type) > pieceValue(record.moved.type)) return true;
    return after.attackerCount(record.move.to, opposite(record.moved.color)) == 0;
}

// What a move does on the board, as sentences.
void describeEffects(const MoveRecord& record, const Position& after, const std::optional<Eval>& evalAfter,
                     std::vector<std::string>& out) {
    const Color mover = record.moved.color;
    if (!record.san.empty() && record.san.back() == '#') {
        out.push_back("Checkmate.");
        return;
    }
    if (record.isCastling) out.push_back("Castles and brings the king to safety.");
    if (!record.captured.empty()) {
        if (winsMaterial(record, after)) {
            out.push_back(std::string("Wins the ") + pieceName(record.captured.type) + ".");
        } else if (pieceValue(record.captured.type) == pieceValue(record.moved.type)) {
            out.push_back(std::string("Trades ") + pieceName(record.moved.type) + "s.");
        }
    }

    // Threats made by the piece that moved: enemy pieces it now attacks that are worth
    // more than it, or that nobody defends.
    const Piece moved = after.at(record.move.to);
    std::vector<Square> targets;
    for (const Square target : after.attacksFrom(record.move.to)) {
        const Piece piece = after.at(target);
        if (piece.empty() || piece.color == mover || piece.type == PieceType::King) continue;
        const bool worthMore = pieceValue(piece.type) > pieceValue(moved.type);
        const bool loose = after.attackerCount(target, opposite(mover)) == 0;
        if (worthMore || loose) targets.push_back(target);
    }
    if (targets.size() + (record.isCheck ? 1 : 0) >= 2) {
        std::string text = "Forks ";
        if (record.isCheck) text += "the king";
        for (std::size_t i = 0; i < targets.size(); ++i) {
            const bool first = i == 0 && !record.isCheck;
            const bool last = i + 1 == targets.size();
            text += first ? "" : last ? " and " : ", ";
            text += thePiece(after.at(targets[i]).type, targets[i]);
        }
        out.push_back(text + ".");
    } else if (targets.size() == 1) {
        out.push_back("Attacks " + thePiece(after.at(targets[0]).type, targets[0]) + (record.isCheck ? " with check." : "."));
    } else if (record.isCheck) {
        out.push_back("Gives check.");
    }

    if (matesFor(evalAfter, mover)) out.push_back("Forces mate in " + std::to_string(mateDistance(evalAfter)) + ".");
}

}  // namespace

std::vector<std::string> explainMove(const GameTree& tree, NodeId id, MoveQuality quality) {
    std::vector<std::string> out;
    if (!tree.contains(id) || id == kRootNode) return out;
    const TreeNode& node = tree.node(id);
    const TreeNode& before = tree.node(node.parent);
    const MoveRecord& record = node.record;
    const Color mover = tree.mover(id);
    const Position positionBefore(before.fen);
    const Position positionAfter(node.fen);

    const bool weak = quality == MoveQuality::Inaccuracy || quality == MoveQuality::Mistake || quality == MoveQuality::Blunder;
    if (quality == MoveQuality::Best) out.push_back("The engine's first choice.");
    describeEffects(record, positionAfter, node.eval, out);

    if (weak) {
        // What the move allows: the opponent's best answer and pieces left loose.
        const std::optional<MoveRecord> reply = playOn(node.fen, node.bestMove);
        bool replyTakes = false;
        if (matesFor(node.eval, opposite(mover))) {
            std::string text = "Allows a forced mate in " + std::to_string(mateDistance(node.eval));
            if (reply) text += ", starting with " + reply->san;
            out.push_back(text + ".");
        } else if (reply && !reply->captured.empty()) {
            replyTakes = true;
            if (reply->move.to == record.move.to) {
                out.push_back("The " + std::string(pieceName(reply->captured.type)) + " can be taken at once: " + reply->san + ".");
            } else {
                out.push_back("Allows " + reply->san + ", which wins the " + pieceName(reply->captured.type) + ".");
            }
        } else if (reply) {
            out.push_back("The strongest answer is " + reply->san + ".");
        }

        const std::vector<Square> looseBefore = positionBefore.hangingPieces(mover);
        for (const Square square : positionAfter.hangingPieces(mover)) {
            if (replyTakes && reply->move.to == square) continue;  // already said
            const bool wasLoose = square != record.move.to && contains(looseBefore, square);
            out.push_back((wasLoose ? "Leaves " : "Puts ") + thePiece(positionAfter.at(square).type, square) +
                          (wasLoose ? " hanging." : " where it can be won."));
            break;  // one is enough
        }
        if (matesFor(before.eval, mover) && !matesFor(node.eval, mover)) {
            out.push_back("There was a forced mate in " + std::to_string(mateDistance(before.eval)) + ".");
        }
    }

    // Why the engine's move was stronger.
    if (quality != MoveQuality::Best && !before.bestMove.empty() && before.bestMove != record.uci) {
        if (const std::optional<MoveRecord> best = playOn(before.fen, before.bestMove)) {
            Game scratch;
            scratch.setEnforceDraws(false);
            std::string fenAfterBest;
            if (scratch.reset(before.fen) && scratch.makeUciMove(before.bestMove)) fenAfterBest = scratch.fen();
            const Position afterBest(fenAfterBest);

            std::string text = (weak ? "Better was " : "The engine preferred ") + best->san;
            if (!best->san.empty() && best->san.back() == '#') {
                text += ", which is checkmate";
            } else if (matesFor(before.eval, mover)) {
                text += ", which forces mate";
            } else if (winsMaterial(*best, afterBest)) {
                text += std::string(", which wins the ") + pieceName(best->captured.type);
            } else if (contains(positionBefore.hangingPieces(mover), best->move.from) &&
                       !contains(afterBest.hangingPieces(mover), best->move.to)) {
                text += std::string(", which saves the ") + pieceName(best->moved.type);
            } else if (best->isCastling) {
                text += ", getting the king to safety";
            } else if (best->isCheck) {
                text += ", with check";
            }
            out.push_back(text + ".");
        }
    }
    return out;
}

}  // namespace core

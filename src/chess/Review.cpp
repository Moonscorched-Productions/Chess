#include "chess/Review.h"

#include <algorithm>

namespace core {

std::optional<double> expectedScoreLoss(const GameTree& tree, NodeId id) {
    if (!tree.contains(id) || id == kRootNode) return std::nullopt;
    const TreeNode& node = tree.node(id);
    const TreeNode& before = tree.node(node.parent);
    if (!node.eval || !before.eval) return std::nullopt;

    const double scoreBefore = before.eval->whiteWinPercent();
    const double scoreAfter = node.eval->whiteWinPercent();
    const double loss = tree.mover(id) == Color::White ? scoreBefore - scoreAfter : scoreAfter - scoreBefore;
    // Deeper search after the move can make it look better than "best"; that is not a loss.
    return std::max(loss, 0.0);
}

MoveQuality classifyMove(const GameTree& tree, NodeId id, const QualityThresholds& thresholds) {
    const std::optional<double> loss = expectedScoreLoss(tree, id);
    if (!loss) return MoveQuality::None;

    const TreeNode& node = tree.node(id);
    const std::string& engineChoice = tree.node(node.parent).bestMove;
    if (!engineChoice.empty() && engineChoice == node.record.uci) return MoveQuality::Best;

    if (*loss <= thresholds.excellent) return MoveQuality::Excellent;
    if (*loss <= thresholds.good) return MoveQuality::Good;
    if (*loss <= thresholds.inaccuracy) return MoveQuality::Inaccuracy;
    if (*loss <= thresholds.mistake) return MoveQuality::Mistake;
    return MoveQuality::Blunder;
}

const char* qualityName(MoveQuality quality) {
    switch (quality) {
        case MoveQuality::Best: return "Best move";
        case MoveQuality::Excellent: return "Excellent";
        case MoveQuality::Good: return "Good";
        case MoveQuality::Inaccuracy: return "Inaccuracy";
        case MoveQuality::Mistake: return "Mistake";
        case MoveQuality::Blunder: return "Blunder";
        case MoveQuality::None: break;
    }
    return "";
}

}  // namespace core

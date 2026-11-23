#include "chess/Review.h"

#include <algorithm>
#include <cmath>
#include <string_view>

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

double moveAccuracy(double expectedScoreLoss) {
    const double loss = std::max(expectedScoreLoss, 0.0);
    const double accuracy = 103.1668100711649 * std::exp(-0.04354415386753951 * loss) - 3.166924740191411;
    return std::clamp(accuracy, 0.0, 100.0);
}

GamePhase gamePhase(std::string_view fen, int ply) {
    int pieces = 0;
    for (const char c : fen) {
        if (c == ' ') break;
        if (std::string_view("qrbnQRBN").find(c) != std::string_view::npos) ++pieces;
    }
    if (pieces <= 6) return GamePhase::Endgame;
    return ply <= 20 ? GamePhase::Opening : GamePhase::Middlegame;
}

const char* phaseName(GamePhase phase) {
    switch (phase) {
        case GamePhase::Opening: return "Opening";
        case GamePhase::Middlegame: return "Middlegame";
        case GamePhase::Endgame: return "Endgame";
    }
    return "";
}

namespace {

// Collects the move accuracies of one player and turns them into a SideReview.
struct SideTally {
    SideReview review;
    double sum = 0.0;
    double inverseSum = 0.0;
    std::array<double, kPhaseCount> phaseSum{};

    void add(MoveQuality quality, double loss, GamePhase phase) {
        const double accuracy = moveAccuracy(loss);
        ++review.moves;
        ++review.counts[static_cast<std::size_t>(quality)];
        sum += accuracy;
        // The harmonic mean punishes single disasters; the floor of one percent keeps it finite.
        inverseSum += 1.0 / std::max(accuracy, 1.0);
        ++review.phases[static_cast<std::size_t>(phase)].moves;
        phaseSum[static_cast<std::size_t>(phase)] += accuracy;
    }

    SideReview finish() {
        if (review.moves > 0) {
            const double mean = sum / review.moves;
            const double harmonic = review.moves / inverseSum;
            review.accuracy = (mean + harmonic) * 0.5;
        }
        for (std::size_t i = 0; i < review.phases.size(); ++i) {
            if (review.phases[i].moves > 0) review.phases[i].accuracy = phaseSum[i] / review.phases[i].moves;
        }
        return review;
    }
};

}  // namespace

GameReview reviewGame(const GameTree& tree, const QualityThresholds& thresholds) {
    GameReview result;
    SideTally white;
    SideTally black;

    for (const NodeId id : tree.mainLine()) {
        ++result.totalMoves;
        const MoveQuality quality = classifyMove(tree, id, thresholds);
        const std::optional<double> loss = expectedScoreLoss(tree, id);
        if (quality == MoveQuality::None || !loss) continue;
        ++result.judgedMoves;

        const TreeNode& node = tree.node(id);
        const TreeNode& before = tree.node(node.parent);
        const bool whiteMoved = tree.mover(id) == Color::White;
        (whiteMoved ? white : black).add(quality, *loss, gamePhase(before.fen, node.ply));

        if (quality != MoveQuality::Mistake && quality != MoveQuality::Blunder) continue;
        const double shareBefore = whiteMoved ? before.eval->whiteWinPercent() : 100.0 - before.eval->whiteWinPercent();
        const double shareAfter = whiteMoved ? node.eval->whiteWinPercent() : 100.0 - node.eval->whiteWinPercent();
        const auto matesForMover = [&](const Eval& eval) {
            return eval.mate && *eval.mate != 0 && (*eval.mate > 0) == whiteMoved;
        };

        CriticalMoment moment;
        moment.node = id;
        moment.quality = quality;
        moment.loss = *loss;
        if (matesForMover(*before.eval) && !matesForMover(*node.eval)) {
            moment.kind = MomentKind::MissedMate;
        } else if (shareBefore >= 75.0 && shareAfter < 60.0) {
            moment.kind = MomentKind::MissedWin;
        } else {
            moment.kind = quality == MoveQuality::Blunder ? MomentKind::Blunder : MomentKind::Mistake;
        }
        result.moments.push_back(moment);
    }

    result.white = white.finish();
    result.black = black.finish();
    return result;
}

const char* momentText(MomentKind kind) {
    switch (kind) {
        case MomentKind::Mistake: return "Mistake";
        case MomentKind::Blunder: return "Blunder";
        case MomentKind::MissedWin: return "Let a winning position slip";
        case MomentKind::MissedMate: return "Missed a forced mate";
    }
    return "";
}

void evaluateFinalPosition(GameTree& tree) {
    const NodeId last = tree.lineEnd(kRootNode);
    if (last == kRootNode || tree.node(last).eval) return;
    Game game;
    game.setEnforceDraws(false);
    if (!loadPosition(tree, last, game)) return;

    Eval eval;
    if (game.status() == Status::Checkmate) {
        eval.mate = 0;
        eval.cp = game.sideToMove() == Color::White ? -1 : 1;
    } else if (game.status() != Status::Stalemate) {
        return;
    }
    tree.setEval(last, eval);
}

}  // namespace core

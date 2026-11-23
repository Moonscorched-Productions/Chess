#pragma once

#include "chess/GameTree.h"

namespace core {

// How good a move was, judged by how much of the mover's expected score it gave up.
enum class MoveQuality { None, Best, Excellent, Good, Inaccuracy, Mistake, Blunder };

// Expected-score losses (in percentage points) at which a move drops a category.
struct QualityThresholds {
    double excellent = 2.0;   // up to this: Excellent
    double good = 5.0;        // up to this: Good
    double inaccuracy = 10.0; // up to this: Inaccuracy
    double mistake = 20.0;    // up to this: Mistake; above: Blunder
};

// Expected score (0..100) the move leading to `id` cost the side that played it.
// Returns nullopt unless both the position before and after the move are evaluated.
std::optional<double> expectedScoreLoss(const GameTree& tree, NodeId id);

// Classifies the move leading to `id`. A move that matches the engine's choice for the
// position is Best; otherwise the category follows from the expected score it lost.
// Returns None for the root and for moves without evaluations on both sides.
MoveQuality classifyMove(const GameTree& tree, NodeId id, const QualityThresholds& thresholds = {});

// "Best move", "Blunder", ... ("" for None).
const char* qualityName(MoveQuality quality);

}  // namespace core

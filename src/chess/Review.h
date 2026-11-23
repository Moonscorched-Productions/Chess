#pragma once

#include <array>
#include <string>
#include <vector>

#include "chess/GameTree.h"

namespace core {

// How good a move was, judged by how much of the mover's expected score it gave up.
enum class MoveQuality { None, Best, Excellent, Good, Inaccuracy, Mistake, Blunder };
inline constexpr int kQualityCount = 7;

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

// --- Whole-game review ----------------------------------------------------------

// Accuracy of one move in percent (0..100) for the expected score it lost. A move that
// loses nothing scores 100; the curve is the one Lichess uses.
double moveAccuracy(double expectedScoreLoss);

enum class GamePhase { Opening, Middlegame, Endgame };
inline constexpr int kPhaseCount = 3;

// Rough phase of a position: the first ten moves are the opening, positions with at
// most six pieces besides kings and pawns are the endgame.
GamePhase gamePhase(std::string_view fen, int ply);
const char* phaseName(GamePhase phase);

struct PhaseSummary {
    int moves = 0;
    double accuracy = 0.0;  // average move accuracy; meaningless while moves == 0
};

// One player's part of a reviewed game.
struct SideReview {
    int moves = 0;          // moves that could be judged
    double accuracy = 0.0;  // 0..100
    std::array<int, kQualityCount> counts{};         // indexed by MoveQuality
    std::array<PhaseSummary, kPhaseCount> phases{};  // indexed by GamePhase

    int count(MoveQuality quality) const { return counts[static_cast<std::size_t>(quality)]; }
};

enum class MomentKind {
    Mistake,
    Blunder,
    MissedWin,   // a winning position was let slip
    MissedMate,  // a forced mate was available
};

// A move of the main line that changed the course of the game.
struct CriticalMoment {
    NodeId node = kNoNode;
    MoveQuality quality = MoveQuality::None;
    MomentKind kind = MomentKind::Mistake;
    double loss = 0.0;  // expected score lost, in percentage points
};

struct GameReview {
    SideReview white;
    SideReview black;
    std::vector<CriticalMoment> moments;  // in game order
    int judgedMoves = 0;
    int totalMoves = 0;  // moves of the main line

    const SideReview& side(Color color) const { return color == Color::White ? white : black; }
    bool complete() const { return totalMoves > 0 && judgedMoves == totalMoves; }
};

// Summarizes the main line from the evaluations stored in the tree.
GameReview reviewGame(const GameTree& tree, const QualityThresholds& thresholds = {});

// "Missed a forced mate", "Let a winning position slip", "Blunder", "Mistake".
const char* momentText(MomentKind kind);

// Gives the last position of the main line its evaluation if the game ended there by
// checkmate or stalemate, so no engine is needed for it.
void evaluateFinalPosition(GameTree& tree);

}  // namespace core

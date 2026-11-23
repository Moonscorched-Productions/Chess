#pragma once

#include <optional>
#include <string>
#include <vector>

#include "chess/GameTree.h"
#include "chess/Puzzle.h"
#include "chess/Review.h"

namespace core {

// Theme given to every puzzle made from the user's own games.
inline constexpr const char* kOwnGameTheme = "fromMyGames";

// Positions of a reviewed game that may hold a tactic: the positions right after a
// mistake or blunder that leaves the other side clearly better. Each returned node is
// such a position; the move leading to it becomes the opponent's first move of the puzzle.
std::vector<NodeId> puzzleCandidates(const GameTree& tree, const GameReview& review);

// Decides whether the engine's best move makes a puzzle move for `solver`: it must keep a
// clear advantage while the second-best move (if there is one) does clearly worse.
// Both evaluations are from White's point of view.
bool isOnlyGoodMove(const Eval& best, const std::optional<Eval>& second, Color solver);

// Finishes a generated puzzle: checks that the line is legal, estimates a difficulty
// rating and assigns themes (mate, fork, hanging piece, sacrifice, promotion, length,
// game phase) and a stable id. `fen` is the position before the opponent's move and
// `moves` the whole line in UCI, ending with a solver move. `solverShare` is the
// solver's expected score (0..100) once the line starts.
// Returns nullopt if the line is not a legal puzzle.
std::optional<Puzzle> buildPuzzle(const std::string& fen, const std::vector<std::string>& moves, double solverShare);

}  // namespace core

#pragma once

namespace core {

// "Guess the move": points for one guess, out of kGuessMaxPoints.
inline constexpr int kGuessMaxPoints = 5;

// The move from the game earns full points; another move earns points by how much of the
// guesser's expected score (in percent, 0..100) it keeps compared with the game move.
// A guess the engine likes at least as much as the game move also earns full points.
int guessPoints(bool sameAsGame, double guessScore, double gameScore);

}  // namespace core

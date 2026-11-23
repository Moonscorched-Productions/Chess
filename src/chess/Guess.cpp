#include "chess/Guess.h"

namespace core {

int guessPoints(bool sameAsGame, double guessScore, double gameScore) {
    if (sameAsGame) return kGuessMaxPoints;
    const double loss = gameScore - guessScore;
    if (loss <= 0.0) return kGuessMaxPoints;
    if (loss <= 3.0) return 4;
    if (loss <= 7.0) return 3;
    if (loss <= 12.0) return 2;
    if (loss <= 20.0) return 1;
    return 0;
}

}  // namespace core

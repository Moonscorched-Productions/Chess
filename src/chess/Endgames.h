#pragma once

#include <vector>

#include "chess/Types.h"

namespace core {

enum class EndgameGoal { Win, Draw };

// A training position to be played out against the engine.
struct EndgamePosition {
    const char* id;        // stable key for progress tracking
    const char* category;  // e.g. "Basic checkmates"
    const char* name;
    const char* fen;
    Color userColor;       // the side the user plays
    EndgameGoal goal;      // what the user has to achieve
    const char* hint;      // one sentence on the idea
};

// The built-in endgame positions, grouped by category. Every goal was checked with Stockfish.
const std::vector<EndgamePosition>& endgamePositions();

// Looks a position up by id; nullptr if there is none.
const EndgamePosition* findEndgame(const char* id);

}  // namespace core

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "chess/GameTree.h"

namespace core {

// --- Spaced repetition ---------------------------------------------------------

// Memory state of one thing to remember (here: the repertoire move in one position).
struct CardState {
    int dueDay = 0;        // day number on which the card should be asked again
    int intervalDays = 0;  // current gap between reviews
    double ease = 2.5;     // growth factor of the gap
    int reviews = 0;       // answers given in total
    int streak = 0;        // correct answers in a row
    int lapses = 0;        // wrong answers in total

    bool isNew() const { return reviews == 0; }
    bool isDue(int today) const { return dueDay <= today; }
};

// The card after an answer on day `today` (a simplified SM-2 schedule):
// a correct answer pushes the next review further away, a wrong one brings it back to today.
CardState reviewCard(CardState card, bool correct, int today);

// Days since 1970-01-01 in local time, the "day number" used for scheduling.
int todayNumber();

// --- Repertoire trees ----------------------------------------------------------

// Identifies a position regardless of how it was reached: piece placement, side to
// move and castling rights of a FEN (move counters and en passant are left out).
std::string positionKey(std::string_view fen);

// The positions of a repertoire in which `side` has to know a move: every node where
// it is `side` to move and the tree continues. The root counts if `side` moves first.
std::vector<NodeId> decisionNodes(const GameTree& tree, Color side);

// Every complete line of the tree (root to leaf), as node lists without the root.
// Stops collecting after `limit` lines.
std::vector<std::vector<NodeId>> allLines(const GameTree& tree, std::size_t limit = 5000);

// Which side is to move in the position at `id`.
Color sideToMoveAt(const GameTree& tree, NodeId id);

}  // namespace core

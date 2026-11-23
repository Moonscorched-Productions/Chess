#pragma once

#include <optional>
#include <string>
#include <vector>

#include "chess/Game.h"

namespace core {

// A tactical puzzle in the Lichess format: the first move of `moves` is played by the
// opponent, after which the solver has to find every second move of the line.
struct Puzzle {
    std::string id;
    std::string fen;                 // position before the opponent's first move
    std::vector<std::string> moves;  // UCI
    int rating = 1500;
    std::string themes;  // space separated, e.g. "fork middlegame short"
};

// Splits a space separated move list ("e2e4 e7e5") into its moves.
std::vector<std::string> splitMoves(std::string_view text);

// One attempt at solving a puzzle: checks the solver's moves against the solution.
class PuzzleSession {
public:
    enum class Outcome {
        Ignored,   // nothing to solve (no puzzle, already finished, or an illegal move)
        Wrong,     // a legal move that is not the solution; the position is unchanged
        Correct,   // right move; the opponent's reply is waiting to be played
        Solved,    // right move and the puzzle is complete
    };

    PuzzleSession();

    // Sets up the puzzle and plays the opponent's first move.
    // Returns false if the puzzle's position or moves are not valid.
    bool start(const Puzzle& puzzle);

    const Game& game() const { return game_; }
    bool active() const { return active_; }
    bool solved() const { return active_ && next_ >= solution_.size(); }
    // The side that has to find the moves.
    Color solverColor() const { return solver_; }
    int mistakes() const { return mistakes_; }
    // Solver moves found so far, and how many the puzzle has in total.
    int solverMovesDone() const;
    int solverMovesTotal() const;

    // The solver tries a move. A move that gives checkmate always counts as correct.
    Outcome play(Move move);
    // True after a Correct move, until playReply() has made the opponent's answer.
    bool replyPending() const;
    void playReply();

    // The move the solver is expected to find now, if any.
    std::optional<Move> expectedMove() const;
    // Plays the next move of the solution, whoever's turn it is (for "show solution").
    // Returns false when the line is finished.
    bool revealNext();

    // The whole solution in SAN with move numbers, e.g. "24... Bxg3 25. Rxe7 Qb1+".
    const std::string& solutionSan() const { return solutionSan_; }

private:
    Game game_;
    std::vector<Move> solution_;  // all moves of the line, the opponent's first move included
    std::size_t next_ = 0;        // index of the next move to be played
    Color solver_ = Color::White;
    bool active_ = false;
    int mistakes_ = 0;
    std::string solutionSan_;
};

// Rating after a result against an opponent (or puzzle) of the given rating.
// New players move faster: the K factor shrinks as `gamesPlayed` grows.
int updatedRating(int rating, int opponentRating, bool won, int gamesPlayed);

}  // namespace core

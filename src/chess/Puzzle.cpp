#include "chess/Puzzle.h"

#include <algorithm>
#include <cmath>

namespace core {

std::vector<std::string> splitMoves(std::string_view text) {
    std::vector<std::string> moves;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && text[pos] == ' ') ++pos;
        std::size_t end = pos;
        while (end < text.size() && text[end] != ' ') ++end;
        if (end > pos) moves.emplace_back(text.substr(pos, end - pos));
        pos = end;
    }
    return moves;
}

PuzzleSession::PuzzleSession() { game_.setEnforceDraws(false); }

bool PuzzleSession::start(const Puzzle& puzzle) {
    active_ = false;
    solution_.clear();
    solutionSan_.clear();
    next_ = 0;
    mistakes_ = 0;
    // A puzzle needs the opponent's move and at least one move to find.
    if (puzzle.moves.size() < 2) return false;

    // Play the whole line once on a scratch board: this validates it and gives SAN.
    Game scratch;
    scratch.setEnforceDraws(false);
    if (!scratch.reset(puzzle.fen)) return false;
    for (std::size_t i = 0; i < puzzle.moves.size(); ++i) {
        const bool white = scratch.sideToMove() == Color::White;
        const int number = scratch.fullMoveNumber();
        if (!scratch.makeUciMove(puzzle.moves[i])) return false;
        solution_.push_back(scratch.history().back().move);
        if (i == 0) continue;  // the opponent's lead-in move is not part of the answer
        if (white) {
            solutionSan_ += std::to_string(number) + ". ";
        } else if (i == 1) {
            solutionSan_ += std::to_string(number) + "... ";
        }
        solutionSan_ += scratch.history().back().san + " ";
    }
    if (!solutionSan_.empty()) solutionSan_.pop_back();

    if (!game_.reset(puzzle.fen)) return false;
    solver_ = opposite(game_.sideToMove());
    if (!game_.makeMove(solution_[0])) return false;
    next_ = 1;
    active_ = true;
    return true;
}

int PuzzleSession::solverMovesDone() const { return static_cast<int>(next_ / 2); }

int PuzzleSession::solverMovesTotal() const { return static_cast<int>(solution_.size() / 2); }

bool PuzzleSession::replyPending() const {
    return active_ && next_ < solution_.size() && game_.sideToMove() != solver_;
}

std::optional<Move> PuzzleSession::expectedMove() const {
    if (!active_ || next_ >= solution_.size() || game_.sideToMove() != solver_) return std::nullopt;
    return solution_[next_];
}

PuzzleSession::Outcome PuzzleSession::play(Move move) {
    const std::optional<Move> expected = expectedMove();
    if (!expected) return Outcome::Ignored;

    if (move == *expected) {
        if (!game_.makeMove(move)) return Outcome::Ignored;
        ++next_;
        return next_ >= solution_.size() ? Outcome::Solved : Outcome::Correct;
    }

    // Any other move: accepted only if it ends the game by checkmate.
    if (!game_.makeMove(move)) return Outcome::Ignored;
    if (game_.status() == Status::Checkmate) {
        next_ = solution_.size();
        return Outcome::Solved;
    }
    game_.undo();
    ++mistakes_;
    return Outcome::Wrong;
}

void PuzzleSession::playReply() {
    if (!replyPending()) return;
    if (game_.makeMove(solution_[next_])) ++next_;
}

bool PuzzleSession::revealNext() {
    if (!active_ || next_ >= solution_.size()) return false;
    if (!game_.makeMove(solution_[next_])) {
        next_ = solution_.size();
        return false;
    }
    ++next_;
    return true;
}

int updatedRating(int rating, int opponentRating, bool won, int gamesPlayed) {
    const double expected = 1.0 / (1.0 + std::pow(10.0, static_cast<double>(opponentRating - rating) / 400.0));
    const double k = gamesPlayed < 15 ? 48.0 : gamesPlayed < 50 ? 32.0 : 20.0;
    const double updated = static_cast<double>(rating) + k * ((won ? 1.0 : 0.0) - expected);
    return std::clamp(static_cast<int>(std::lround(updated)), 100, 3500);
}

}  // namespace core

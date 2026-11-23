#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "app/Settings.h"
#include "chess/GameTree.h"
#include "chess/Puzzle.h"
#include "engine/UciEngine.h"

namespace app {

// Turns tactical moments of a reviewed game into puzzles. For each candidate position
// the engine searches with two lines: the best move becomes a puzzle move only while it
// is clearly better than every alternative, so each solution is verified and unambiguous.
// Runs in the background with an engine process of its own, like GameAnalyzer.
class PuzzleGenerator {
public:
    explicit PuzzleGenerator(const Settings& settings);

    // Starts working on the given positions of `tree` (see core::puzzleCandidates()).
    // Everything needed is copied, so the tree may change afterwards.
    // Returns false if there is nothing to do or the engine could not be started (see error()).
    bool start(const core::GameTree& tree, const std::vector<core::NodeId>& candidates);
    void stop();
    // Processes engine output and starts the next search. Call once per frame.
    void update();

    bool running() const { return running_; }
    std::size_t done() const { return index_; }
    std::size_t total() const { return jobs_.size(); }
    const std::string& error() const { return error_; }
    // True once after all candidates were handled.
    bool takeFinished();
    // The puzzles found by the last run (cleared by the call).
    std::vector<core::Puzzle> takePuzzles();

private:
    struct Job {
        std::string fen;                 // position before the opponent's mistake
        std::vector<std::string> moves;  // the mistake, then the solution found so far
        std::string pendingReply;        // opponent's answer, kept only if another solver move follows
        core::Color solver = core::Color::White;
        double solverShare = 0.0;        // expected score after the first solver move
    };
    struct LineResult {
        bool valid = false;
        int depth = 0;
        core::Eval eval;
        std::string pv;
    };

    void startSearch();
    void finishSearch();
    void finishJob();

    const Settings& settings_;
    engine::UciEngine engine_;
    std::vector<Job> jobs_;
    std::size_t index_ = 0;
    bool running_ = false;
    bool finished_ = false;
    int searchId_ = 0;
    LineResult best_;
    LineResult second_;
    std::vector<core::Puzzle> puzzles_;
    std::string error_;
};

}  // namespace app

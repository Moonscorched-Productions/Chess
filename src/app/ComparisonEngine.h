#pragma once

#include <string>

#include "app/Settings.h"
#include "chess/GameTree.h"
#include "engine/UciEngine.h"

namespace app {

// A second engine that analyses the same position as the main one, so the two can be
// compared: evaluation, preferred move and principal variation. The engine is the one
// set as "comparison engine" in the settings, with its own option list.
class ComparisonEngine {
public:
    struct Result {
        bool valid = false;
        core::Eval eval;  // from White's point of view
        int depth = 0;
        std::string pv;   // UCI moves
    };

    explicit ComparisonEngine(const Settings& settings);

    // True if a comparison engine is set in the settings.
    bool configured() const { return !settings_.compareEnginePath.empty(); }

    // Keeps an open-ended search running on this position. Calling it again with the same
    // position changes nothing.
    void analyse(const std::string& startFen, const std::string& uciMoves, bool whiteToMove);
    // Stops searching; the engine process stays ready.
    void rest();
    // Starts over with the current settings (call when the path or options changed).
    void restart();
    // Processes engine output. Call once per frame.
    void update();

    std::string name() const { return engine_.name(); }
    const std::string& error() const { return error_; }
    const Result& result() const { return result_; }

private:
    const Settings& settings_;
    engine::UciEngine engine_;
    bool started_ = false;
    std::string error_;
    // The position wanted and the one being searched ("fen|moves"; empty = none).
    std::string wantedKey_;
    std::string wantedFen_;
    std::string wantedMoves_;
    bool wantedWhiteToMove_ = true;
    std::string searchKey_;
    bool searchWhiteToMove_ = true;
    int searchId_ = 0;
    Result result_;
};

}  // namespace app

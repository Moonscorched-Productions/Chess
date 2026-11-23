#pragma once

#include <optional>
#include <string>
#include <vector>

#include "app/Settings.h"
#include "chess/GameTree.h"
#include "engine/UciEngine.h"

namespace app {

// Asks an engine how good some moves are in one position: each candidate is searched briefly
// and its result kept as an evaluation from White's side. The engine process is started on
// the first request and kept for the next ones.
class MoveJudge {
public:
    explicit MoveJudge(const Settings& settings) : settings_(settings) {}

    // Starts judging `moves` (UCI) in `fen`. A request still running is dropped.
    void judge(const std::string& fen, std::vector<std::string> moves);
    // Processes engine output. Call once per frame.
    void update();

    bool busy() const { return !moves_.empty() && !done_; }
    // True once after a request finished (successfully or not).
    bool takeDone();
    // The evaluation after each move, in request order (empty where the engine gave none).
    const std::vector<std::optional<core::Eval>>& results() const { return results_; }
    const std::string& error() const { return error_; }

private:
    void startNext();

    const Settings& settings_;
    engine::UciEngine engine_;
    std::string fen_;
    std::vector<std::string> moves_;
    std::vector<std::optional<core::Eval>> results_;
    std::size_t index_ = 0;
    int searchId_ = 0;
    int depth_ = 0;
    bool whiteToMoveAfter_ = false;
    bool done_ = false;
    bool doneReported_ = true;
    std::string error_;
};

}  // namespace app

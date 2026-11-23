#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "app/Settings.h"
#include "chess/GameTree.h"
#include "engine/UciEngine.h"

namespace app {

// Applies a list of extra UCI options written as "Name=Value; Name=Value". Options the
// engine does not have are skipped.
void applyExtraOptions(engine::UciEngine& engine, std::string_view options);

// Points the engine at the Syzygy tablebase folder from the settings, if both exist.
void applyTablebases(engine::UciEngine& engine, const Settings& settings);

// Sets an engine up for analysis at full strength (threads, hash, tablebases, no strength
// limit) from the user's settings, then applies `extraOptions`. Any running search is
// stopped first.
void configureForAnalysis(engine::UciEngine& engine, const Settings& settings, std::string_view extraOptions);
// Same, with the main engine's extra options from the settings.
void configureForAnalysis(engine::UciEngine& engine, const Settings& settings);

// Converts an engine score (from the side to move) into an evaluation from White's side.
// `info` must carry a score.
core::Eval evalFromInfo(const engine::SearchInfo& info, bool whiteToMove);

// Evaluates every position of a game's main line in the background, with an engine
// process of its own that only lives while there is work. Results are written into the
// tree: an evaluation per position and the engine's preferred move.
class GameAnalyzer {
public:
    explicit GameAnalyzer(const Settings& settings);

    // Starts analysing the positions of the main line that are not analysed yet.
    // The tree must stay alive and keep its nodes until the work is done or stop() is called.
    // Returns false if there is nothing to do or the engine could not be started (see error()).
    bool start(core::GameTree& tree);
    // Abandons the work and releases the engine.
    void stop();
    // Processes engine output and starts the next search. Call once per frame.
    void update();

    bool running() const { return tree_ != nullptr; }
    std::size_t done() const { return index_; }
    std::size_t total() const { return queue_.size(); }
    const std::string& error() const { return error_; }
    // True once after the last position has been analysed.
    bool takeFinished();

private:
    void handleInfo(const engine::SearchInfo& info);

    const Settings& settings_;
    engine::UciEngine engine_;
    core::GameTree* tree_ = nullptr;
    std::vector<core::NodeId> queue_;
    std::size_t index_ = 0;
    int searchId_ = 0;
    int searchDepth_ = 0;  // deepest result of the current search
    bool finished_ = false;
    std::string error_;
};

}  // namespace app

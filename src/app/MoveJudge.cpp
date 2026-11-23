#include "app/MoveJudge.h"

#include <algorithm>
#include <utility>

#include "app/GameAnalyzer.h"
#include "app/Paths.h"
#include "chess/Game.h"

namespace app {

void MoveJudge::judge(const std::string& fen, std::vector<std::string> moves) {
    if (searchId_ != 0) engine_.stopSearch();
    searchId_ = 0;
    fen_ = fen;
    moves_ = std::move(moves);
    results_.assign(moves_.size(), std::nullopt);
    index_ = 0;
    done_ = moves_.empty();
    doneReported_ = false;
    core::Game game;
    game.setEnforceDraws(false);
    whiteToMoveAfter_ = game.reset(fen) && game.sideToMove() == core::Color::Black;

    using State = engine::UciEngine::State;
    if (engine_.state() == State::Stopped || engine_.state() == State::Failed) {
        error_.clear();
        const std::filesystem::path path = resolveEnginePath(settings_.enginePath);
        if (path.empty()) {
            error_ = "No chess engine found. Set the engine path in Settings.";
        } else if (!engine_.start(path, &error_) && error_.empty()) {
            error_ = "The engine could not be started.";
        }
        if (!error_.empty()) done_ = true;
    }
}

bool MoveJudge::takeDone() {
    if (!done_ || doneReported_) return false;
    doneReported_ = true;
    return true;
}

void MoveJudge::startNext() {
    engine::SearchLimits limits;
    limits.moveTimeMs = std::clamp(settings_.analysisMoveTimeMs, 300, 1000);
    limits.depth = std::max(settings_.analysisDepth, 12);
    depth_ = 0;
    searchId_ = engine_.search(fen_, moves_[index_], limits);
}

void MoveJudge::update() {
    for (const engine::EngineEvent& event : engine_.poll()) {
        switch (event.type) {
            case engine::EngineEvent::Type::Ready:
                configureForAnalysis(engine_, settings_);
                if (engine_.option("MultiPV")) engine_.setOption("MultiPV", "1");
                break;
            case engine::EngineEvent::Type::Info: {
                const engine::SearchInfo& info = event.info;
                if (event.searchId != searchId_ || searchId_ == 0 || info.isBound || info.multiPv != 1) break;
                if ((!info.scoreCp && !info.mateIn) || info.depth < depth_) break;
                depth_ = info.depth;
                results_[index_] = evalFromInfo(info, whiteToMoveAfter_);
                break;
            }
            case engine::EngineEvent::Type::BestMove:
                if (event.searchId == searchId_ && searchId_ != 0) {
                    searchId_ = 0;
                    if (++index_ >= moves_.size()) done_ = true;
                }
                break;
            case engine::EngineEvent::Type::Terminated:
                error_ = event.message.empty() ? "The engine stopped unexpectedly." : event.message;
                searchId_ = 0;
                done_ = true;
                break;
        }
    }
    if (!done_ && searchId_ == 0 && index_ < moves_.size() && engine_.state() == engine::UciEngine::State::Ready) {
        startNext();
    }
}

}  // namespace app

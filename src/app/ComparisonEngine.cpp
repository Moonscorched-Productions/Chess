#include "app/ComparisonEngine.h"

#include "app/GameAnalyzer.h"
#include "app/Paths.h"

namespace app {

ComparisonEngine::ComparisonEngine(const Settings& settings) : settings_(settings) {}

void ComparisonEngine::analyse(const std::string& startFen, const std::string& uciMoves, bool whiteToMove) {
    wantedKey_ = startFen + "|" + uciMoves;
    wantedFen_ = startFen;
    wantedMoves_ = uciMoves;
    wantedWhiteToMove_ = whiteToMove;
}

void ComparisonEngine::rest() {
    wantedKey_.clear();
    if (searchId_ != 0) engine_.stopSearch();
    searchId_ = 0;
    searchKey_.clear();
}

void ComparisonEngine::restart() {
    engine_.shutdown();
    started_ = false;
    error_.clear();
    searchId_ = 0;
    searchKey_.clear();
    result_ = {};
}

void ComparisonEngine::update() {
    if (!configured()) {
        if (started_) restart();
        return;
    }
    if (!started_) {
        if (wantedKey_.empty()) return;  // started on first use
        started_ = true;
        const std::filesystem::path path = resolveEnginePath(settings_.compareEnginePath);
        if (path.empty()) {
            error_ = "The comparison engine was not found.";
        } else if (!engine_.start(path, &error_) && error_.empty()) {
            error_ = "The comparison engine could not be started.";
        }
        return;
    }

    for (const engine::EngineEvent& event : engine_.poll()) {
        switch (event.type) {
            case engine::EngineEvent::Type::Ready:
                error_.clear();
                configureForAnalysis(engine_, settings_, settings_.compareEngineOptions);
                if (engine_.option("MultiPV")) engine_.setOption("MultiPV", "1");
                searchId_ = 0;
                searchKey_.clear();
                break;
            case engine::EngineEvent::Type::Info:
                if (event.searchId != searchId_ || searchId_ == 0 || event.info.isBound || event.info.multiPv != 1) break;
                if (!event.info.scoreCp && !event.info.mateIn) break;
                result_.valid = true;
                result_.eval = evalFromInfo(event.info, searchWhiteToMove_);
                result_.depth = event.info.depth;
                if (!event.info.pv.empty()) result_.pv = event.info.pv;
                break;
            case engine::EngineEvent::Type::BestMove:
                if (event.searchId == searchId_) searchId_ = 0;
                break;
            case engine::EngineEvent::Type::Terminated:
                error_ = event.message.empty() ? "The comparison engine stopped unexpectedly." : event.message;
                searchId_ = 0;
                searchKey_.clear();
                break;
        }
    }
    if (engine_.state() != engine::UciEngine::State::Ready || wantedKey_ == searchKey_) return;

    // search() stops whatever was running before.
    result_ = {};
    searchKey_ = wantedKey_;
    searchWhiteToMove_ = wantedWhiteToMove_;
    searchId_ = wantedKey_.empty() ? 0 : engine_.search(wantedFen_, wantedMoves_, engine::SearchLimits{});
}

}  // namespace app

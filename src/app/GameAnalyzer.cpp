#include "app/GameAnalyzer.h"

#include <algorithm>
#include <thread>
#include <utility>

#include "app/Paths.h"
#include "chess/Review.h"

namespace app {

void applyExtraOptions(engine::UciEngine& engine, std::string_view options) {
    const auto trim = [](std::string_view text) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
        return text;
    };
    std::size_t start = 0;
    while (start <= options.size()) {
        const std::size_t end = std::min(options.find(';', start), options.size());
        const std::string_view entry = options.substr(start, end - start);
        if (const std::size_t equals = entry.find('='); equals != std::string_view::npos) {
            const std::string_view name = trim(entry.substr(0, equals));
            const std::string_view value = trim(entry.substr(equals + 1));
            if (!name.empty() && engine.option(name)) engine.setOption(name, value);
        }
        start = end + 1;
    }
}

void applyTablebases(engine::UciEngine& engine, const Settings& settings) {
    if (!settings.syzygyPath.empty() && engine.option("SyzygyPath")) engine.setOption("SyzygyPath", settings.syzygyPath);
}

void configureForAnalysis(engine::UciEngine& engine, const Settings& settings) {
    configureForAnalysis(engine, settings, settings.engineOptions);
}

void configureForAnalysis(engine::UciEngine& engine, const Settings& settings, std::string_view extraOptions) {
    // Options must not be changed while a search is running.
    engine.stopSearch();

    if (const auto threads = engine.option("Threads")) {
        int wanted = settings.analysisThreads;
        if (wanted <= 0) wanted = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) / 2, 1, 8);
        wanted = std::clamp(wanted, threads->min.value_or(1), threads->max.value_or(wanted));
        engine.setOption("Threads", std::to_string(wanted));
    }
    if (const auto hash = engine.option("Hash")) {
        const int wanted = std::clamp(settings.analysisHashMb, hash->min.value_or(1), hash->max.value_or(settings.analysisHashMb));
        engine.setOption("Hash", std::to_string(wanted));
    }
    if (engine.option("UCI_LimitStrength")) engine.setOption("UCI_LimitStrength", "false");
    if (const auto skill = engine.option("Skill Level"); skill && skill->max) {
        engine.setOption("Skill Level", std::to_string(*skill->max));
    }
    applyTablebases(engine, settings);
    applyExtraOptions(engine, extraOptions);
    engine.newGame();
}

core::Eval evalFromInfo(const engine::SearchInfo& info, bool whiteToMove) {
    core::Eval eval;
    eval.depth = info.depth;
    if (info.mateIn) {
        eval.mate = whiteToMove ? *info.mateIn : -*info.mateIn;
        // "Mate in 0": the side to move has been mated.
        if (*info.mateIn == 0) eval.cp = whiteToMove ? -1 : 1;
    } else if (info.scoreCp) {
        eval.cp = whiteToMove ? *info.scoreCp : -*info.scoreCp;
    }
    return eval;
}

GameAnalyzer::GameAnalyzer(const Settings& settings) : settings_(settings) {}

bool GameAnalyzer::start(core::GameTree& tree) {
    stop();
    error_.clear();
    finished_ = false;
    queue_.clear();
    index_ = 0;

    core::evaluateFinalPosition(tree);
    std::vector<core::NodeId> nodes{core::kRootNode};
    const std::vector<core::NodeId> mainLine = tree.mainLine();
    nodes.insert(nodes.end(), mainLine.begin(), mainLine.end());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const core::TreeNode& node = tree.node(nodes[i]);
        // Every position needs an evaluation; all but the last also the engine's move,
        // which decides whether the move played there was the best one.
        const bool needsMove = i + 1 < nodes.size() && node.bestMove.empty();
        if (!node.eval || needsMove) queue_.push_back(nodes[i]);
    }
    if (queue_.empty()) return false;

    const std::filesystem::path path = resolveEnginePath(settings_.enginePath);
    if (path.empty()) {
        error_ = "No chess engine found. Set the engine path in Settings.";
        queue_.clear();
        return false;
    }
    if (!engine_.start(path, &error_)) {
        if (error_.empty()) error_ = "The engine could not be started.";
        queue_.clear();
        return false;
    }
    tree_ = &tree;
    searchId_ = 0;
    return true;
}

void GameAnalyzer::stop() {
    if (tree_ == nullptr) return;
    tree_ = nullptr;
    searchId_ = 0;
    engine_.shutdown();
}

bool GameAnalyzer::takeFinished() { return std::exchange(finished_, false); }

void GameAnalyzer::update() {
    if (tree_ == nullptr) return;

    for (const engine::EngineEvent& event : engine_.poll()) {
        switch (event.type) {
            case engine::EngineEvent::Type::Ready:
                configureForAnalysis(engine_, settings_);
                if (engine_.option("MultiPV")) engine_.setOption("MultiPV", "1");
                break;
            case engine::EngineEvent::Type::Info:
                if (event.searchId == searchId_ && searchId_ != 0) handleInfo(event.info);
                break;
            case engine::EngineEvent::Type::BestMove:
                if (event.searchId == searchId_ && searchId_ != 0) {
                    searchId_ = 0;
                    ++index_;
                }
                break;
            case engine::EngineEvent::Type::Terminated:
                error_ = event.message.empty() ? "The engine stopped unexpectedly." : event.message;
                stop();
                return;
        }
    }
    if (engine_.state() != engine::UciEngine::State::Ready || searchId_ != 0) return;

    while (index_ < queue_.size() && !tree_->contains(queue_[index_])) ++index_;
    if (index_ >= queue_.size()) {
        stop();
        finished_ = true;
        return;
    }
    engine::SearchLimits limits;
    // Depth alone is unpredictable (sharp positions can take many seconds), so time caps it.
    limits.depth = settings_.analysisDepth;
    limits.moveTimeMs = settings_.analysisMoveTimeMs;
    searchDepth_ = 0;
    searchId_ = engine_.search(tree_->startFen(), tree_->uciMoves(queue_[index_]), limits);
}

void GameAnalyzer::handleInfo(const engine::SearchInfo& info) {
    if (info.isBound || info.multiPv != 1 || (!info.scoreCp && !info.mateIn)) return;
    if (info.depth < searchDepth_) return;
    searchDepth_ = info.depth;

    const core::NodeId node = queue_[index_];
    const bool whiteToMove =
        (node == core::kRootNode ? tree_->startSideToMove() : core::opposite(tree_->mover(node))) == core::Color::White;
    tree_->setEval(node, evalFromInfo(info, whiteToMove));
    if (!info.pv.empty()) tree_->setBestMove(node, info.pv.substr(0, info.pv.find(' ')));
}

}  // namespace app

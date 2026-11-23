#include "app/PuzzleGenerator.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "app/GameAnalyzer.h"
#include "app/Paths.h"
#include "chess/PuzzleBuilder.h"

namespace app {

namespace {

constexpr std::size_t kMaxSolverMoves = 4;

std::string joined(const std::vector<std::string>& moves) {
    std::string text;
    for (const std::string& move : moves) {
        if (!text.empty()) text += ' ';
        text += move;
    }
    return text;
}

// The n-th move (0-based) of a space separated principal variation, or "".
std::string pvMove(const std::string& pv, int n) {
    std::size_t pos = 0;
    for (int i = 0; pos < pv.size(); ++i) {
        const std::size_t end = std::min(pv.find(' ', pos), pv.size());
        if (i == n) return pv.substr(pos, end - pos);
        pos = end + 1;
    }
    return {};
}

}  // namespace

PuzzleGenerator::PuzzleGenerator(const Settings& settings) : settings_(settings) {}

bool PuzzleGenerator::start(const core::GameTree& tree, const std::vector<core::NodeId>& candidates) {
    stop();
    error_.clear();
    finished_ = false;
    jobs_.clear();
    puzzles_.clear();
    index_ = 0;

    for (const core::NodeId id : candidates) {
        if (!tree.contains(id) || id == core::kRootNode) continue;
        const core::TreeNode& node = tree.node(id);
        Job job;
        job.fen = tree.node(node.parent).fen;
        job.moves.push_back(node.record.uci);
        job.solver = core::opposite(tree.mover(id));
        jobs_.push_back(std::move(job));
    }
    if (jobs_.empty()) return false;

    const std::filesystem::path path = resolveEnginePath(settings_.enginePath);
    if (path.empty()) {
        error_ = "No chess engine found. Set the engine path in Settings.";
        jobs_.clear();
        return false;
    }
    if (!engine_.start(path, &error_)) {
        if (error_.empty()) error_ = "The engine could not be started.";
        jobs_.clear();
        return false;
    }
    running_ = true;
    searchId_ = 0;
    return true;
}

void PuzzleGenerator::stop() {
    if (!running_) return;
    running_ = false;
    searchId_ = 0;
    engine_.shutdown();
}

bool PuzzleGenerator::takeFinished() { return std::exchange(finished_, false); }

std::vector<core::Puzzle> PuzzleGenerator::takePuzzles() { return std::exchange(puzzles_, {}); }

void PuzzleGenerator::update() {
    if (!running_) return;

    for (const engine::EngineEvent& event : engine_.poll()) {
        switch (event.type) {
            case engine::EngineEvent::Type::Ready:
                configureForAnalysis(engine_, settings_);
                // Two lines: the second one shows whether the best move stands alone.
                if (engine_.option("MultiPV")) engine_.setOption("MultiPV", "2");
                break;
            case engine::EngineEvent::Type::Info: {
                const engine::SearchInfo& info = event.info;
                if (event.searchId != searchId_ || searchId_ == 0 || info.isBound) break;
                if ((!info.scoreCp && !info.mateIn) || info.multiPv < 1 || info.multiPv > 2) break;
                LineResult& line = info.multiPv == 1 ? best_ : second_;
                if (info.depth < line.depth) break;
                line.valid = true;
                line.depth = info.depth;
                line.eval = evalFromInfo(info, jobs_[index_].solver == core::Color::White);
                line.pv = info.pv;
                break;
            }
            case engine::EngineEvent::Type::BestMove:
                if (event.searchId == searchId_ && searchId_ != 0) {
                    searchId_ = 0;
                    finishSearch();
                }
                break;
            case engine::EngineEvent::Type::Terminated:
                error_ = event.message.empty() ? "The engine stopped unexpectedly." : event.message;
                stop();
                return;
        }
    }
    if (engine_.state() != engine::UciEngine::State::Ready || searchId_ != 0) return;

    if (index_ >= jobs_.size()) {
        stop();
        finished_ = true;
        return;
    }
    startSearch();
}

void PuzzleGenerator::startSearch() {
    Job& job = jobs_[index_];
    std::vector<std::string> moves = job.moves;
    if (!job.pendingReply.empty()) moves.push_back(job.pendingReply);
    best_ = {};
    second_ = {};
    engine::SearchLimits limits;
    // A little deeper than a game review: the solution has to hold up.
    limits.depth = settings_.analysisDepth + 2;
    limits.moveTimeMs = settings_.analysisMoveTimeMs * 2;
    searchId_ = engine_.search(job.fen, joined(moves), limits);
}

// Decides what the finished search means for the puzzle being built.
void PuzzleGenerator::finishSearch() {
    Job& job = jobs_[index_];
    const std::string move = best_.valid ? pvMove(best_.pv, 0) : std::string();
    // A second line from a shallower iteration than the first says nothing reliable.
    const bool secondKnown = second_.valid && second_.depth + 2 >= best_.depth;
    const std::optional<core::Eval> second = secondKnown ? std::optional<core::Eval>(second_.eval) : std::nullopt;
    const bool firstSolverMove = job.moves.size() == 1;
    // The first move must be a real choice: with a single legal move there is nothing to find.
    const bool clear = !move.empty() && core::isOnlyGoodMove(best_.eval, second, job.solver) && (!firstSolverMove || second_.valid);
    if (!clear) {
        finishJob();
        return;
    }

    if (!job.pendingReply.empty()) job.moves.push_back(std::exchange(job.pendingReply, {}));
    job.moves.push_back(move);
    if (firstSolverMove) {
        const double white = best_.eval.whiteWinPercent();
        job.solverShare = job.solver == core::Color::White ? white : 100.0 - white;
    }

    const std::string reply = pvMove(best_.pv, 1);
    const bool mated = best_.eval.mate && std::abs(*best_.eval.mate) == 1;
    if (reply.empty() || mated || job.moves.size() / 2 >= kMaxSolverMoves) {
        finishJob();
        return;
    }
    job.pendingReply = reply;  // search again after the opponent's answer
}

void PuzzleGenerator::finishJob() {
    const Job& job = jobs_[index_];
    if (job.moves.size() >= 2) {
        if (std::optional<core::Puzzle> puzzle = core::buildPuzzle(job.fen, job.moves, job.solverShare)) {
            puzzles_.push_back(std::move(*puzzle));
        }
    }
    ++index_;
}

}  // namespace app

#include "chess/GameTree.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace core {

bool Eval::whiteIsAhead() const {
    if (mate && *mate != 0) return *mate > 0;
    return cp >= 0;
}

double Eval::whiteWinPercent() const {
    if (mate) return whiteIsAhead() ? 100.0 : 0.0;
    // Logistic model of expected score from centipawns (the constant is the one Lichess uses).
    const double clamped = std::clamp(static_cast<double>(cp), -1500.0, 1500.0);
    return 50.0 + 50.0 * (2.0 / (1.0 + std::exp(-0.00368208 * clamped)) - 1.0);
}

GameTree::GameTree(std::string_view startFen) {
    Game game;
    if (!game.reset(startFen)) game.reset();
    TreeNode root;
    root.fen = game.fen();
    nodes_.push_back(std::move(root));
    removed_.push_back(false);
    startSide_ = game.sideToMove();
    startMoveNumber_ = game.fullMoveNumber();
}

bool GameTree::contains(NodeId id) const {
    return id >= 0 && static_cast<std::size_t>(id) < nodes_.size() && !removed_[static_cast<std::size_t>(id)];
}

const TreeNode& GameTree::node(NodeId id) const {
    assert(contains(id));
    return nodes_[static_cast<std::size_t>(contains(id) ? id : kRootNode)];
}

TreeNode& GameTree::mutableNode(NodeId id) { return nodes_[static_cast<std::size_t>(contains(id) ? id : kRootNode)]; }

NodeId GameTree::addMove(NodeId parent, const MoveRecord& record, std::string fenAfter) {
    if (!contains(parent)) return kNoNode;
    for (const NodeId child : node(parent).children) {
        if (node(child).record.move == record.move) return child;
    }
    TreeNode added;
    added.parent = parent;
    added.record = record;
    added.fen = std::move(fenAfter);
    added.ply = node(parent).ply + 1;
    const NodeId id = static_cast<NodeId>(nodes_.size());
    nodes_.push_back(std::move(added));
    removed_.push_back(false);
    mutableNode(parent).children.push_back(id);
    ++moveCount_;
    return id;
}

void GameTree::remove(NodeId id) {
    if (!contains(id) || id == kRootNode) return;
    std::vector<NodeId>& siblings = mutableNode(node(id).parent).children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());

    std::vector<NodeId> pending{id};
    while (!pending.empty()) {
        const NodeId current = pending.back();
        pending.pop_back();
        TreeNode& removedNode = nodes_[static_cast<std::size_t>(current)];
        pending.insert(pending.end(), removedNode.children.begin(), removedNode.children.end());
        removedNode = TreeNode{};
        removed_[static_cast<std::size_t>(current)] = true;
        --moveCount_;
    }
}

void GameTree::promote(NodeId id) {
    if (!contains(id) || id == kRootNode) return;
    std::vector<NodeId>& siblings = mutableNode(node(id).parent).children;
    const auto it = std::find(siblings.begin(), siblings.end(), id);
    if (it != siblings.end() && it != siblings.begin()) std::iter_swap(it, it - 1);
}

void GameTree::makeMainLine(NodeId id) {
    for (NodeId current = id; contains(current) && current != kRootNode; current = node(current).parent) {
        std::vector<NodeId>& siblings = mutableNode(node(current).parent).children;
        const auto it = std::find(siblings.begin(), siblings.end(), current);
        if (it != siblings.end()) std::rotate(siblings.begin(), it, it + 1);
    }
}

void GameTree::setComment(NodeId id, std::string comment) {
    if (contains(id)) mutableNode(id).comment = std::move(comment);
}

void GameTree::setNags(NodeId id, std::vector<int> nags) {
    if (contains(id)) mutableNode(id).nags = std::move(nags);
}

void GameTree::setEval(NodeId id, std::optional<Eval> eval) {
    if (contains(id)) mutableNode(id).eval = eval;
}

void GameTree::setBestMove(NodeId id, std::string uci) {
    if (contains(id)) mutableNode(id).bestMove = std::move(uci);
}

std::vector<NodeId> GameTree::pathTo(NodeId id) const {
    std::vector<NodeId> path;
    for (NodeId current = id; contains(current) && current != kRootNode; current = node(current).parent)
        path.push_back(current);
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<NodeId> GameTree::mainLine() const {
    std::vector<NodeId> line;
    for (NodeId current = kRootNode; !node(current).children.empty();) {
        current = node(current).children.front();
        line.push_back(current);
    }
    return line;
}

NodeId GameTree::lineEnd(NodeId id) const {
    NodeId current = contains(id) ? id : kRootNode;
    while (!node(current).children.empty()) current = node(current).children.front();
    return current;
}

bool GameTree::isMainLine(NodeId id) const {
    for (NodeId current = id; contains(current) && current != kRootNode; current = node(current).parent) {
        if (node(node(current).parent).children.front() != current) return false;
    }
    return contains(id);
}

std::string GameTree::uciMoves(NodeId id) const {
    std::string result;
    for (const NodeId step : pathTo(id)) {
        if (!result.empty()) result += ' ';
        result += node(step).record.uci;
    }
    return result;
}

int GameTree::moveNumber(NodeId id) const {
    const int halfMoves = node(id).ply - 1 + (startSide_ == Color::Black ? 1 : 0);
    return startMoveNumber_ + std::max(halfMoves, 0) / 2;
}

Color GameTree::mover(NodeId id) const {
    // Odd plies are made by the side that moved first.
    const bool firstMover = (node(id).ply % 2) == 1;
    return firstMover ? startSide_ : opposite(startSide_);
}

// --- GameRecord ---------------------------------------------------------------

std::string GameRecord::tag(std::string_view name) const {
    for (const auto& [key, value] : tags)
        if (key == name) return value;
    return {};
}

void GameRecord::setTag(std::string_view name, std::string_view value) {
    for (auto& [key, existing] : tags) {
        if (key == name) {
            existing = std::string(value);
            return;
        }
    }
    tags.emplace_back(std::string(name), std::string(value));
}

// --- Helpers ------------------------------------------------------------------

bool loadPosition(const GameTree& tree, NodeId id, Game& game) {
    if (!game.reset(tree.startFen())) return false;
    for (const NodeId step : tree.pathTo(id)) {
        if (!game.makeMove(tree.node(step).record.move)) return false;
    }
    return true;
}

NodeId playMove(GameTree& tree, NodeId parent, Game& game, Move move) {
    if (!tree.contains(parent) || !game.makeMove(move)) return kNoNode;
    return tree.addMove(parent, game.history().back(), game.fen());
}

std::string_view resultString(Result result) {
    switch (result) {
        case Result::WhiteWins: return "1-0";
        case Result::BlackWins: return "0-1";
        case Result::Draw: return "1/2-1/2";
        case Result::None: break;
    }
    return "*";
}

GameRecord recordFromGame(const Game& game) {
    GameRecord record{{}, GameTree(game.startFen())};
    Game replay;
    replay.setEnforceDraws(false);
    replay.reset(game.startFen());
    NodeId current = kRootNode;
    for (const MoveRecord& played : game.history()) {
        const NodeId next = playMove(record.tree, current, replay, played.move);
        if (next == kNoNode) break;
        current = next;
    }
    record.setTag("Result", resultString(game.result()));
    return record;
}

}  // namespace core

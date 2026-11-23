#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "chess/Game.h"
#include "chess/Types.h"

namespace core {

using NodeId = int;
constexpr NodeId kRootNode = 0;
constexpr NodeId kNoNode = -1;

// An engine evaluation from White's point of view.
struct Eval {
    int cp = 0;               // centipawns; with `mate` set only its sign matters
    std::optional<int> mate;  // moves to mate: positive = White mates, negative = Black mates.
                              // 0 = the position is checkmate; the sign of `cp` says who won.
    int depth = 0;

    bool whiteIsAhead() const;

    // Expected score for White in percent (0..100), the scale used by evaluation bars and graphs.
    double whiteWinPercent() const;
};

struct TreeNode {
    NodeId parent = kNoNode;
    // children[0] continues the line this node is on; the others are alternatives to it.
    std::vector<NodeId> children;
    MoveRecord record;  // the move leading to this node (unused for the root)
    std::string fen;    // position after the move
    std::string comment;
    std::vector<int> nags;  // PGN numeric annotation glyphs: 1 = "!", 2 = "?", ...
    std::optional<Eval> eval;
    std::string bestMove;  // the engine's preferred move in this position (UCI), if analysed
    int ply = 0;           // number of moves from the root
};

// The moves of a game as a tree: a main line plus any number of nested variations.
// The tree only stores moves and positions; it does not check legality (see playMove()).
// Node ids stay valid until that node is removed and are never reused.
class GameTree {
public:
    explicit GameTree(std::string_view startFen = kStartFen);

    const std::string& startFen() const { return nodes_[0].fen; }
    Color startSideToMove() const { return startSide_; }
    int startFullMoveNumber() const { return startMoveNumber_; }

    bool contains(NodeId id) const;
    const TreeNode& node(NodeId id) const;
    // Number of moves in the tree (all variations).
    int moveCount() const { return moveCount_; }

    // Adds a move below `parent`. If that move already exists there, returns the existing node.
    NodeId addMove(NodeId parent, const MoveRecord& record, std::string fenAfter);
    // Removes a node and everything after it. The root cannot be removed.
    void remove(NodeId id);
    // Moves a variation one place up among its alternatives (to the main continuation at most).
    void promote(NodeId id);
    // Makes the line leading to `id` the main line of the whole game.
    void makeMainLine(NodeId id);

    void setComment(NodeId id, std::string comment);
    void setNags(NodeId id, std::vector<int> nags);
    void setEval(NodeId id, std::optional<Eval> eval);
    void setBestMove(NodeId id, std::string uci);

    // Nodes from the first move to `id`, excluding the root.
    std::vector<NodeId> pathTo(NodeId id) const;
    // Nodes of the main line, excluding the root.
    std::vector<NodeId> mainLine() const;
    // Last node reached by following first children from `id`.
    NodeId lineEnd(NodeId id) const;
    bool isMainLine(NodeId id) const;
    // Space separated UCI moves from the start position to `id`.
    std::string uciMoves(NodeId id) const;

    // Move number of the move leading to `id`, and which side made it.
    int moveNumber(NodeId id) const;
    Color mover(NodeId id) const;

private:
    TreeNode& mutableNode(NodeId id);

    std::vector<TreeNode> nodes_;
    std::vector<bool> removed_;
    Color startSide_ = Color::White;
    int startMoveNumber_ = 1;
    int moveCount_ = 0;
};

// A game with its PGN header tags.
struct GameRecord {
    std::vector<std::pair<std::string, std::string>> tags;
    GameTree tree;

    // Value of a tag, or an empty string.
    std::string tag(std::string_view name) const;
    void setTag(std::string_view name, std::string_view value);
};

// Sets `game` to the position at `id` by replaying the moves from the start.
// Returns false if the tree holds a move that is not legal.
bool loadPosition(const GameTree& tree, NodeId id, Game& game);

// Plays `move` in the position at `parent` and adds it to the tree.
// `game` must already be at `parent`; it is advanced to the new node.
// Returns kNoNode if the move is illegal.
NodeId playMove(GameTree& tree, NodeId parent, Game& game, Move move);

// Converts a linear game (as played in the Play section) into a record with a Result tag.
GameRecord recordFromGame(const Game& game);

// PGN result string for a game result: "1-0", "0-1", "1/2-1/2" or "*".
std::string_view resultString(Result result);

}  // namespace core

#include "storage/Explorer.h"

#include <algorithm>

#include "chess/Pgn.h"
#include "chess/Repertoire.h"
#include "chess/Statistics.h"
#include "storage/GameStore.h"

namespace storage {

std::vector<ExplorerMove> ExplorerIndex::moves(std::string_view fen, bool mineOnly) const {
    const auto it = positions_.find(core::positionKey(fen));
    if (it == positions_.end()) return {};
    std::vector<ExplorerMove> result;
    for (const ExplorerMove& move : it->second) {
        if (!mineOnly || move.mine > 0) result.push_back(move);
    }
    std::stable_sort(result.begin(), result.end(), [mineOnly](const ExplorerMove& a, const ExplorerMove& b) {
        return mineOnly ? a.mine > b.mine : a.games > b.games;
    });
    return result;
}

void ExplorerIndex::addGame(std::string_view pgn, std::string_view names) {
    const std::optional<core::GameRecord> record = core::parsePgn(pgn);
    if (!record || record->tree.moveCount() == 0) return;
    const core::GameTree& tree = record->tree;

    const std::string result = record->tag("Result");
    const bool whiteWon = result == "1-0";
    const bool blackWon = result == "0-1";
    const bool drawn = result == "1/2-1/2";
    // The user's side, if exactly one side carries one of their names.
    const bool userWhite = core::matchesPlayer(record->tag("White"), names);
    const bool userBlack = core::matchesPlayer(record->tag("Black"), names);
    const bool mine = userWhite != userBlack;

    ++games_;
    if (mine) ++myGames_;
    core::NodeId parent = core::kRootNode;
    int ply = 0;
    for (const core::NodeId node : tree.mainLine()) {
        if (++ply > kMaxPlies) break;
        const core::MoveRecord& played = tree.node(node).record;
        std::vector<ExplorerMove>& moves = positions_[core::positionKey(tree.node(parent).fen)];
        auto it = std::find_if(moves.begin(), moves.end(), [&](const ExplorerMove& move) { return move.uci == played.uci; });
        if (it == moves.end()) {
            moves.push_back(ExplorerMove{played.uci, played.san});
            it = moves.end() - 1;
        }
        ++it->games;
        it->whiteWins += whiteWon ? 1 : 0;
        it->blackWins += blackWon ? 1 : 0;
        it->draws += drawn ? 1 : 0;
        if (mine) {
            ++it->mine;
            it->myWins += (userWhite && whiteWon) || (userBlack && blackWon) ? 1 : 0;
            it->myLosses += (userWhite && blackWon) || (userBlack && whiteWon) ? 1 : 0;
            it->myDraws += drawn ? 1 : 0;
        }
        parent = node;
    }
}

ExplorerIndex buildExplorerIndex(const GameStore& store, std::string_view names, const std::atomic<bool>* cancel) {
    ExplorerIndex index;
    GameFilter filter;
    filter.limit = 1000000;
    for (const GameSummary& game : store.list(filter)) {
        if (cancel != nullptr && cancel->load()) break;
        if (const std::optional<std::string> pgn = store.pgn(game.id)) index.addGame(*pgn, names);
    }
    return index;
}

}  // namespace storage

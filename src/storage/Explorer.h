#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace storage {

class GameStore;

// How the library games continued from one position with one move.
struct ExplorerMove {
    std::string uci;
    std::string san;
    int games = 0;
    int whiteWins = 0;
    int draws = 0;
    int blackWins = 0;
    // The same, counted from the user's side, for the games the user played.
    int mine = 0;
    int myWins = 0;
    int myDraws = 0;
    int myLosses = 0;
};

// An opening explorer over the user's own game library: for every position of the
// first moves of each game, the moves played there and how those games ended.
class ExplorerIndex {
public:
    // The moves played from the position (any FEN; matched like core::positionKey),
    // most played first. With `mineOnly`, only moves from the user's own games.
    std::vector<ExplorerMove> moves(std::string_view fen, bool mineOnly) const;
    int gameCount() const { return games_; }
    int myGameCount() const { return myGames_; }

    // Adds one game (PGN text). `names`: the user's names, as in Settings::playerNames.
    void addGame(std::string_view pgn, std::string_view names);

    // How many moves of each game are indexed.
    static constexpr int kMaxPlies = 40;

private:
    std::unordered_map<std::string, std::vector<ExplorerMove>> positions_;
    int games_ = 0;
    int myGames_ = 0;
};

// Indexes every game of the store. Setting `cancel` stops early (the index is then partial).
ExplorerIndex buildExplorerIndex(const GameStore& store, std::string_view names, const std::atomic<bool>* cancel = nullptr);

}  // namespace storage

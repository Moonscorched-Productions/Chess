#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chess/GameTree.h"
#include "chess/Review.h"

namespace core {

// --- One move's tactics ------------------------------------------------------------

// What a single move does on the board, judged from the positions before and after it.
struct MoveTactics {
    bool check = false;
    bool mate = false;
    bool fork = false;           // the moved piece attacks two worthwhile targets
    bool pin = false;            // a line piece pins an enemy piece to a more valuable one (or the king)
    bool skewer = false;         // a line piece attacks a valuable piece with a lesser one behind it
    bool discovery = false;      // moving uncovers an attack by another piece
    bool winsHanging = false;    // captures a piece that was not adequately defended
    bool promotion = false;
};
// Returns nothing if `uci` is not legal in `fen`.
std::optional<MoveTactics> analyseMove(std::string_view fen, std::string_view uci);

// --- Why a mistake was a mistake -----------------------------------------------------

// Recurring patterns behind the user's mistakes. "Missed" = the engine's move had it,
// "Allowed" = the move played let the opponent do it.
enum class Motif {
    MissedMate,
    AllowedMate,
    MissedFork,
    AllowedFork,
    MissedPin,
    AllowedPin,
    MissedSkewer,
    AllowedSkewer,
    MissedDiscovery,
    AllowedDiscovery,
    MissedCapture,  // free material was there to take
    HungPiece,      // a piece was left where it could be taken
    Positional,     // no one-move tactic explains it
};
inline constexpr int kMotifCount = 13;

const char* motifName(Motif motif);     // "Allowed a fork"
const char* motifAdvice(Motif motif);   // one sentence on what to look for
// The puzzle theme that trains the pattern ("fork", "pin", ...), or "" when there is none.
const char* motifTheme(Motif motif);

// The motifs behind the move leading to `id` (which should be a mistake or blunder): looks at
// the engine's preferred move in the position before (what was missed) and the opponent's best
// reply after (what was allowed). Needs evaluations and engine moves in the tree.
// Returns {Positional} when no tactic explains it.
std::vector<Motif> mistakeMotifs(const GameTree& tree, NodeId id);

// --- Many games ----------------------------------------------------------------------

// One of the user's mistakes, with what is needed to drill it again.
struct MistakeExample {
    std::int64_t gameId = 0;
    NodeId node = kNoNode;  // the mistake in the game's tree
    std::string date;
    std::string opponent;
    std::string label;      // "14. Nxe5?"
    std::string fenBefore;  // the position the user had to find a move in
    std::string played;     // UCI
    std::string best;       // UCI, the engine's move
    std::string bestSan;
    MoveQuality quality = MoveQuality::None;
    GamePhase phase = GamePhase::Opening;
    double loss = 0.0;      // expected score lost, in percentage points
    std::vector<Motif> motifs;
};

// What the user's reviewed games say about their play.
struct TrainingReport {
    int games = 0;
    int moves = 0;          // the user's judged moves
    double centipawnLoss = 0.0;  // average per move, capped at 1000 per move
    int inaccuracies = 0;
    int mistakes = 0;
    int blunders = 0;
    std::array<int, kMotifCount> motifCounts{};          // over mistakes and blunders
    std::array<int, kPhaseCount> phaseErrors{};          // mistakes and blunders per game phase
    std::vector<MistakeExample> examples;                // in the order the games were added
    std::vector<double> blundersByGame;                  // per game, in order

    double perGame(int count) const { return games > 0 ? static_cast<double>(count) / games : 0.0; }
    // Motifs from most to least frequent (only those that occurred).
    std::vector<std::pair<Motif, int>> rankedMotifs() const;
};

// Adds one reviewed game, seen from `user`'s side.
void addToReport(TrainingReport& report, const GameRecord& record, Color user, std::int64_t gameId);

// --- Results against the engine ------------------------------------------------------

// "level 3" or "1500 Elo" from an opponent name such as "Stockfish 17 (level 3)"; empty if none.
std::string engineStrengthLabel(std::string_view opponent);

}  // namespace core

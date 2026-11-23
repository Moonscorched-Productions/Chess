#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chess/Review.h"

namespace core {

// True if `name` is one of the comma separated names in `list` (case and surrounding
// spaces are ignored). Used to find the user's own side in a game.
bool matchesPlayer(std::string_view name, std::string_view list);

// One reviewed game from the point of view of one player.
struct PlayerGame {
    std::int64_t gameId = 0;
    std::string date;      // PGN date; may be empty
    Color color = Color::White;
    std::optional<double> score;  // 1 = won, 0.5 = drawn, 0 = lost; empty for unfinished games
    std::string opponent;
    std::string opening;   // "Sicilian Defense: Najdorf Variation"; may be empty
    SideReview review;
};

struct ColorStats {
    int games = 0;
    int scoredGames = 0;      // games with a result
    double scorePercent = 0;  // points won out of scoredGames, 0..100
    double accuracy = 0;      // average game accuracy
};

struct OpeningStats {
    std::string name;  // opening family, e.g. "Sicilian Defense"
    int games = 0;
    int scoredGames = 0;
    double scorePercent = 0;
    double accuracy = 0;
};

// Everything the Statistics section shows about a set of reviewed games.
struct PlayerStats {
    int games = 0;
    int wins = 0;
    int draws = 0;
    int losses = 0;
    double accuracy = 0;  // average game accuracy
    ColorStats white;
    ColorStats black;
    int moves = 0;                                   // judged moves in all games
    std::array<int, kQualityCount> counts{};         // indexed by MoveQuality
    std::array<PhaseSummary, kPhaseCount> phases{};  // indexed by GamePhase, weighted by moves
    std::optional<GamePhase> weakestPhase;           // lowest accuracy among phases with enough moves
    std::vector<OpeningStats> openings;              // most played first
    std::vector<double> accuracyTrend;               // one value per game, in the order given
};

// `games` should be in chronological order (the trend keeps that order).
PlayerStats summarize(const std::vector<PlayerGame>& games);

}  // namespace core

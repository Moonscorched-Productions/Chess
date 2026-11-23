#include "chess/Statistics.h"

#include <algorithm>
#include <cctype>

namespace core {

namespace {

std::string normalized(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

// Running totals for a group of games.
struct Tally {
    int games = 0;
    int scoredGames = 0;
    double points = 0;
    double accuracySum = 0;

    void add(const PlayerGame& game) {
        ++games;
        accuracySum += game.review.accuracy;
        if (game.score) {
            ++scoredGames;
            points += *game.score;
        }
    }
    double accuracy() const { return games > 0 ? accuracySum / games : 0.0; }
    double scorePercent() const { return scoredGames > 0 ? 100.0 * points / scoredGames : 0.0; }
};

ColorStats toColorStats(const Tally& tally) {
    return ColorStats{tally.games, tally.scoredGames, tally.scorePercent(), tally.accuracy()};
}

}  // namespace

bool matchesPlayer(std::string_view name, std::string_view list) {
    const std::string wanted = normalized(name);
    if (wanted.empty()) return false;
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t end = std::min(list.find(',', start), list.size());
        if (normalized(list.substr(start, end - start)) == wanted) return true;
        start = end + 1;
    }
    return false;
}

PlayerStats summarize(const std::vector<PlayerGame>& games) {
    PlayerStats stats;
    Tally all;
    Tally white;
    Tally black;
    std::array<double, kPhaseCount> phaseSum{};
    std::vector<std::pair<std::string, Tally>> openings;

    for (const PlayerGame& game : games) {
        if (game.review.moves == 0) continue;
        all.add(game);
        (game.color == Color::White ? white : black).add(game);
        if (game.score) {
            if (*game.score > 0.75) ++stats.wins;
            else if (*game.score < 0.25) ++stats.losses;
            else ++stats.draws;
        }

        stats.moves += game.review.moves;
        for (std::size_t i = 0; i < stats.counts.size(); ++i) stats.counts[i] += game.review.counts[i];
        for (std::size_t i = 0; i < stats.phases.size(); ++i) {
            stats.phases[i].moves += game.review.phases[i].moves;
            phaseSum[i] += game.review.phases[i].accuracy * game.review.phases[i].moves;
        }
        stats.accuracyTrend.push_back(game.review.accuracy);

        // Variations are grouped under their opening family (the part before the colon).
        std::string family = game.opening.substr(0, game.opening.find(':'));
        if (family.empty()) family = "Unknown opening";
        auto found = std::find_if(openings.begin(), openings.end(), [&](const auto& entry) { return entry.first == family; });
        if (found == openings.end()) found = openings.insert(openings.end(), {family, Tally{}});
        found->second.add(game);
    }

    stats.games = all.games;
    stats.accuracy = all.accuracy();
    stats.white = toColorStats(white);
    stats.black = toColorStats(black);

    // A phase needs a handful of moves before it can be called a weakness.
    constexpr int kMinPhaseMoves = 10;
    for (std::size_t i = 0; i < stats.phases.size(); ++i) {
        if (stats.phases[i].moves == 0) continue;
        stats.phases[i].accuracy = phaseSum[i] / stats.phases[i].moves;
        if (stats.phases[i].moves < kMinPhaseMoves) continue;
        if (!stats.weakestPhase || stats.phases[i].accuracy < stats.phases[static_cast<std::size_t>(*stats.weakestPhase)].accuracy) {
            stats.weakestPhase = static_cast<GamePhase>(i);
        }
    }

    for (const auto& [name, tally] : openings) {
        stats.openings.push_back(OpeningStats{name, tally.games, tally.scoredGames, tally.scorePercent(), tally.accuracy()});
    }
    std::stable_sort(stats.openings.begin(), stats.openings.end(),
                     [](const OpeningStats& a, const OpeningStats& b) { return a.games > b.games; });
    return stats;
}

}  // namespace core

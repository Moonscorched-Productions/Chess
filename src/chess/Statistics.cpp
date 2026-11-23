#include "chess/Statistics.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

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

// Howard Hinnant's civil-date algorithms.
std::optional<int> dayNumberFromIso(std::string_view date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return std::nullopt;
    const auto number = [&](std::size_t from, std::size_t count) {
        int value = 0;
        for (std::size_t i = from; i < from + count; ++i) {
            if (date[i] < '0' || date[i] > '9') return -1;
            value = value * 10 + (date[i] - '0');
        }
        return value;
    };
    int y = number(0, 4);
    const int m = number(5, 2);
    const int d = number(8, 2);
    if (y < 0 || m < 1 || m > 12 || d < 1 || d > 31) return std::nullopt;
    y -= m <= 2 ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

std::string isoFromDayNumber(int day) {
    const int z = day + 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const int doe = z - era * 146097;
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153;
    const int d = doy - (153 * mp + 2) / 5 + 1;
    const int m = mp + (mp < 10 ? 3 : -9);
    const int y = yoe + era * 400 + (m <= 2 ? 1 : 0);
    char text[16];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d", y, m, d);
    return text;
}

Streaks computeStreaks(std::vector<int> days, int today) {
    Streaks streaks;
    std::sort(days.begin(), days.end());
    days.erase(std::unique(days.begin(), days.end()), days.end());
    int run = 0;
    for (std::size_t i = 0; i < days.size(); ++i) {
        run = (i > 0 && days[i] == days[i - 1] + 1) ? run + 1 : 1;
        streaks.longest = std::max(streaks.longest, run);
    }
    // The current streak ends today, or yesterday when today has not been counted yet.
    if (!days.empty() && (days.back() == today || days.back() == today - 1)) {
        streaks.current = 1;
        for (std::size_t i = days.size() - 1; i > 0 && days[i - 1] == days[i] - 1; --i) ++streaks.current;
    }
    return streaks;
}

}  // namespace core

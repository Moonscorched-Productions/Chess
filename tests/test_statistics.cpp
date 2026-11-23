#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "chess/Pgn.h"
#include "chess/Statistics.h"
#include "storage/ReviewStore.h"

using namespace core;

namespace {

PlayerGame played(Color color, double score, double accuracy, const char* opening) {
    PlayerGame game;
    game.color = color;
    game.score = score;
    game.opening = opening;
    game.review.moves = 30;
    game.review.accuracy = accuracy;
    game.review.counts[static_cast<std::size_t>(MoveQuality::Blunder)] = 1;
    game.review.phases[0] = PhaseSummary{10, 95.0};
    game.review.phases[1] = PhaseSummary{15, accuracy};
    game.review.phases[2] = PhaseSummary{5, 60.0};
    return game;
}

GameRecord stored(const char* white, const char* black, const char* result, const char* moves) {
    const std::string pgn = std::string("[Date \"2026.03.04\"]\n[White \"") + white + "\"]\n[Black \"" + black +
                            "\"]\n[Result \"" + result + "\"]\n\n" + moves + " " + result + "\n";
    std::string error;
    auto record = parsePgn(pgn, &error);
    REQUIRE_MESSAGE(record.has_value(), error);
    return std::move(*record);
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() / "chess_review_test";
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}  // namespace

TEST_CASE("player names match without regard to case and spaces") {
    CHECK(matchesPlayer("You", "You"));
    CHECK(matchesPlayer("magnus", "You, Magnus ,DrNykterstein"));
    CHECK(matchesPlayer(" DrNykterstein ", "You,Magnus,drnykterstein"));
    CHECK_FALSE(matchesPlayer("Magnus Carlsen", "You, Magnus"));
    CHECK_FALSE(matchesPlayer("", "You,,"));
    CHECK_FALSE(matchesPlayer("You", ""));
}

TEST_CASE("statistics summarize reviewed games") {
    const std::vector<PlayerGame> games = {
        played(Color::White, 1.0, 90.0, "Italian Game: Giuoco Piano"),
        played(Color::White, 0.0, 70.0, "Italian Game"),
        played(Color::Black, 0.5, 80.0, "Sicilian Defense: Najdorf Variation"),
    };
    const PlayerStats stats = summarize(games);

    CHECK(stats.games == 3);
    CHECK(stats.wins == 1);
    CHECK(stats.draws == 1);
    CHECK(stats.losses == 1);
    CHECK(stats.accuracy == doctest::Approx(80.0));
    CHECK(stats.white.games == 2);
    CHECK(stats.white.scorePercent == doctest::Approx(50.0));
    CHECK(stats.white.accuracy == doctest::Approx(80.0));
    CHECK(stats.black.games == 1);
    CHECK(stats.black.scorePercent == doctest::Approx(50.0));
    CHECK(stats.moves == 90);
    CHECK(stats.counts[static_cast<std::size_t>(MoveQuality::Blunder)] == 3);
    CHECK(stats.accuracyTrend == std::vector<double>{90.0, 70.0, 80.0});

    CHECK(stats.phases[0].moves == 30);
    CHECK(stats.phases[0].accuracy == doctest::Approx(95.0));
    CHECK(stats.phases[1].accuracy == doctest::Approx(80.0));
    REQUIRE(stats.weakestPhase.has_value());
    CHECK(*stats.weakestPhase == GamePhase::Endgame);

    // Variations are grouped by opening family, most played first.
    REQUIRE(stats.openings.size() == 2);
    CHECK(stats.openings[0].name == "Italian Game");
    CHECK(stats.openings[0].games == 2);
    CHECK(stats.openings[0].scorePercent == doctest::Approx(50.0));
    CHECK(stats.openings[1].name == "Sicilian Defense");
}

TEST_CASE("statistics of no games are empty") {
    const PlayerStats stats = summarize({});
    CHECK(stats.games == 0);
    CHECK(stats.openings.empty());
    CHECK_FALSE(stats.weakestPhase.has_value());
}

TEST_CASE("review store: save, list and player games") {
    using storage::GameId;
    TempDir dir;
    const auto file = dir.path / "reviews.db";
    std::string error;
    const auto games = storage::GameStore::open(file, &error);
    REQUIRE_MESSAGE(games != nullptr, error);
    const auto reviews = storage::ReviewStore::open(file, &error);
    REQUIRE_MESSAGE(reviews != nullptr, error);

    const auto won = games->add(stored("You", "Stockfish", "1-0", "1. e4 e5 2. Nf3"), "play");
    const auto lost = games->add(stored("Bob", "you", "1-0", "1. d4 d5"), "import");
    const auto other = games->add(stored("Alice", "Bob", "1/2-1/2", "1. c4 c5"), "import");
    REQUIRE((won && lost && other));
    CHECK(reviews->unreviewed() == std::vector<GameId>{*won, *lost, *other});
    CHECK(reviews->accuracies().empty());

    GameReview review;
    review.white.moves = 20;
    review.white.accuracy = 91.5;
    review.white.counts[static_cast<std::size_t>(MoveQuality::Best)] = 12;
    review.white.counts[static_cast<std::size_t>(MoveQuality::Blunder)] = 1;
    review.white.phases[1] = PhaseSummary{8, 88.0};
    review.black.moves = 19;
    review.black.accuracy = 72.25;
    review.black.phases[2] = PhaseSummary{4, 55.0};
    REQUIRE(reviews->save(*won, review, "Italian Game"));
    REQUIRE(reviews->save(*lost, review, "Queen Pawn Game"));
    REQUIRE(reviews->save(*other, review, ""));
    // Saving again replaces the earlier review.
    review.white.accuracy = 93.0;
    REQUIRE(reviews->save(*won, review, "Italian Game"));

    CHECK(reviews->unreviewed().empty());
    const auto accuracies = reviews->accuracies();
    REQUIRE(accuracies.size() == 3);
    CHECK(accuracies.at(*won).white == doctest::Approx(93.0));
    CHECK(accuracies.at(*won).black == doctest::Approx(72.25));

    // Only games with exactly one matching player count, seen from the side of that player.
    const auto mine = reviews->playerGames("You");
    REQUIRE(mine.size() == 2);
    CHECK(mine[0].gameId == *won);
    CHECK(mine[0].color == Color::White);
    CHECK(mine[0].score == 1.0);
    CHECK(mine[0].opponent == "Stockfish");
    CHECK(mine[0].opening == "Italian Game");
    CHECK(mine[0].review.accuracy == doctest::Approx(93.0));
    CHECK(mine[0].review.count(MoveQuality::Best) == 12);
    CHECK(mine[0].review.count(MoveQuality::Blunder) == 1);
    CHECK(mine[0].review.phases[1].moves == 8);
    CHECK(mine[0].review.phases[1].accuracy == doctest::Approx(88.0));
    CHECK(mine[1].color == Color::Black);
    CHECK(mine[1].score == 0.0);
    CHECK(mine[1].review.accuracy == doctest::Approx(72.25));
    CHECK(mine[1].review.phases[2].moves == 4);
    CHECK(reviews->playerGames("Alice, Bob").size() == 1);  // "Alice vs Bob" matches both sides and is left out
    CHECK(reviews->playerGames("Nobody").empty());

    // Deleting a game takes its review out of the lists.
    REQUIRE(games->remove(*lost));
    CHECK(reviews->accuracies().size() == 2);
    CHECK(reviews->playerGames("You").size() == 1);
    REQUIRE(reviews->remove(*won));
    CHECK(reviews->unreviewed() == std::vector<GameId>{*won});
}

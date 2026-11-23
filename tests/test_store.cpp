#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "chess/Pgn.h"
#include "storage/GameStore.h"

using namespace storage;

namespace {

core::GameRecord game(const char* white, const char* black, const char* result, const char* moves) {
    const std::string pgn = std::string("[Event \"Club night\"]\n[Date \"2026.03.04\"]\n[White \"") + white +
                            "\"]\n[Black \"" + black + "\"]\n[Result \"" + result + "\"]\n\n" + moves + " " + result + "\n";
    std::string error;
    auto record = core::parsePgn(pgn, &error);
    REQUIRE_MESSAGE(record.has_value(), error);
    return std::move(*record);
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() / "chess_store_test";
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}  // namespace

TEST_CASE("game store: add, list, read, update, remove") {
    std::string error;
    const auto store = GameStore::open(":memory:", &error);
    REQUIRE_MESSAGE(store != nullptr, error);
    CHECK(store->count() == 0);
    CHECK(store->list().empty());

    const auto first = store->add(game("Alice", "Bob", "1-0", "1. e4 e5 2. Nf3 Nc6 3. Bb5"), "play");
    const auto second = store->add(game("Carol", "Alice O'Neil", "0-1", "1. f3 e5 2. g4 Qh4#"), "import");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(store->count() == 2);

    SUBCASE("list is newest first with header fields") {
        const auto games = store->list();
        REQUIRE(games.size() == 2);
        CHECK(games[0].id == *second);
        CHECK(games[0].white == "Carol");
        CHECK(games[0].black == "Alice O'Neil");
        CHECK(games[0].result == "0-1");
        CHECK(games[0].moves == 2);
        CHECK(games[0].source == "import");
        CHECK(games[1].white == "Alice");
        CHECK(games[1].date == "2026.03.04");
        CHECK(games[1].event == "Club night");
        CHECK(games[1].moves == 3);
    }
    SUBCASE("filters") {
        GameFilter filter;
        filter.text = "alice";
        CHECK(store->list(filter).size() == 2);
        filter.text = "Bob";
        CHECK(store->list(filter).size() == 1);
        filter.text = "O'Neil";
        CHECK(store->list(filter).size() == 1);
        filter.text = "%";  // wildcards are literal characters
        CHECK(store->list(filter).empty());
        filter.text = "'; DROP TABLE games; --";
        CHECK(store->list(filter).empty());
        CHECK(store->count() == 2);
        filter.text.clear();
        filter.result = "0-1";
        const auto lost = store->list(filter);
        REQUIRE(lost.size() == 1);
        CHECK(lost[0].white == "Carol");
        filter.result.clear();
        filter.limit = 1;
        CHECK(store->list(filter).size() == 1);
    }
    SUBCASE("PGN round trip") {
        const auto text = store->pgn(*first);
        REQUIRE(text.has_value());
        const auto record = core::parsePgn(*text);
        REQUIRE(record.has_value());
        CHECK(record->tag("White") == "Alice");
        CHECK(record->tree.moveCount() == 5);
        CHECK_FALSE(store->pgn(9999).has_value());
    }
    SUBCASE("update and remove") {
        CHECK(store->update(*first, game("Alice", "Dave", "1/2-1/2", "1. d4 d5")));
        const auto games = store->list();
        CHECK(games[1].black == "Dave");
        CHECK(games[1].result == "1/2-1/2");
        CHECK(games[1].moves == 1);
        CHECK_FALSE(store->update(9999, game("X", "Y", "*", "1. d4")));

        CHECK(store->remove(*first));
        CHECK_FALSE(store->remove(*first));
        CHECK(store->count() == 1);
    }
}

TEST_CASE("game store: time in the app") {
    std::string error;
    const auto store = GameStore::open(":memory:", &error);
    REQUIRE_MESSAGE(store != nullptr, error);
    CHECK(store->usage().totalSeconds == 0);
    CHECK(store->usage().days == 0);

    store->addUsage(30);
    store->addUsage(45);
    store->addUsage(0);
    store->addUsage(-5);  // ignored
    const Usage usage = store->usage();
    CHECK(usage.totalSeconds == 75);
    CHECK(usage.todaySeconds == 75);
    CHECK(usage.days == 1);
}

TEST_CASE("game store: time in the app survives reopening") {
    TempDir dir;
    const auto file = dir.path / "games.sqlite";
    {
        const auto store = GameStore::open(file);
        REQUIRE(store != nullptr);
        store->addUsage(42);
    }
    const auto store = GameStore::open(file);
    REQUIRE(store != nullptr);
    CHECK(store->usage().totalSeconds == 42);
}

TEST_CASE("game store: import and export files") {
    TempDir temp;
    const std::filesystem::path source = temp.path / "games.pgn";
    {
        std::ofstream out(source, std::ios::binary);
        out << "[White \"A\"]\n[Black \"B\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. Nf3 1-0\n\n"
            << "[White \"Broken\"]\n[Black \"Game\"]\n\n1. e4 Ke3 *\n\n"
            << "[White \"C\"]\n[Black \"D\"]\n[Result \"1/2-1/2\"]\n\n1. d4 (1. c4 c5) d5 1/2-1/2\n\n"
            << "[White \"Empty\"]\n[Black \"Entry\"]\n\n*\n";
    }

    std::string error;
    const auto store = GameStore::open(temp.path / "games.db", &error);
    REQUIRE_MESSAGE(store != nullptr, error);

    std::atomic<int> progress{0};
    const ImportResult result = store->importFile(source, &progress);
    CHECK(result.error.empty());
    CHECK(result.imported == 2);
    CHECK(result.skipped == 2);
    CHECK(progress.load() == 4);
    CHECK(store->count() == 2);

    CHECK_FALSE(store->importFile(temp.path / "missing.pgn").error.empty());

    SUBCASE("a second connection sees the data") {
        const auto other = GameStore::open(temp.path / "games.db", &error);
        REQUIRE(other != nullptr);
        CHECK(other->count() == 2);
    }
    SUBCASE("export all and re-import") {
        const std::filesystem::path exported = temp.path / "out.pgn";
        REQUIRE(store->exportFile(exported));
        const auto fresh = GameStore::open(":memory:");
        REQUIRE(fresh != nullptr);
        const ImportResult again = fresh->importFile(exported);
        CHECK(again.imported == 2);
        CHECK(again.skipped == 0);
        const auto games = fresh->list();
        REQUIRE(games.size() == 2);
        CHECK(games[0].white == "C");
        // Variations survive the round trip.
        const auto record = core::parsePgn(*fresh->pgn(games[0].id));
        REQUIRE(record.has_value());
        CHECK(record->tree.moveCount() == 4);
    }
    SUBCASE("export a selection") {
        const std::filesystem::path exported = temp.path / "one.pgn";
        const auto games = store->list();
        REQUIRE(store->exportFile(exported, {games.back().id}));
        const auto fresh = GameStore::open(":memory:");
        CHECK(fresh->importFile(exported).imported == 1);
        CHECK(fresh->list()[0].white == "A");
    }
    SUBCASE("cancel stops an import") {
        const auto fresh = GameStore::open(":memory:");
        std::atomic<bool> cancel{true};
        CHECK(fresh->importFile(source, nullptr, &cancel).imported == 0);
    }
}

// --- Puzzles and training ----------------------------------------------------

#include <sstream>

#include "storage/PuzzleStore.h"
#include "storage/TrainingStore.h"

TEST_CASE("puzzle store: import, selection, attempts and rating") {
    const auto store = PuzzleStore::open(":memory:");
    REQUIRE(store != nullptr);
    CHECK(store->count() == 0);
    CHECK_FALSE(store->random({}, 1).has_value());
    CHECK_FALSE(store->daily(20000).has_value());

    std::istringstream csv(
        "PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags\n"
        "0000D,5rk1/1p3ppp/pq3b2/8/8/1P1Q1N2/P4PPP/3R2K1 w - - 2 27,d3d6 f8d8 d6d8 f6d8,1433,75,96,37700,advantage endgame short,https://x,\n"
        "easy1,6k1/8/8/8/8/5n2/P3q3/7K w - - 0 1,a2a3 e2h2,800,80,95,5000,mate mateIn1 oneMove,,\n"
        "hard1,6k1/8/8/8/8/5n2/P3q3/7K w - - 0 1,a2a3 e2h2,2400,80,95,5000,mate fork,,\n"
        "broken,not-a-fen,,abc\n"
        "0000D,duplicate/id w,a2a3 e2h2,1,1,1,1,x\n");
    std::atomic<int> progress{0};
    const PuzzleImportResult result = store->importCsv(csv, &progress);
    CHECK(result.error.empty());
    CHECK(result.imported == 3);
    CHECK(result.skipped == 2);
    CHECK(progress.load() == 6);
    CHECK(store->count() == 3);

    const auto puzzle = store->get("0000D");
    REQUIRE(puzzle.has_value());
    CHECK(puzzle->rating == 1433);
    CHECK(puzzle->moves.size() == 4);
    CHECK(puzzle->themes == "advantage endgame short");
    CHECK_FALSE(store->get("nope").has_value());

    SUBCASE("filters by rating and theme") {
        PuzzleFilter filter;
        filter.minRating = 700;
        filter.maxRating = 900;
        for (std::uint64_t seed = 0; seed < 5; ++seed) CHECK(store->random(filter, seed)->id == "easy1");
        filter = {};
        filter.theme = "fork";
        CHECK(store->random(filter, 7)->id == "hard1");
        filter.theme = "for";  // themes match whole words only
        CHECK_FALSE(store->random(filter, 7).has_value());
        filter.theme = "%";
        CHECK_FALSE(store->random(filter, 7).has_value());
        filter = {};
        filter.minRating = 3000;
        CHECK_FALSE(store->random(filter, 7).has_value());
        CHECK(store->daily(20000).has_value());
        CHECK(store->daily(20000)->id == store->daily(20000)->id);
    }
    SUBCASE("attempts move the rating and feed the statistics") {
        CHECK(store->stats().rating == 1500);
        const int afterWin = store->recordAttempt(*puzzle, true, 12.5, true);
        CHECK(afterWin > 1500);
        const int afterUnrated = store->recordAttempt(*store->get("hard1"), false, 30.0, false);
        CHECK(afterUnrated == afterWin);
        const int afterLoss = store->recordAttempt(*store->get("easy1"), false, 5.0, true);
        CHECK(afterLoss < afterWin);

        const PuzzleStats stats = store->stats();
        CHECK(stats.attempts == 3);
        CHECK(stats.solved == 1);
        CHECK(stats.streak == 0);
        CHECK(stats.bestStreak == 1);
        CHECK(stats.rating == afterLoss);
        CHECK(stats.ratedAttempts == 2);

        const auto history = store->history();
        REQUIRE(history.size() == 3);
        CHECK(history[0].puzzleId == "easy1");
        CHECK_FALSE(history[0].solved);
        CHECK(history[2].seconds == doctest::Approx(12.5));
        CHECK(store->ratingHistory() == std::vector<int>{afterWin, afterLoss});

        // Solved puzzles are not served again; failed ones come back for a retry.
        PuzzleFilter filter;
        for (std::uint64_t seed = 0; seed < 6; ++seed) CHECK(store->random(filter, seed)->id != "0000D");
        CHECK(store->failedCount() == 2);
        REQUIRE(store->nextFailed().has_value());
        CHECK(store->nextFailed()->id == "hard1");
        store->recordAttempt(*store->get("hard1"), true, 9.0, false);
        CHECK(store->failedCount() == 1);
        CHECK(store->nextFailed()->id == "easy1");
        CHECK(store->stats().streak == 1);
    }
}

TEST_CASE("training store: repertoires, cards and endgame results") {
    const auto store = TrainingStore::open(":memory:");
    REQUIRE(store != nullptr);
    CHECK(store->repertoires().empty());

    const auto white = store->createRepertoire("Italian Game", core::Color::White, "Main");
    const auto black = store->createRepertoire("Sicilian", core::Color::Black);
    REQUIRE(white.has_value());
    REQUIRE(black.has_value());
    auto list = store->repertoires();
    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "Sicilian");  // sorted by folder first; no folder sorts ahead
    CHECK(list[1].name == "Italian Game");
    CHECK(list[1].color == core::Color::White);
    CHECK(list[1].folder == "Main");

    SUBCASE("trees round trip with variations") {
        CHECK(store->loadTree(*white).moveCount() == 0);
        const auto record = core::parsePgn("1. e4 e5 2. Nf3 Nc6 3. Bc4 Bc5 (3... Nf6 4. d3) 4. c3 *");
        REQUIRE(record.has_value());
        CHECK(store->saveTree(*white, record->tree));
        const core::GameTree loaded = store->loadTree(*white);
        CHECK(loaded.moveCount() == 9);
        CHECK(core::allLines(loaded).size() == 2);
        CHECK_FALSE(store->saveTree(9999, record->tree));
    }
    SUBCASE("rename, cards and removal") {
        list[1].name = "Giuoco Piano";
        list[1].folder.clear();
        CHECK(store->updateRepertoire(list[1]));
        CHECK(store->repertoires()[0].name == "Giuoco Piano");

        CHECK(store->cards(*white).empty());
        core::CardState card = core::reviewCard({}, true, 100);
        CHECK(store->saveCard(*white, "key one", card));
        card = core::reviewCard(card, true, 101);
        CHECK(store->saveCard(*white, "key one", card));
        CHECK(store->saveCard(*white, "key two", core::reviewCard({}, false, 100)));
        CHECK(store->saveCard(*black, "key one", core::CardState{}));
        const auto cards = store->cards(*white);
        REQUIRE(cards.size() == 2);
        CHECK(cards.at("key one").intervalDays == 3);
        CHECK(cards.at("key one").reviews == 2);
        CHECK(cards.at("key two").lapses == 1);

        CHECK(store->removeRepertoire(*white));
        CHECK(store->cards(*white).empty());
        CHECK(store->cards(*black).size() == 1);
        CHECK(store->repertoires().size() == 1);
        CHECK_FALSE(store->removeRepertoire(*white));
    }
    SUBCASE("endgame results accumulate") {
        store->recordEndgame("kq-k", false);
        store->recordEndgame("kq-k", true);
        store->recordEndgame("lucena", true);
        const auto progress = store->endgameProgress();
        REQUIRE(progress.size() == 2);
        CHECK(progress.at("kq-k").attempts == 2);
        CHECK(progress.at("kq-k").successes == 1);
        CHECK(progress.at("lucena").successes == 1);
    }
}

#include <doctest/doctest.h>

#include <string>

#include "chess/OpeningBook.h"
#include "chess/Pgn.h"
#include "chess/Puzzle.h"
#include "chess/Repertoire.h"

using namespace core;

namespace {

Move uci(const char* text) { return *parseUciMove(text); }

// A real Lichess puzzle (id 0000D): White has just played Qd6, Black wins the queen.
Puzzle samplePuzzle() {
    Puzzle puzzle;
    puzzle.id = "0000D";
    puzzle.fen = "5rk1/1p3ppp/pq3b2/8/8/1P1Q1N2/P4PPP/3R2K1 w - - 2 27";
    puzzle.moves = splitMoves("d3d6 f8d8 d6d8 f6d8");
    puzzle.rating = 1433;
    return puzzle;
}

}  // namespace

TEST_CASE("UCI move parsing") {
    CHECK(parseUciMove("e2e4") == Move{makeSquare(4, 1), makeSquare(4, 3)});
    CHECK(parseUciMove("e7e8q")->promotion == PieceType::Queen);
    CHECK_FALSE(parseUciMove("e2").has_value());
    CHECK_FALSE(parseUciMove("e2e9").has_value());
    CHECK_FALSE(parseUciMove("e7e8k").has_value());
    CHECK(splitMoves("  e2e4  e7e5 ") == std::vector<std::string>{"e2e4", "e7e5"});

    // Castling written as "king takes rook" is understood.
    Game game;
    REQUIRE(game.reset("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    CHECK(game.makeUciMove("e1h1"));
    CHECK(game.history().back().san == "O-O");
    CHECK(game.makeUciMove("e8a8"));
    CHECK(game.history().back().san == "O-O-O");
}

TEST_CASE("puzzle session: solving") {
    PuzzleSession session;
    CHECK_FALSE(session.active());
    CHECK(session.play(uci("e2e4")) == PuzzleSession::Outcome::Ignored);

    REQUIRE(session.start(samplePuzzle()));
    CHECK(session.active());
    CHECK(session.solverColor() == Color::Black);
    CHECK(session.game().sideToMove() == Color::Black);  // the opponent's move was played
    CHECK(session.game().history().back().san == "Qd6");
    CHECK(session.solverMovesTotal() == 2);
    CHECK(session.solverMovesDone() == 0);
    CHECK(session.solutionSan() == "27... Rd8 28. Qxd8+ Bxd8");
    CHECK(session.expectedMove() == uci("f8d8"));

    SUBCASE("wrong and illegal moves leave the position alone") {
        const std::string before = session.game().fen();
        CHECK(session.play(uci("g8h8")) == PuzzleSession::Outcome::Wrong);
        CHECK(session.play(uci("g8g5")) == PuzzleSession::Outcome::Ignored);  // not legal
        CHECK(session.game().fen() == before);
        CHECK(session.mistakes() == 1);
        CHECK_FALSE(session.solved());
    }
    SUBCASE("the full line") {
        CHECK(session.play(uci("f8d8")) == PuzzleSession::Outcome::Correct);
        CHECK(session.replyPending());
        CHECK_FALSE(session.expectedMove().has_value());
        CHECK(session.play(uci("f6d8")) == PuzzleSession::Outcome::Ignored);  // not the solver's turn yet
        session.playReply();
        CHECK_FALSE(session.replyPending());
        CHECK(session.solverMovesDone() == 1);
        CHECK(session.play(uci("f6d8")) == PuzzleSession::Outcome::Solved);
        CHECK(session.solved());
        CHECK(session.mistakes() == 0);
        CHECK(session.play(uci("a6a5")) == PuzzleSession::Outcome::Ignored);
    }
    SUBCASE("showing the solution") {
        int revealed = 0;
        while (session.revealNext()) ++revealed;
        CHECK(revealed == 3);
        CHECK(session.solved());
    }
}

TEST_CASE("puzzle session: any checkmate is accepted, bad puzzles are rejected") {
    // Lead-in a2-a3; the listed solution is Qh2#, but Qf1# mates as well.
    Puzzle puzzle;
    puzzle.fen = "6k1/8/8/8/8/5n2/P3q3/7K w - - 0 1";
    puzzle.moves = {"a2a3", "e2h2"};
    PuzzleSession session;
    REQUIRE(session.start(puzzle));
    CHECK(session.play(uci("e2g2")) == PuzzleSession::Outcome::Wrong);  // check, but the king takes
    CHECK(session.play(uci("e2f1")) == PuzzleSession::Outcome::Solved);
    CHECK(session.game().status() == Status::Checkmate);

    Puzzle broken = samplePuzzle();
    broken.moves = {"d3d6"};
    CHECK_FALSE(session.start(broken));
    broken.moves = {"d3d6", "a1a8"};
    CHECK_FALSE(session.start(broken));
    broken = samplePuzzle();
    broken.fen = "not a fen";
    CHECK_FALSE(session.start(broken));
    CHECK_FALSE(session.active());
}

TEST_CASE("rating updates") {
    CHECK(updatedRating(1500, 1500, true, 100) == 1510);
    CHECK(updatedRating(1500, 1500, false, 100) == 1490);
    CHECK(updatedRating(1500, 1500, true, 0) == 1524);  // newcomers move faster
    CHECK(updatedRating(1500, 2300, false, 100) == 1500);  // losing to a far harder puzzle costs nothing
    CHECK(updatedRating(1500, 2300, true, 100) > 1515);
    CHECK(updatedRating(120, 100, false, 0) >= 100);
}

TEST_CASE("spaced repetition schedule") {
    CardState card;
    CHECK(card.isNew());
    CHECK(card.isDue(100));

    card = reviewCard(card, true, 100);
    CHECK(card.intervalDays == 1);
    CHECK(card.dueDay == 101);
    CHECK_FALSE(card.isDue(100));
    card = reviewCard(card, true, 101);
    CHECK(card.intervalDays == 3);
    card = reviewCard(card, true, 104);
    CHECK(card.intervalDays == 8);  // 3 * 2.6
    CHECK(card.dueDay == 112);
    CHECK(card.streak == 3);

    const double easeBefore = card.ease;
    card = reviewCard(card, false, 112);
    CHECK(card.intervalDays == 0);
    CHECK(card.isDue(112));
    CHECK(card.lapses == 1);
    CHECK(card.streak == 0);
    CHECK(card.ease < easeBefore);
    card = reviewCard(card, true, 112);
    CHECK(card.intervalDays == 1);  // starts over

    for (int i = 0; i < 20; ++i) card = reviewCard(card, false, 200);
    CHECK(card.ease == doctest::Approx(1.3));
    for (int i = 0; i < 40; ++i) card = reviewCard(card, true, 200);
    CHECK(card.intervalDays <= 365);
    CHECK(todayNumber() > 20000);
}

TEST_CASE("repertoire helpers") {
    CHECK(positionKey("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1") ==
          "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq");
    CHECK(positionKey("8/8/8/8/8/8/8/K6k w -") == "8/8/8/8/8/8/8/K6k w -");

    const auto record = parsePgn("1. e4 e5 (1... c5 2. Nf3 (2. Nc3) d6) 2. Nf3 Nc6 3. Bb5 *");
    REQUIRE(record.has_value());
    const GameTree& tree = record->tree;

    const auto lines = allLines(tree);
    REQUIRE(lines.size() == 3);
    CHECK(tree.node(lines[0].back()).record.san == "Bb5");  // main line first
    CHECK(tree.node(lines[1].back()).record.san == "d6");
    CHECK(tree.node(lines[2].back()).record.san == "Nc3");

    // White decides at the start, after 1...e5, after 2...Nc6 and after 1...c5.
    CHECK(decisionNodes(tree, Color::White).size() == 4);
    // Black decides after 1.e4, after 2.Nf3 (main line) and after 2.Nf3 in the Sicilian.
    CHECK(decisionNodes(tree, Color::Black).size() == 3);
    CHECK(sideToMoveAt(tree, kRootNode) == Color::White);
    CHECK(sideToMoveAt(tree, lines[0].back()) == Color::Black);
}

TEST_CASE("opening book") {
    OpeningBook book;
    CHECK(book.empty());
    CHECK(book.loadText("eco\tname\tpgn\n"
                        "C20\tKing's Pawn Game\t1. e4 e5\n"
                        "C44\tKing's Knight Opening\t1. e4 e5 2. Nf3 Nc6\n"
                        "C60\tRuy Lopez\t1. e4 e5 2. Nf3 Nc6 3. Bb5\r\n"
                        "X00\tBroken line\t1. e4 Ke3\n"
                        "bad line without tabs\n") == 3);
    CHECK(book.size() == 3);

    // Reached by a different move order, then continued past the book.
    const auto record = parsePgn("1. Nf3 Nc6 2. e4 e5 3. Bb5 a6 4. Ba4 *");
    REQUIRE(record.has_value());
    const std::vector<NodeId> line = record->tree.mainLine();
    CHECK(book.identify(record->tree, line[0]) == nullptr);
    REQUIRE(book.identify(record->tree, line[3]) != nullptr);
    CHECK(book.identify(record->tree, line[3])->name == "King's Knight Opening");
    REQUIRE(book.identify(record->tree, line.back()) != nullptr);
    CHECK(book.identify(record->tree, line.back())->eco == "C60");
    CHECK(book.identify(record->tree, kRootNode) == nullptr);
}

#include <set>

#include "chess/Endgames.h"

TEST_CASE("endgame positions are valid and uniquely named") {
    std::set<std::string> ids;
    for (const EndgamePosition& position : endgamePositions()) {
        INFO("position ", position.id);
        std::string error;
        CHECK_MESSAGE(validateFen(position.fen, &error), error);
        CHECK(ids.insert(position.id).second);
        CHECK(std::string(position.name).size() > 3);
        CHECK(std::string(position.hint).size() > 10);
        Game game;
        REQUIRE(game.reset(position.fen));
        CHECK_FALSE(game.isOver());
        CHECK(findEndgame(position.id) == &position);
    }
    CHECK(ids.size() >= 12);
    CHECK(findEndgame("no-such-position") == nullptr);
}

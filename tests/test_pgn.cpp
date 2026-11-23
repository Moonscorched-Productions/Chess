#include <doctest/doctest.h>

#include <sstream>
#include <string>

#include "chess/GameTree.h"
#include "chess/Pgn.h"

using namespace core;

namespace {

Square sq(const char* name) { return makeSquare(name[0] - 'a', name[1] - '1'); }

// Plays UCI moves from `parent`, returning the last node.
NodeId playLine(GameTree& tree, NodeId parent, std::initializer_list<const char*> moves) {
    Game game;
    game.setEnforceDraws(false);
    REQUIRE(loadPosition(tree, parent, game));
    NodeId current = parent;
    for (const char* uci : moves) {
        INFO("move ", uci);
        const Move move{sq(uci), sq(uci + 2)};
        current = playMove(tree, current, game, move);
        REQUIRE(current != kNoNode);
    }
    return current;
}

std::string sanLine(const GameTree& tree, NodeId id) {
    std::string result;
    for (const NodeId step : tree.pathTo(id)) {
        if (!result.empty()) result += ' ';
        result += tree.node(step).record.san;
    }
    return result;
}

}  // namespace

TEST_CASE("game tree: main line and variations") {
    GameTree tree;
    const NodeId e4 = playLine(tree, kRootNode, {"e2e4"});
    const NodeId nf3 = playLine(tree, e4, {"e7e5", "g1f3"});
    const NodeId c5 = playLine(tree, e4, {"c7c5"});
    const NodeId nc3 = playLine(tree, c5, {"b1c3"});

    CHECK(tree.moveCount() == 5);
    CHECK(sanLine(tree, nf3) == "e4 e5 Nf3");
    CHECK(sanLine(tree, nc3) == "e4 c5 Nc3");
    CHECK(tree.isMainLine(nf3));
    CHECK_FALSE(tree.isMainLine(c5));
    CHECK_FALSE(tree.isMainLine(nc3));
    CHECK(tree.lineEnd(kRootNode) == nf3);
    CHECK(tree.mainLine().size() == 3);
    CHECK(tree.uciMoves(nc3) == "e2e4 c7c5 b1c3");
    CHECK(tree.node(nc3).ply == 3);
    CHECK(tree.moveNumber(nc3) == 2);
    CHECK(tree.mover(nc3) == Color::White);
    CHECK(tree.mover(c5) == Color::Black);
    CHECK(tree.moveNumber(c5) == 1);

    SUBCASE("replaying an existing move does not duplicate it") {
        CHECK(playLine(tree, e4, {"c7c5"}) == c5);
        CHECK(tree.moveCount() == 5);
    }
    SUBCASE("illegal moves are not added") {
        Game game;
        REQUIRE(loadPosition(tree, e4, game));
        CHECK(playMove(tree, e4, game, Move{sq("e2"), sq("e4")}) == kNoNode);
        CHECK(tree.moveCount() == 5);
    }
    SUBCASE("promote and make main line") {
        tree.promote(c5);
        CHECK(tree.isMainLine(c5));
        CHECK(tree.lineEnd(kRootNode) == nc3);
        tree.makeMainLine(nf3);
        CHECK(tree.isMainLine(nf3));
        CHECK(tree.lineEnd(kRootNode) == nf3);
    }
    SUBCASE("remove deletes the whole branch") {
        tree.remove(c5);
        CHECK_FALSE(tree.contains(c5));
        CHECK_FALSE(tree.contains(nc3));
        CHECK(tree.contains(nf3));
        CHECK(tree.moveCount() == 3);
        CHECK(tree.node(e4).children.size() == 1);
        tree.remove(kRootNode);  // ignored
        CHECK(tree.contains(kRootNode));
    }
    SUBCASE("positions can be restored") {
        Game game;
        REQUIRE(loadPosition(tree, nc3, game));
        CHECK(game.fen() == tree.node(nc3).fen);
        CHECK(game.pieceAt(sq("c3")).type == PieceType::Knight);
    }
}

TEST_CASE("game tree from a custom position with Black to move") {
    GameTree tree("7k/8/8/8/8/8/R7/K7 b - - 0 40");
    const NodeId first = playLine(tree, kRootNode, {"h8g8"});
    const NodeId second = playLine(tree, first, {"a2a7"});
    CHECK(tree.startSideToMove() == Color::Black);
    CHECK(tree.mover(first) == Color::Black);
    CHECK(tree.moveNumber(first) == 40);
    CHECK(tree.mover(second) == Color::White);
    CHECK(tree.moveNumber(second) == 41);
}

TEST_CASE("evaluation to win percentage") {
    Eval eval;
    CHECK(eval.whiteWinPercent() == doctest::Approx(50.0));
    eval.cp = 100;
    CHECK(eval.whiteWinPercent() > 55.0);
    CHECK(eval.whiteWinPercent() < 65.0);
    eval.cp = -100;
    CHECK(eval.whiteWinPercent() < 45.0);
    eval.mate = 3;
    CHECK(eval.whiteWinPercent() == doctest::Approx(100.0));
    eval.mate = -1;
    CHECK(eval.whiteWinPercent() == doctest::Approx(0.0));
}

TEST_CASE("PGN: tags, moves and result") {
    const char* pgn = R"([Event "Test \"quoted\" event"]
[Site "Home"]
[Date "2026.01.02"]
[Round "1"]
[White "Alice"]
[Black "Bob"]
[Result "0-1"]

1. f3 e5 2. g4 Qh4# 0-1
)";
    std::string error;
    const auto record = parsePgn(pgn, &error);
    REQUIRE_MESSAGE(record.has_value(), error);
    CHECK(record->tag("Event") == "Test \"quoted\" event");
    CHECK(record->tag("White") == "Alice");
    CHECK(record->tag("Result") == "0-1");
    CHECK(record->tag("Missing").empty());
    CHECK(record->tree.moveCount() == 4);
    CHECK(sanLine(record->tree, record->tree.lineEnd(kRootNode)) == "f3 e5 g4 Qh4#");

    const std::string written = writePgn(*record);
    CHECK(written.find("[Event \"Test \\\"quoted\\\" event\"]") != std::string::npos);
    CHECK(written.find("1. f3 e5 2. g4 Qh4# 0-1") != std::string::npos);
}

TEST_CASE("PGN: variations, comments, glyphs and evaluations") {
    const char* pgn = R"([Result "*"]

{Opening remarks} 1. e4 { [%eval 0.25,20] king's pawn } e5 $1 (1... c5!? 2. Nf3 (2. Nc3 ; a line comment
Nc6) d6 { [%eval #-3] }) 2. Nf3 Nc6?? 3. Bb5 *
)";
    std::string error;
    const auto record = parsePgn(pgn, &error);
    REQUIRE_MESSAGE(record.has_value(), error);
    const GameTree& tree = record->tree;

    CHECK(tree.node(kRootNode).comment == "Opening remarks");
    const std::vector<NodeId> main = tree.mainLine();
    REQUIRE(main.size() == 5);
    CHECK(sanLine(tree, main.back()) == "e4 e5 Nf3 Nc6 Bb5");

    const TreeNode& e4 = tree.node(main[0]);
    CHECK(e4.comment == "king's pawn");
    REQUIRE(e4.eval.has_value());
    CHECK(e4.eval->cp == 25);
    CHECK(e4.eval->depth == 20);
    CHECK_FALSE(e4.eval->mate.has_value());
    REQUIRE(e4.children.size() == 2);

    CHECK(tree.node(main[1]).nags == std::vector<int>{1});
    CHECK(tree.node(main[3]).nags == std::vector<int>{4});

    const TreeNode& c5 = tree.node(e4.children[1]);
    CHECK(c5.record.san == "c5");
    CHECK(c5.nags == std::vector<int>{5});
    REQUIRE(c5.children.size() == 2);
    CHECK(tree.node(c5.children[0]).record.san == "Nf3");
    const TreeNode& nc3 = tree.node(c5.children[1]);
    CHECK(nc3.record.san == "Nc3");
    CHECK(nc3.comment == "a line comment");
    REQUIRE(nc3.children.size() == 1);
    CHECK(tree.node(nc3.children[0]).record.san == "Nc6");

    const TreeNode& d6 = tree.node(tree.node(c5.children[0]).children[0]);
    CHECK(d6.record.san == "d6");
    REQUIRE(d6.eval.has_value());
    REQUIRE(d6.eval->mate.has_value());
    CHECK(*d6.eval->mate == -3);
    CHECK(d6.comment.empty());

    SUBCASE("writing and reading back gives the same game") {
        const std::string written = writePgn(*record);
        const auto again = parsePgn(written, &error);
        REQUIRE_MESSAGE(again.has_value(), error);
        CHECK(writePgn(*again) == written);
        CHECK(again->tree.moveCount() == tree.moveCount());
        std::string flat = written;
        for (char& c : flat)
            if (c == '\n') c = ' ';
        CHECK(flat.find("( 1... c5 $5 2. Nf3 ( 2. Nc3") != std::string::npos);
        CHECK(flat.find("[%eval 0.25,20] king's pawn") != std::string::npos);
        CHECK(written.find("[%eval #-3]") != std::string::npos);
        for (std::size_t start = 0; start < written.size();) {
            const std::size_t end = written.find('\n', start);
            CHECK(end - start <= 80);
            start = end + 1;
        }
    }
}

TEST_CASE("PGN: custom start position, castling with zeros, promotion") {
    const char* pgn = R"([FEN "r3k2r/6P1/8/8/8/8/8/R3K2R w KQkq - 0 1"]
[SetUp "1"]

1. 0-0 0-0-0 2. gxh8=Q *
)";
    std::string error;
    const auto record = parsePgn(pgn, &error);
    REQUIRE_MESSAGE(record.has_value(), error);
    CHECK(sanLine(record->tree, record->tree.lineEnd(kRootNode)) == "O-O O-O-O gxh8=Q");
}

TEST_CASE("PGN: errors") {
    std::string error;
    SUBCASE("empty text") {
        CHECK_FALSE(parsePgn("", &error).has_value());
        CHECK_FALSE(parsePgn("   \n\n ", &error).has_value());
        CHECK_FALSE(error.empty());
    }
    SUBCASE("illegal move names the move") {
        CHECK_FALSE(parsePgn("1. e4 e5 2. Ke3 *", &error).has_value());
        CHECK(error.find("Ke3") != std::string::npos);
    }
    SUBCASE("bad start position") {
        CHECK_FALSE(parsePgn("[FEN \"nonsense\"]\n\n1. e4 *", &error).has_value());
    }
    SUBCASE("variation before any move") {
        CHECK_FALSE(parsePgn("(1. d4) 1. e4 *", &error).has_value());
    }
    SUBCASE("unterminated comment and stray brackets do not crash") {
        const auto record = parsePgn("1. e4 ) e5 {never closed", &error);
        REQUIRE(record.has_value());
        CHECK(record->tree.moveCount() == 2);
        CHECK(record->tree.node(record->tree.lineEnd(kRootNode)).comment == "never closed");
    }
    SUBCASE("moves without numbers and a missing result are accepted") {
        const auto record = parsePgn("e4 e5 Nf3", &error);
        REQUIRE(record.has_value());
        CHECK(record->tree.moveCount() == 3);
        CHECK(record->tag("Result") == "*");
    }
}

TEST_CASE("PGN: games may continue past a draw by repetition") {
    std::string error;
    const auto record = parsePgn("1. Nf3 Nf6 2. Ng1 Ng8 3. Nf3 Nf6 4. Ng1 Ng8 5. e4 e5 1/2-1/2", &error);
    REQUIRE_MESSAGE(record.has_value(), error);
    CHECK(record->tree.moveCount() == 10);
    CHECK(record->tag("Result") == "1/2-1/2");
}

TEST_CASE("PGN: splitting a file with several games") {
    std::istringstream input(
        "[Event \"One\"]\n[Result \"1-0\"]\n\n1. e4 e5\n2. Nf3 1-0\n\n"
        "[Event \"Two\"]\n\n1. d4 { [%clk 0:01:00]\n[%eval 0.1] } d5 *\n"
        "\n\n[Event \"Three\"]\n[Result \"*\"]\n\n*\n");
    PgnSplitter splitter(input);
    std::string text;
    std::vector<std::string> events;
    std::vector<int> moves;
    while (splitter.next(text)) {
        const auto record = parsePgn(text);
        REQUIRE(record.has_value());
        events.push_back(record->tag("Event"));
        moves.push_back(record->tree.moveCount());
    }
    CHECK(events == std::vector<std::string>{"One", "Two", "Three"});
    CHECK(moves == std::vector<int>{3, 2, 0});
}

TEST_CASE("record from a played game") {
    Game game;
    for (const char* uci : {"f2f3", "e7e5", "g2g4", "d8h4"}) REQUIRE(game.makeUciMove(uci));
    const GameRecord record = recordFromGame(game);
    CHECK(record.tag("Result") == "0-1");
    CHECK(record.tree.moveCount() == 4);
    CHECK(writePgn(record).find("1. f3 e5 2. g4 Qh4# 0-1") != std::string::npos);

    Game resigned;
    REQUIRE(resigned.makeUciMove("e2e4"));
    resigned.resign(Color::Black);
    CHECK(recordFromGame(resigned).tag("Result") == "1-0");
}

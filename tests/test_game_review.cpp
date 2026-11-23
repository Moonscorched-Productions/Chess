#include <doctest/doctest.h>

#include "chess/Pgn.h"
#include "chess/Review.h"

using namespace core;

namespace {

Eval cp(int value) {
    Eval eval;
    eval.cp = value;
    eval.depth = 16;
    return eval;
}

// Plays the given UCI moves from the start position.
struct Line {
    GameTree tree;
    std::vector<NodeId> nodes;

    explicit Line(std::initializer_list<const char*> moves) {
        Game game;
        game.setEnforceDraws(false);
        NodeId node = kRootNode;
        for (const char* uci : moves) {
            const auto move = parseUciMove(uci);
            REQUIRE(move.has_value());
            node = playMove(tree, node, game, *move);
            REQUIRE(node != kNoNode);
            nodes.push_back(node);
        }
    }
};

}  // namespace

TEST_CASE("move accuracy falls with the expected score lost") {
    CHECK(moveAccuracy(0.0) == doctest::Approx(100.0));
    CHECK(moveAccuracy(-3.0) == doctest::Approx(100.0));
    CHECK(moveAccuracy(5.0) < moveAccuracy(1.0));
    CHECK(moveAccuracy(10.0) == doctest::Approx(63.6).epsilon(0.02));
    CHECK(moveAccuracy(100.0) == doctest::Approx(0.0));
}

TEST_CASE("game phase follows the move number and the material left") {
    CHECK(gamePhase(kStartFen, 0) == GamePhase::Opening);
    CHECK(gamePhase(kStartFen, 21) == GamePhase::Middlegame);
    CHECK(gamePhase("8/5pk1/6p1/8/3R4/6P1/5PK1/3r4 w - - 0 40", 10) == GamePhase::Endgame);
    CHECK(std::string(phaseName(GamePhase::Endgame)) == "Endgame");
}

TEST_CASE("a game review counts move qualities and finds the key moments") {
    Line line{"e2e4", "e7e5"};
    const NodeId e4 = line.nodes[0];
    const NodeId e5 = line.nodes[1];
    // 1. e4 keeps the balance, 1... e5 is treated as a blunder by Black.
    line.tree.setEval(kRootNode, cp(20));
    line.tree.setBestMove(kRootNode, "e2e4");
    line.tree.setEval(e4, cp(20));
    line.tree.setEval(e5, cp(500));

    const GameReview review = reviewGame(line.tree);
    CHECK(review.totalMoves == 2);
    CHECK(review.judgedMoves == 2);
    CHECK(review.complete());
    CHECK(review.white.moves == 1);
    CHECK(review.white.count(MoveQuality::Best) == 1);
    CHECK(review.white.accuracy == doctest::Approx(100.0));
    CHECK(review.black.count(MoveQuality::Blunder) == 1);
    CHECK(review.black.accuracy < 50.0);
    CHECK(review.side(Color::Black).phases[static_cast<std::size_t>(GamePhase::Opening)].moves == 1);
    REQUIRE(review.moments.size() == 1);
    CHECK(review.moments[0].node == e5);
    CHECK(review.moments[0].kind == MomentKind::Blunder);

    // Without an evaluation after the last move the review is incomplete.
    line.tree.setEval(e5, std::nullopt);
    const GameReview partial = reviewGame(line.tree);
    CHECK(partial.judgedMoves == 1);
    CHECK_FALSE(partial.complete());
    CHECK(partial.black.moves == 0);
}

TEST_CASE("missed wins and missed mates are named as such") {
    Line line{"e2e4", "e7e5"};
    Eval mate;
    mate.mate = 2;
    line.tree.setEval(kRootNode, mate);
    line.tree.setEval(line.nodes[0], cp(0));
    line.tree.setEval(line.nodes[1], cp(0));
    GameReview review = reviewGame(line.tree);
    REQUIRE(review.moments.size() == 1);
    CHECK(review.moments[0].kind == MomentKind::MissedMate);
    CHECK(std::string(momentText(review.moments[0].kind)) == "Missed a forced mate");

    line.tree.setEval(kRootNode, cp(600));
    review = reviewGame(line.tree);
    REQUIRE(review.moments.size() == 1);
    CHECK(review.moments[0].kind == MomentKind::MissedWin);
}

TEST_CASE("a game that ends in mate needs no engine for its last position") {
    Line line{"f2f3", "e7e5", "g2g4", "d8h4"};
    evaluateFinalPosition(line.tree);
    const TreeNode& last = line.tree.node(line.nodes.back());
    REQUIRE(last.eval.has_value());
    CHECK(last.eval->mate == 0);
    CHECK_FALSE(last.eval->whiteIsAhead());

    // An unfinished game keeps its last position open.
    Line open{"e2e4"};
    evaluateFinalPosition(open.tree);
    CHECK_FALSE(open.tree.node(open.nodes.back()).eval.has_value());
}

TEST_CASE("pgn: the best moves of the engine survive a round trip") {
    const auto record = parsePgn("1. e4 { [%eval 0.30,16] [%best e7e5] A comment } e5 { [%best g1f3] } *");
    REQUIRE(record.has_value());
    const auto line = record->tree.mainLine();
    REQUIRE(line.size() == 2);
    CHECK(record->tree.node(line[0]).bestMove == "e7e5");
    CHECK(record->tree.node(line[0]).comment == "A comment");
    REQUIRE(record->tree.node(line[0]).eval.has_value());
    CHECK(record->tree.node(line[1]).bestMove == "g1f3");
    CHECK(record->tree.node(line[1]).comment.empty());

    GameRecord copy = *record;
    copy.tree.setBestMove(kRootNode, "e2e4");
    const auto again = parsePgn(writePgn(copy));
    REQUIRE(again.has_value());
    CHECK(again->tree.node(kRootNode).bestMove == "e2e4");
    CHECK(again->tree.node(again->tree.mainLine()[0]).bestMove == "e7e5");
    CHECK(again->tree.node(again->tree.mainLine()[1]).bestMove == "g1f3");
}

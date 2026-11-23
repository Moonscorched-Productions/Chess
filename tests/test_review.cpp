#include <doctest/doctest.h>

#include "chess/Review.h"

using namespace core;

namespace {

Square sq(const char* name) { return makeSquare(name[0] - 'a', name[1] - '1'); }

Eval cp(int value) {
    Eval eval;
    eval.cp = value;
    eval.depth = 16;
    return eval;
}

struct Fixture {
    GameTree tree;
    Game game;
    NodeId e4 = kNoNode;
    NodeId e5 = kNoNode;

    Fixture() {
        game.setEnforceDraws(false);
        e4 = playMove(tree, kRootNode, game, Move{sq("e2"), sq("e4")});
        e5 = playMove(tree, e4, game, Move{sq("e7"), sq("e5")});
        REQUIRE(e5 != kNoNode);
    }
};

}  // namespace

TEST_CASE("move quality needs evaluations before and after the move") {
    Fixture f;
    CHECK(classifyMove(f.tree, kRootNode) == MoveQuality::None);
    CHECK(classifyMove(f.tree, f.e4) == MoveQuality::None);
    CHECK_FALSE(expectedScoreLoss(f.tree, f.e4).has_value());
    f.tree.setEval(kRootNode, cp(20));
    CHECK(classifyMove(f.tree, f.e4) == MoveQuality::None);
    f.tree.setEval(f.e4, cp(20));
    CHECK(classifyMove(f.tree, f.e4) == MoveQuality::Excellent);
    CHECK(expectedScoreLoss(f.tree, f.e4).value() == doctest::Approx(0.0));
}

TEST_CASE("move quality follows the expected score lost by the mover") {
    Fixture f;
    f.tree.setEval(kRootNode, cp(0));

    const auto whiteQuality = [&](int after) {
        f.tree.setEval(f.e4, cp(after));
        return classifyMove(f.tree, f.e4);
    };
    CHECK(whiteQuality(40) == MoveQuality::Excellent);   // an improvement is never a loss
    CHECK(whiteQuality(-5) == MoveQuality::Excellent);
    CHECK(whiteQuality(-40) == MoveQuality::Good);
    CHECK(whiteQuality(-90) == MoveQuality::Inaccuracy);
    CHECK(whiteQuality(-180) == MoveQuality::Mistake);
    CHECK(whiteQuality(-400) == MoveQuality::Blunder);

    // For Black the signs are reversed: a rising evaluation is a loss.
    f.tree.setEval(f.e4, cp(0));
    const auto blackQuality = [&](int after) {
        f.tree.setEval(f.e5, cp(after));
        return classifyMove(f.tree, f.e5);
    };
    CHECK(blackQuality(-300) == MoveQuality::Excellent);
    CHECK(blackQuality(90) == MoveQuality::Inaccuracy);
    CHECK(blackQuality(400) == MoveQuality::Blunder);
}

TEST_CASE("the engine's choice is the best move whatever the numbers say") {
    Fixture f;
    f.tree.setEval(kRootNode, cp(30));
    f.tree.setEval(f.e4, cp(-60));
    CHECK(classifyMove(f.tree, f.e4) == MoveQuality::Inaccuracy);
    f.tree.setBestMove(kRootNode, "d2d4");
    CHECK(classifyMove(f.tree, f.e4) == MoveQuality::Inaccuracy);
    f.tree.setBestMove(kRootNode, "e2e4");
    CHECK(classifyMove(f.tree, f.e4) == MoveQuality::Best);
}

TEST_CASE("mate scores count as won or lost positions") {
    Fixture f;
    Eval winning;
    winning.mate = 3;
    Eval losing;
    losing.mate = -2;
    f.tree.setEval(kRootNode, winning);
    f.tree.setEval(f.e4, cp(0));
    CHECK(classifyMove(f.tree, f.e4) == MoveQuality::Blunder);  // threw away a forced mate
    f.tree.setEval(f.e4, winning);
    CHECK(classifyMove(f.tree, f.e4) == MoveQuality::Excellent);
    f.tree.setEval(f.e5, losing);
    CHECK(classifyMove(f.tree, f.e5) == MoveQuality::Excellent);  // Black turned it around

    // A custom threshold set changes the categories.
    f.tree.setEval(kRootNode, cp(0));
    f.tree.setEval(f.e4, cp(-40));
    QualityThresholds strict;
    strict.excellent = 0.5;
    strict.good = 1.0;
    strict.inaccuracy = 2.0;
    strict.mistake = 3.0;
    CHECK(classifyMove(f.tree, f.e4, strict) == MoveQuality::Blunder);
    CHECK(std::string(qualityName(MoveQuality::Blunder)) == "Blunder");
    CHECK(std::string(qualityName(MoveQuality::None)).empty());
}

#include <doctest/doctest.h>

#include "chess/Game.h"
#include "chess/Weakness.h"

using namespace core;

TEST_CASE("weakness: tactics of one move") {
    // A knight check that also hits the rook.
    const auto fork = analyseMove("4k3/8/8/8/3n4/8/8/R3K3 b - - 0 1", "d4c2");
    REQUIRE(fork.has_value());
    CHECK(fork->check);
    CHECK(fork->fork);
    CHECK_FALSE(fork->pin);

    // Bb5 pins the c6 knight to the king once d7 is empty.
    const auto pin = analyseMove("r1bqkbnr/ppp2ppp/2np4/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 4", "f1b5");
    REQUIRE(pin.has_value());
    CHECK(pin->pin);
    CHECK_FALSE(pin->fork);

    // Ra8+ skewers the king and the queen behind it.
    const auto skewer = analyseMove("4k2q/8/8/8/8/8/R7/6K1 w - - 0 1", "a2a8");
    REQUIRE(skewer.has_value());
    CHECK(skewer->check);
    CHECK(skewer->skewer);

    // The knight steps aside and the rook gives check.
    const auto discovery = analyseMove("4k3/8/8/8/8/8/4N3/4R2K w - - 0 1", "e2c3");
    REQUIRE(discovery.has_value());
    CHECK(discovery->discovery);
    CHECK(discovery->check);

    // Taking an undefended queen.
    const auto capture = analyseMove("4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1", "d1d5");
    REQUIRE(capture.has_value());
    CHECK(capture->winsHanging);

    // Castling is not a discovered attack.
    const auto castle = analyseMove("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1");
    REQUIRE(castle.has_value());
    CHECK_FALSE(castle->discovery);

    CHECK_FALSE(analyseMove("4k3/8/8/8/8/8/8/4K3 w - - 0 1", "e1e3").has_value());
}

namespace {

// White blunders with Ke1, after which Nc2+ forks king and rook.
GameRecord forkBlunder() {
    GameRecord record{{}, GameTree("4k3/8/8/8/3n4/8/5K2/R7 w - - 0 1")};
    GameTree& tree = record.tree;
    Game game;
    game.setEnforceDraws(false);
    game.reset(tree.startFen());
    tree.setEval(kRootNode, Eval{350, std::nullopt, 20});
    tree.setBestMove(kRootNode, "f2e3");
    const NodeId blunder = playMove(tree, kRootNode, game, *parseUciMove("f2e1"));
    REQUIRE(blunder != kNoNode);
    tree.setEval(blunder, Eval{-300, std::nullopt, 20});
    tree.setBestMove(blunder, "d4c2");
    record.setTag("White", "You");
    record.setTag("Black", "Engine (level 4)");
    record.setTag("Result", "0-1");
    return record;
}

}  // namespace

TEST_CASE("weakness: why a mistake was one") {
    const GameRecord record = forkBlunder();
    const NodeId blunder = record.tree.mainLine().front();
    CHECK(classifyMove(record.tree, blunder) == MoveQuality::Blunder);
    const std::vector<Motif> motifs = mistakeMotifs(record.tree, blunder);
    CHECK(std::find(motifs.begin(), motifs.end(), Motif::AllowedFork) != motifs.end());
    CHECK(std::find(motifs.begin(), motifs.end(), Motif::Positional) == motifs.end());

    TrainingReport report;
    addToReport(report, record, Color::White, 7);
    CHECK(report.games == 1);
    CHECK(report.moves == 1);
    CHECK(report.blunders == 1);
    CHECK(report.centipawnLoss == doctest::Approx(650.0));
    REQUIRE(report.examples.size() == 1);
    const MistakeExample& example = report.examples.front();
    CHECK(example.gameId == 7);
    CHECK(example.label == "1. Ke1??");
    CHECK(example.best == "f2e3");
    CHECK(example.bestSan == "Ke3");
    CHECK(example.opponent == "Engine (level 4)");
    const auto ranked = report.rankedMotifs();
    REQUIRE_FALSE(ranked.empty());
    CHECK(report.motifCounts[static_cast<std::size_t>(Motif::AllowedFork)] == 1);
    CHECK(report.blundersByGame == std::vector<double>{1.0});

    // Seen from Black, the same game has no judged moves.
    TrainingReport black;
    addToReport(black, record, Color::Black, 7);
    CHECK(black.moves == 0);
    CHECK(black.examples.empty());
}

TEST_CASE("weakness: engine strength from the opponent's name") {
    CHECK(engineStrengthLabel("Stockfish 17 (level 3)") == "level 3");
    CHECK(engineStrengthLabel("Stockfish 17 (1500 Elo)") == "1500 Elo");
    CHECK(engineStrengthLabel("Magnus Carlsen") == "");
    CHECK(engineStrengthLabel("Club (Paris)") == "");
}

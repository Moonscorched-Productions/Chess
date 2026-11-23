#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "chess/Explain.h"
#include "chess/Position.h"
#include "chess/PuzzleBuilder.h"

using namespace core;

namespace {

Square sq(const char* name) { return makeSquare(name[0] - 'a', name[1] - '1'); }

bool has(const std::vector<Square>& squares, const char* name) {
    return std::find(squares.begin(), squares.end(), sq(name)) != squares.end();
}

bool mentions(const std::vector<std::string>& sentences, const char* text) {
    return std::any_of(sentences.begin(), sentences.end(),
                       [&](const std::string& sentence) { return sentence.find(text) != std::string::npos; });
}

Eval cp(int value) {
    Eval eval;
    eval.cp = value;
    eval.depth = 16;
    return eval;
}

Eval mateIn(int moves) {
    Eval eval;
    eval.mate = moves;
    return eval;
}

// A tree built from UCI moves, starting from `fen`.
struct Line {
    GameTree tree;
    std::vector<NodeId> nodes;

    Line(std::string_view fen, std::initializer_list<const char*> moves) : tree(fen) {
        Game game;
        game.setEnforceDraws(false);
        REQUIRE(game.reset(fen));
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

TEST_CASE("position: pieces, attackers and attacks") {
    const Position start(kStartFen);
    CHECK(start.pieceCount() == 32);
    CHECK(start.sideToMove() == Color::White);
    CHECK(start.at(sq("e1")) == Piece{PieceType::King, Color::White});
    CHECK(start.at(sq("d8")) == Piece{PieceType::Queen, Color::Black});
    CHECK(start.at(sq("e4")).empty());
    CHECK(start.kingSquare(Color::Black) == sq("e8"));
    CHECK(start.attackerCount(sq("f3"), Color::White) == 3);  // knight and two pawns
    CHECK(start.attackerCount(sq("f3"), Color::Black) == 0);
    CHECK(has(start.attacksFrom(sq("g1")), "f3"));
    CHECK(start.attacksFrom(sq("a1")).size() == 2);  // blocked by its own pieces
    CHECK(start.mobility(sq("g1")) == 2);
    CHECK(start.mobility(sq("e2")) == 2);
    CHECK(start.mobility(sq("a1")) == 0);
    CHECK(start.hangingPieces(Color::White).empty());

    const Position black("8/8/8/8/8/8/8/K6k b - - 0 1");
    CHECK(black.sideToMove() == Color::Black);
    CHECK(Position("not a fen").pieceCount() == 0);
}

TEST_CASE("position: pieces that can be won") {
    // The knight on e5 is attacked by a pawn; the rook on a8 by a bishop and undefended.
    const Position position("r3k3/8/8/4n3/3P4/5B2/4K3/8 w - - 0 1");
    const std::vector<Square> loose = position.hangingPieces(Color::Black);
    CHECK(has(loose, "e5"));
    CHECK(has(loose, "a8"));
    CHECK(position.hangingPieces(Color::White).empty());
    // A defended pawn attacked by a pawn is not hanging.
    const Position solid("4k3/8/2p5/3p4/4P3/8/8/4K3 w - - 0 1");
    CHECK(solid.hangingPieces(Color::Black).empty());
}

TEST_CASE("position: pawn structure and notes") {
    const Position position("4k3/p5p1/8/8/8/2P5/P1P3P1/4K3 w - - 0 1");
    const Position::Pawns white = position.pawns(Color::White);
    CHECK(has(white.doubled, "c2"));
    CHECK_FALSE(has(white.doubled, "c3"));
    CHECK(has(white.isolated, "a2"));
    CHECK(has(white.isolated, "c3"));
    CHECK(has(white.isolated, "g2"));
    CHECK(has(white.passed, "c3"));
    CHECK_FALSE(has(white.passed, "a2"));
    const Position::Pawns black = position.pawns(Color::Black);
    CHECK(black.isolated.size() == 2);  // a7 and g7
    CHECK(black.passed.empty());

    const std::vector<std::string> notes = positionNotes("4k3/p5p1/8/8/8/2P5/P1P3P1/4K3 w - - 0 1", Color::White);
    CHECK(mentions(notes, "Passed pawn"));
    CHECK(mentions(notes, "Isolated pawns"));
    CHECK(mentions(notes, "Doubled pawn on c2"));
    CHECK(pieceValue(PieceType::Queen) == 9);
    CHECK(std::string(pieceName(PieceType::Knight)) == "knight");
}

TEST_CASE("explanations: a blunder that hangs the queen") {
    // After 1. e4 e5 2. Qh5 Nc6 3. Qxf7+ the queen is simply taken by the king.
    Line line(kStartFen, {"e2e4", "e7e5", "d1h5", "b8c6", "h5f7"});
    const NodeId blunder = line.nodes[4];
    const NodeId before = line.nodes[3];
    line.tree.setEval(before, cp(30));
    line.tree.setBestMove(before, "g1f3");
    line.tree.setEval(blunder, cp(-800));
    line.tree.setBestMove(blunder, "e8f7");

    const std::vector<std::string> text = explainMove(line.tree, blunder, MoveQuality::Blunder);
    CHECK(mentions(text, "The queen can be taken at once: Kxf7."));
    CHECK(mentions(text, "Better was Nf3."));
    CHECK(explainMove(line.tree, kRootNode, MoveQuality::None).empty());
}

TEST_CASE("explanations: good moves say what they do") {
    // Scholar's mate: 4. Qxf7#.
    Line mate(kStartFen, {"e2e4", "e7e5", "d1h5", "b8c6", "f1c4", "g8f6", "h5f7"});
    const std::vector<std::string> mateText = explainMove(mate.tree, mate.nodes.back(), MoveQuality::Best);
    CHECK(mentions(mateText, "first choice"));
    CHECK(mentions(mateText, "Checkmate."));

    // A knight fork of king and rook.
    Line fork("r3k3/8/8/3N4/8/8/8/4K3 w - - 0 1", {"d5c7"});
    const std::vector<std::string> forkText = explainMove(fork.tree, fork.nodes[0], MoveQuality::Best);
    CHECK(mentions(forkText, "Forks the king and the rook on a8."));

    // Missing a mate is pointed out together with the better move.
    Line missed("6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1", {"e1e2"});
    missed.tree.setEval(kRootNode, mateIn(1));
    missed.tree.setBestMove(kRootNode, "a1a8");
    missed.tree.setEval(missed.nodes[0], cp(500));
    const std::vector<std::string> missedText = explainMove(missed.tree, missed.nodes[0], MoveQuality::Mistake);
    CHECK(mentions(missedText, "There was a forced mate in 1."));
    CHECK(mentions(missedText, "Better was Ra8#, which is checkmate."));
}

TEST_CASE("puzzle generation: candidates follow the mistakes of a reviewed game") {
    Line line(kStartFen, {"e2e4", "e7e5", "d1h5", "b8c6", "h5f7"});
    line.tree.setEval(kRootNode, cp(20));
    for (std::size_t i = 0; i < 4; ++i) line.tree.setEval(line.nodes[i], cp(20));
    line.tree.setEval(line.nodes[4], cp(-800));

    const GameReview review = reviewGame(line.tree);
    REQUIRE(review.moments.size() == 1);
    const std::vector<NodeId> candidates = puzzleCandidates(line.tree, review);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0] == line.nodes[4]);

    // A mistake that only gives up an advantage leaves nothing to find.
    line.tree.setEval(line.nodes[2], cp(900));
    line.tree.setEval(line.nodes[3], cp(900));
    line.tree.setEval(line.nodes[4], cp(100));
    CHECK(puzzleCandidates(line.tree, reviewGame(line.tree)).empty());
}

TEST_CASE("puzzle generation: only clear best moves make puzzle moves") {
    CHECK(isOnlyGoodMove(cp(600), cp(50), Color::White));
    CHECK(isOnlyGoodMove(cp(600), std::nullopt, Color::White));
    CHECK_FALSE(isOnlyGoodMove(cp(600), cp(550), Color::White));  // two ways to win
    CHECK_FALSE(isOnlyGoodMove(cp(60), cp(-300), Color::White));  // not winning enough
    CHECK(isOnlyGoodMove(cp(-600), cp(0), Color::Black));
    CHECK_FALSE(isOnlyGoodMove(cp(-600), cp(0), Color::White));
    CHECK(isOnlyGoodMove(mateIn(2), cp(200), Color::White));
    CHECK_FALSE(isOnlyGoodMove(mateIn(2), mateIn(3), Color::White));
    CHECK_FALSE(isOnlyGoodMove(mateIn(2), cp(1200), Color::White));
}

TEST_CASE("puzzle generation: a finished puzzle has themes, a rating and a stable id") {
    // The opponent hangs the queen; the solver takes it.
    const std::string fen = "r1bqkbnr/pppp1ppp/2n5/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR w KQkq - 2 3";
    const auto puzzle = buildPuzzle(fen, {"h5f7", "e8f7"}, 99.0);
    REQUIRE(puzzle.has_value());
    CHECK(puzzle->fen == fen);
    CHECK(puzzle->moves.size() == 2);
    CHECK(puzzle->themes.find("fromMyGames") != std::string::npos);
    CHECK(puzzle->themes.find("hangingPiece") != std::string::npos);
    CHECK(puzzle->themes.find("oneMove") != std::string::npos);
    CHECK(puzzle->themes.find("crushing") != std::string::npos);
    CHECK(puzzle->themes.find("opening") != std::string::npos);
    CHECK(puzzle->rating < 1000);
    CHECK(puzzle->id == buildPuzzle(fen, {"h5f7", "e8f7"}, 99.0)->id);
    CHECK(puzzle->id.rfind("own-", 0) == 0);

    // The generated puzzle works in a solving session.
    PuzzleSession session;
    REQUIRE(session.start(*puzzle));
    CHECK(session.solverColor() == Color::Black);
    CHECK(session.play(Move{sq("e8"), sq("f7")}) == PuzzleSession::Outcome::Solved);

    // A check on the back rank that is not mate, then a real back-rank mate in one.
    const auto mate = buildPuzzle("6k1/5ppp/8/8/8/8/5PPP/R5K1 b - - 0 30", {"h7h6", "a1a8"}, 100.0);
    REQUIRE(mate.has_value());  // h6 gives the king air: Ra8 is only a check
    CHECK(mate->themes.find("mate") == std::string::npos);
    const auto backRank = buildPuzzle("6k1/5ppp/8/8/8/7P/5PP1/R5K1 b - - 0 30", {"g8h8", "a1a8"}, 100.0);
    REQUIRE(backRank.has_value());
    CHECK(backRank->themes.find("mateIn1") != std::string::npos);
    CHECK(backRank->themes.find("backRankMate") != std::string::npos);
    CHECK(backRank->themes.find("endgame") != std::string::npos);

    // Illegal or incomplete lines are rejected.
    CHECK_FALSE(buildPuzzle(fen, {"h5f7"}, 99.0).has_value());
    CHECK_FALSE(buildPuzzle(fen, {"h5f7", "e8e7", "f7f8"}, 99.0).has_value());
    CHECK_FALSE(buildPuzzle(fen, {"h5h8", "e8f7"}, 99.0).has_value());
    CHECK_FALSE(buildPuzzle("nonsense", {"e2e4", "e7e5"}, 99.0).has_value());
}

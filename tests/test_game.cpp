#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <string>

#include "chess/Game.h"

using namespace core;

namespace {

Square sq(const char* name) { return makeSquare(name[0] - 'a', name[1] - '1'); }

std::uint64_t perft(Game& game, int depth) {
    const std::vector<Move> moves = game.legalMoves();
    if (depth <= 1) return moves.size();
    std::uint64_t nodes = 0;
    for (const Move& move : moves) {
        REQUIRE(game.makeMove(move));
        // Draw rules would cut the tree short; perft counts pure move generation.
        if (game.isOver() && game.status() != Status::Checkmate && game.status() != Status::Stalemate) {
            FAIL("unexpected draw inside perft");
        }
        nodes += perft(game, depth - 1);
        REQUIRE(game.undo());
    }
    return nodes;
}

void play(Game& game, std::initializer_list<const char*> moves) {
    for (const char* move : moves) {
        INFO("move ", move);
        REQUIRE(game.makeUciMove(move));
    }
}

bool hasMove(const Game& game, const char* from, const char* to) {
    for (const Move& move : game.legalMovesFrom(sq(from)))
        if (move.to == sq(to)) return true;
    return false;
}

}  // namespace

TEST_CASE("perft matches published node counts") {
    Game game;
    SUBCASE("start position") {
        CHECK(perft(game, 1) == 20);
        CHECK(perft(game, 2) == 400);
        CHECK(perft(game, 3) == 8902);
        CHECK(perft(game, 4) == 197281);
    }
    SUBCASE("kiwipete") {
        REQUIRE(game.reset("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"));
        CHECK(perft(game, 1) == 48);
        CHECK(perft(game, 2) == 2039);
        CHECK(perft(game, 3) == 97862);
    }
    SUBCASE("position 3 (en passant and pins)") {
        REQUIRE(game.reset("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"));
        CHECK(perft(game, 1) == 14);
        CHECK(perft(game, 2) == 191);
        CHECK(perft(game, 3) == 2812);
        CHECK(perft(game, 4) == 43238);
    }
    SUBCASE("position 4 (promotions)") {
        REQUIRE(game.reset("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"));
        CHECK(perft(game, 1) == 6);
        CHECK(perft(game, 2) == 264);
        CHECK(perft(game, 3) == 9467);
    }
    SUBCASE("position 5") {
        REQUIRE(game.reset("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"));
        CHECK(perft(game, 1) == 44);
        CHECK(perft(game, 2) == 1486);
        CHECK(perft(game, 3) == 62379);
    }
}

TEST_CASE("new game starts from the standard position") {
    Game game;
    CHECK(game.fen() == std::string(kStartFen));
    CHECK(game.sideToMove() == Color::White);
    CHECK(game.status() == Status::Ongoing);
    CHECK(game.pieceAt(sq("e1")) == Piece{PieceType::King, Color::White});
    CHECK(game.pieceAt(sq("d8")) == Piece{PieceType::Queen, Color::Black});
    CHECK(game.pieceAt(sq("e4")).empty());
    CHECK_FALSE(game.canUndo());
    CHECK_FALSE(game.canRedo());
}

TEST_CASE("illegal moves are rejected") {
    Game game;
    CHECK_FALSE(game.makeUciMove("e2e5"));
    CHECK_FALSE(game.makeUciMove("e7e5"));  // not black's turn
    CHECK_FALSE(game.makeUciMove("zz"));
    CHECK_FALSE(game.makeUciMove("e2e4x"));
    CHECK_FALSE(game.makeMove(Move{-1, 70}));
    CHECK(game.ply() == 0);
}

TEST_CASE("SAN, UCI and history") {
    Game game;
    play(game, {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5c6", "d7c6"});
    const auto& history = game.history();
    REQUIRE(history.size() == 8);
    CHECK(history[0].san == "e4");
    CHECK(history[2].san == "Nf3");
    CHECK(history[6].san == "Bxc6");
    CHECK(history[7].san == "dxc6");
    CHECK(history[6].captured == Piece{PieceType::Knight, Color::Black});
    CHECK(game.uciMoves() == "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5c6 d7c6");
    CHECK(game.capturedBy(Color::White) == std::vector<PieceType>{PieceType::Knight});
    CHECK(game.capturedBy(Color::Black) == std::vector<PieceType>{PieceType::Bishop});
    REQUIRE(game.lastMove().has_value());
    CHECK(game.lastMove()->from == sq("d7"));
    CHECK(game.lastMove()->to == sq("c6"));
}

TEST_CASE("castling") {
    Game game;
    REQUIRE(game.reset("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 1"));
    CHECK(hasMove(game, "e1", "g1"));
    CHECK(hasMove(game, "e1", "c1"));

    SUBCASE("king side") {
        play(game, {"e1g1"});
        CHECK(game.history().back().san == "O-O");
        CHECK(game.history().back().isCastling);
        CHECK(game.history().back().captured.empty());
        CHECK(game.pieceAt(sq("g1")).type == PieceType::King);
        CHECK(game.pieceAt(sq("f1")).type == PieceType::Rook);
        CHECK(game.pieceAt(sq("h1")).empty());
        play(game, {"e8c8"});
        CHECK(game.history().back().san == "O-O-O");
        CHECK(game.pieceAt(sq("c8")).type == PieceType::King);
        CHECK(game.pieceAt(sq("d8")).type == PieceType::Rook);
        REQUIRE(game.undo());
        REQUIRE(game.undo());
        CHECK(game.pieceAt(sq("e1")).type == PieceType::King);
        CHECK(game.pieceAt(sq("h1")).type == PieceType::Rook);
    }
    SUBCASE("rights are lost after the king moves") {
        play(game, {"e1f1", "a7a6", "f1e1", "a6a5"});
        CHECK_FALSE(hasMove(game, "e1", "g1"));
        CHECK_FALSE(hasMove(game, "e1", "c1"));
    }
    SUBCASE("cannot castle through check") {
        REQUIRE(game.reset("r3k2r/8/8/8/8/8/5r2/R3K2R w KQ - 0 1"));
        CHECK_FALSE(hasMove(game, "e1", "g1"));
        CHECK(hasMove(game, "e1", "c1"));
    }
}

TEST_CASE("en passant") {
    Game game;
    play(game, {"e2e4", "a7a6", "e4e5", "d7d5"});
    CHECK(hasMove(game, "e5", "d6"));
    play(game, {"e5d6"});
    CHECK(game.history().back().san == "exd6");
    CHECK(game.history().back().captured == Piece{PieceType::Pawn, Color::Black});
    CHECK(game.pieceAt(sq("d5")).empty());
    CHECK(game.pieceAt(sq("d6")) == Piece{PieceType::Pawn, Color::White});
    REQUIRE(game.undo());
    CHECK(game.pieceAt(sq("d5")) == Piece{PieceType::Pawn, Color::Black});

    // The right expires after one move.
    play(game, {"h2h3", "h7h6"});
    CHECK_FALSE(hasMove(game, "e5", "d6"));
}

TEST_CASE("promotion") {
    Game game;
    REQUIRE(game.reset("1n5k/P7/8/8/8/8/8/K7 w - - 0 1"));
    CHECK(game.isPromotion(sq("a7"), sq("a8")));
    CHECK(game.isPromotion(sq("a7"), sq("b8")));
    CHECK_FALSE(game.isPromotion(sq("a1"), sq("a2")));
    CHECK_FALSE(game.makeUciMove("a7a8"));  // promotion piece is required
    CHECK(game.legalMovesFrom(sq("a7")).size() == 8);

    SUBCASE("to queen") {
        play(game, {"a7a8q"});
        CHECK(game.pieceAt(sq("a8")) == Piece{PieceType::Queen, Color::White});
        CHECK(game.history().back().san == "a8=Q");
    }
    SUBCASE("underpromotion with capture") {
        play(game, {"a7b8n"});
        CHECK(game.pieceAt(sq("b8")) == Piece{PieceType::Knight, Color::White});
        CHECK(game.history().back().san == "axb8=N");
        CHECK(game.history().back().captured.type == PieceType::Knight);
        REQUIRE(game.undo());
        CHECK(game.pieceAt(sq("a7")) == Piece{PieceType::Pawn, Color::White});
        CHECK(game.pieceAt(sq("b8")) == Piece{PieceType::Knight, Color::Black});
    }
}

TEST_CASE("check and checkmate") {
    Game game;
    play(game, {"f2f3", "e7e5", "g2g4"});
    CHECK_FALSE(game.inCheck());
    play(game, {"d8h4"});
    CHECK(game.inCheck());
    CHECK(game.status() == Status::Checkmate);
    CHECK(game.result() == Result::BlackWins);
    CHECK(game.history().back().san == "Qh4#");
    CHECK(game.history().back().isCheck);
    CHECK(game.legalMoves().empty());
    CHECK_FALSE(game.makeUciMove("a2a3"));
    CHECK(game.kingSquare(Color::White) == sq("e1"));

    REQUIRE(game.undo());
    CHECK(game.status() == Status::Ongoing);
    CHECK(game.result() == Result::None);
}

TEST_CASE("stalemate") {
    Game game;
    REQUIRE(game.reset("7k/5Q2/8/8/8/8/8/K7 w - - 0 1"));
    play(game, {"f7g6"});
    CHECK(game.status() == Status::Stalemate);
    CHECK(game.result() == Result::Draw);
    CHECK_FALSE(game.inCheck());
}

TEST_CASE("draw rules") {
    Game game;
    SUBCASE("threefold repetition") {
        play(game, {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1"});
        CHECK(game.status() == Status::Ongoing);
        play(game, {"f6g8"});
        CHECK(game.status() == Status::DrawRepetition);
        CHECK(game.result() == Result::Draw);
        REQUIRE(game.undo());
        CHECK(game.status() == Status::Ongoing);
    }
    SUBCASE("fifty-move rule") {
        REQUIRE(game.reset("7k/8/8/8/8/8/R7/K7 w - - 99 80"));
        CHECK(game.status() == Status::Ongoing);
        play(game, {"a2b2"});
        CHECK(game.status() == Status::DrawFiftyMove);
    }
    SUBCASE("insufficient material") {
        REQUIRE(game.reset("7k/8/8/8/8/8/8/KB6 w - - 0 1"));
        CHECK(game.status() == Status::DrawInsufficientMaterial);
        REQUIRE(game.reset("7k/8/8/8/8/8/1r6/K7 w - - 0 1"));
        CHECK(game.status() == Status::Ongoing);
        play(game, {"a1b2"});
        CHECK(game.status() == Status::DrawInsufficientMaterial);
    }
}

TEST_CASE("undo and redo") {
    Game game;
    play(game, {"e2e4", "e7e5", "g1f3"});
    const std::string after = game.fen();

    REQUIRE(game.undo());
    REQUIRE(game.undo());
    CHECK(game.ply() == 1);
    CHECK(game.canRedo());
    REQUIRE(game.redo());
    REQUIRE(game.redo());
    CHECK(game.fen() == after);
    CHECK_FALSE(game.canRedo());
    CHECK(game.history().back().san == "Nf3");

    // A new move discards the redo line.
    REQUIRE(game.undo());
    play(game, {"d2d4"});
    CHECK_FALSE(game.canRedo());
    CHECK(game.uciMoves() == "e2e4 e7e5 d2d4");

    while (game.canUndo()) REQUIRE(game.undo());
    CHECK(game.fen() == std::string(kStartFen));
    CHECK_FALSE(game.undo());
}

TEST_CASE("resignation and agreed draws") {
    Game game;
    play(game, {"e2e4"});
    SUBCASE("resign") {
        game.resign(Color::Black);
        CHECK(game.status() == Status::Resigned);
        CHECK(game.result() == Result::WhiteWins);
        CHECK(game.legalMoves().empty());
        CHECK_FALSE(game.makeUciMove("e7e5"));
        REQUIRE(game.undo());
        CHECK(game.status() == Status::Ongoing);
    }
    SUBCASE("draw") {
        game.agreeDraw();
        CHECK(game.status() == Status::DrawAgreed);
        CHECK(game.result() == Result::Draw);
        game.resign(Color::White);  // ignored once the game is over
        CHECK(game.status() == Status::DrawAgreed);
    }
}

TEST_CASE("FEN handling") {
    Game game;
    SUBCASE("round trip") {
        const std::string fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
        REQUIRE(game.reset(fen));
        CHECK(game.fen() == fen);
        CHECK(game.startFen() == fen);
        REQUIRE(game.reset("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 b - - 12 34"));
        CHECK(game.fen() == "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 b - - 12 34");
        CHECK(game.sideToMove() == Color::Black);
        CHECK(game.fullMoveNumber() == 34);
    }
    SUBCASE("missing trailing fields are filled in") {
        REQUIRE(game.reset("7k/8/8/8/8/8/R7/K7 b"));
        CHECK(game.fen() == "7k/8/8/8/8/8/R7/K7 b - - 0 1");
    }
    SUBCASE("impossible castling rights are dropped") {
        REQUIRE(game.reset("4k3/8/8/8/8/8/8/4K2R w KQkq - 0 1"));
        CHECK(game.fen() == "4k3/8/8/8/8/8/8/4K2R w K - 0 1");
    }
    SUBCASE("invalid input is rejected and leaves the game untouched") {
        play(game, {"e2e4"});
        const std::string before = game.fen();
        std::string error;
        for (const char* bad : {
                 "",
                 "hello world",
                 "8/8/8/8/8/8/8/8 w - - 0 1",                                  // no kings
                 "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1",   // bad side
                 "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN w KQkq - 0 1",    // short rank
                 "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",   // bad digit
                 "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1",            // 7 ranks
                 "Pnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",   // pawn on rank 8
                 "kk6/8/8/8/8/8/8/K7 w - - 0 1",                               // two black kings
                 "7k/8/8/8/8/8/8/K6r b - - 0 1",                               // white in check, black to move
                 "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e9 0 1",  // bad en passant
                 "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - x 1",   // bad counter
             }) {
            INFO("fen: ", bad);
            error.clear();
            CHECK_FALSE(game.reset(bad, &error));
            CHECK_FALSE(error.empty());
            CHECK_FALSE(validateFen(bad));
        }
        CHECK(game.fen() == before);
        CHECK(game.ply() == 1);
    }
}

TEST_CASE("running out of time") {
    SUBCASE("the other side wins") {
        Game game;
        play(game, {"e2e4", "e7e5"});
        game.flag(Color::White);
        CHECK(game.status() == Status::Timeout);
        CHECK(game.result() == Result::BlackWins);
        CHECK(game.legalMoves().empty());
        REQUIRE(game.undo());
        CHECK(game.status() == Status::Ongoing);
    }
    SUBCASE("a draw when the other side cannot mate") {
        Game game;
        REQUIRE(game.reset("8/8/8/4k3/8/8/4K1N1/7Q w - - 0 1"));
        game.flag(Color::White);  // Black has a lone king
        CHECK(game.status() == Status::Timeout);
        CHECK(game.result() == Result::Draw);
    }
    SUBCASE("a win with a single pawn left") {
        Game game;
        REQUIRE(game.reset("8/8/8/4k3/8/3p4/4K3/7Q w - - 0 1"));
        game.flag(Color::White);
        CHECK(game.result() == Result::BlackWins);
    }
}

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chess/Types.h"

namespace core {

inline constexpr std::string_view kStartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Returns true when the FEN describes a position the rules engine can safely play from.
// On failure, `error` (if given) receives a short human-readable reason.
bool validateFen(std::string_view fen, std::string* error = nullptr);

// Reads a move in UCI notation ("e2e4", "e7e8q"). Checks the syntax only, not legality.
std::optional<Move> parseUciMove(std::string_view uci);

// A single game of chess: the current position, the moves that led to it,
// and an undo/redo cursor into that move list.
class Game {
public:
    Game();
    ~Game();
    Game(Game&&) noexcept;
    Game& operator=(Game&&) noexcept;

    // Starts a new game from the given position. Returns false and leaves the
    // game untouched when the FEN is invalid.
    bool reset(std::string_view fen = kStartFen, std::string* error = nullptr);

    // --- Position queries -------------------------------------------------
    Piece pieceAt(Square square) const;
    Color sideToMove() const;
    bool inCheck() const;
    Square kingSquare(Color color) const;
    std::string fen() const;
    const std::string& startFen() const;
    // Side to move and move number of the start position (for move lists and PGN).
    Color startSideToMove() const;
    int startFullMoveNumber() const;
    int fullMoveNumber() const;

    // --- Moves ------------------------------------------------------------
    std::vector<Move> legalMoves() const;
    std::vector<Move> legalMovesFrom(Square from) const;
    // True when moving from -> to is legal and requires choosing a promotion piece.
    bool isPromotion(Square from, Square to) const;

    // Plays a move. Returns false if the move is illegal or the game is over.
    // Playing a move discards any moves available for redo.
    bool makeMove(Move move);
    // Same, for a move in UCI notation such as "e2e4" or "e7e8q".
    bool makeUciMove(std::string_view uci);
    // Same, for a move in standard algebraic notation such as "Nf3" or "exd8=Q+".
    bool makeSanMove(std::string_view san);

    // With draw enforcement off (used for analysis), repetition, the fifty-move rule and
    // insufficient material do not end the game; only checkmate and stalemate do.
    void setEnforceDraws(bool enforce);

    bool canUndo() const;
    bool canRedo() const;
    bool undo();
    bool redo();

    // --- History ----------------------------------------------------------
    // Moves played up to the current position (excludes moves available for redo).
    const std::vector<MoveRecord>& history() const;
    // Number of moves played to reach the current position.
    int ply() const;
    std::optional<Move> lastMove() const;
    // Space separated UCI moves from the start position, for the engine.
    std::string uciMoves() const;
    // Piece types captured by `capturer` so far, in the order they were taken.
    std::vector<PieceType> capturedBy(Color capturer) const;

    // --- Outcome ----------------------------------------------------------
    Status status() const;
    Result result() const;
    bool isOver() const { return status() != Status::Ongoing; }
    // Ends the game by resignation of the given side. Ignored if the game is over.
    void resign(Color loser);
    // Ends the game as a draw by agreement. Ignored if the game is over.
    void agreeDraw();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace core

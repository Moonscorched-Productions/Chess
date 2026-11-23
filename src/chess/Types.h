#pragma once

#include <cstdint>
#include <string>

// Plain value types shared between the rules module and its users.
// Nothing here depends on rendering, UI or the engine.
namespace core {

enum class Color : std::uint8_t { White, Black };

constexpr Color opposite(Color color) { return color == Color::White ? Color::Black : Color::White; }

enum class PieceType : std::uint8_t { Pawn, Knight, Bishop, Rook, Queen, King, None };

struct Piece {
    PieceType type = PieceType::None;
    Color color = Color::White;

    constexpr bool empty() const { return type == PieceType::None; }
    constexpr bool operator==(const Piece&) const = default;
};

// Squares are indexed 0..63 with a1 = 0, b1 = 1, ..., h8 = 63.
using Square = int;
constexpr Square kNoSquare = -1;

constexpr bool isValidSquare(Square square) { return square >= 0 && square < 64; }
constexpr int fileOf(Square square) { return square & 7; }
constexpr int rankOf(Square square) { return square >> 3; }
constexpr Square makeSquare(int file, int rank) { return rank * 8 + file; }

inline std::string squareName(Square square) {
    if (!isValidSquare(square)) return "-";
    return {static_cast<char>('a' + fileOf(square)), static_cast<char>('1' + rankOf(square))};
}

// A move as the user sees it: castling is the king moving two squares.
struct Move {
    Square from = kNoSquare;
    Square to = kNoSquare;
    PieceType promotion = PieceType::None;

    constexpr bool operator==(const Move&) const = default;
};

struct MoveRecord {
    Move move;
    std::string san;  // e.g. "Nf3", "exd5", "O-O", "Qh7#"
    std::string uci;  // e.g. "g1f3", "e7e8q"
    Piece moved;
    Piece captured;  // empty when the move is not a capture
    bool isCheck = false;
    bool isCastling = false;
};

enum class Status {
    Ongoing,
    Checkmate,
    Stalemate,
    DrawRepetition,
    DrawFiftyMove,
    DrawInsufficientMaterial,
    DrawAgreed,
    Resigned,
    Timeout,  // a clock ran out (a draw if the other side cannot mate)
};

enum class Result { None, WhiteWins, BlackWins, Draw };

}  // namespace core

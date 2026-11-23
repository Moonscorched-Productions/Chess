#include "chess/Position.h"

#include <algorithm>
#include <cstdint>

#include "chess/chess.hpp"

namespace core {

namespace {

std::uint64_t bit(Square square) { return 1ULL << square; }

std::vector<Square> toSquares(std::uint64_t bits) {
    std::vector<Square> squares;
    for (Square square = 0; square < 64; ++square) {
        if ((bits & bit(square)) != 0) squares.push_back(square);
    }
    return squares;
}

std::uint64_t occupancy(const std::array<Piece, 64>& board) {
    std::uint64_t occupied = 0;
    for (Square square = 0; square < 64; ++square) {
        if (!board[static_cast<std::size_t>(square)].empty()) occupied |= bit(square);
    }
    return occupied;
}

// Attack set of a piece standing on `square` with the given occupancy.
std::uint64_t attackBits(Piece piece, Square square, std::uint64_t occupied) {
    const chess::Square sq(square);
    const chess::Bitboard occ(occupied);
    switch (piece.type) {
        case PieceType::Pawn:
            return chess::attacks::pawn(piece.color == Color::White ? chess::Color::WHITE : chess::Color::BLACK, sq).getBits();
        case PieceType::Knight: return chess::attacks::knight(sq).getBits();
        case PieceType::Bishop: return chess::attacks::bishop(sq, occ).getBits();
        case PieceType::Rook: return chess::attacks::rook(sq, occ).getBits();
        case PieceType::Queen: return chess::attacks::queen(sq, occ).getBits();
        case PieceType::King: return chess::attacks::king(sq).getBits();
        case PieceType::None: break;
    }
    return 0;
}

std::string joinSquares(const std::vector<Square>& squares) {
    std::string text;
    for (std::size_t i = 0; i < squares.size(); ++i) {
        if (i > 0) text += i + 1 == squares.size() ? " and " : ", ";
        text += squareName(squares[i]);
    }
    return text;
}

}  // namespace

int pieceValue(PieceType type) {
    switch (type) {
        case PieceType::Pawn: return 1;
        case PieceType::Knight: return 3;
        case PieceType::Bishop: return 3;
        case PieceType::Rook: return 5;
        case PieceType::Queen: return 9;
        default: return 0;
    }
}

const char* pieceName(PieceType type) {
    switch (type) {
        case PieceType::Pawn: return "pawn";
        case PieceType::Knight: return "knight";
        case PieceType::Bishop: return "bishop";
        case PieceType::Rook: return "rook";
        case PieceType::Queen: return "queen";
        case PieceType::King: return "king";
        case PieceType::None: break;
    }
    return "piece";
}

Position::Position(std::string_view fen) {
    // Piece placement: ranks from 8 down to 1, files a to h.
    int file = 0;
    int rank = 7;
    std::size_t i = 0;
    std::array<Piece, 64> board{};
    bool valid = true;
    for (; i < fen.size() && fen[i] != ' '; ++i) {
        const char c = fen[i];
        if (c == '/') {
            file = 0;
            --rank;
            continue;
        }
        if (c >= '1' && c <= '8') {
            file += c - '0';
            continue;
        }
        PieceType type = PieceType::None;
        switch (c | 0x20) {
            case 'p': type = PieceType::Pawn; break;
            case 'n': type = PieceType::Knight; break;
            case 'b': type = PieceType::Bishop; break;
            case 'r': type = PieceType::Rook; break;
            case 'q': type = PieceType::Queen; break;
            case 'k': type = PieceType::King; break;
            default: break;
        }
        if (type == PieceType::None || file > 7 || rank < 0) {
            valid = false;
            break;
        }
        board[static_cast<std::size_t>(makeSquare(file, rank))] = Piece{type, (c & 0x20) != 0 ? Color::Black : Color::White};
        ++file;
    }
    if (!valid) return;
    board_ = board;
    if (i + 1 < fen.size() && fen[i + 1] == 'b') sideToMove_ = Color::Black;
}

Square Position::kingSquare(Color color) const {
    for (Square square = 0; square < 64; ++square) {
        if (at(square) == Piece{PieceType::King, color}) return square;
    }
    return kNoSquare;
}

int Position::pieceCount() const {
    return static_cast<int>(std::count_if(board_.begin(), board_.end(), [](const Piece& piece) { return !piece.empty(); }));
}

std::vector<Square> Position::attacksFrom(Square square) const {
    const Piece piece = at(square);
    if (piece.empty()) return {};
    return toSquares(attackBits(piece, square, occupancy(board_)));
}

std::vector<Square> Position::attackers(Square square, Color color) const {
    std::vector<Square> result;
    if (!isValidSquare(square)) return result;
    const std::uint64_t occupied = occupancy(board_);
    for (Square from = 0; from < 64; ++from) {
        const Piece piece = at(from);
        if (piece.empty() || piece.color != color) continue;
        if ((attackBits(piece, from, occupied) & bit(square)) != 0) result.push_back(from);
    }
    return result;
}

std::vector<Square> Position::hangingPieces(Color owner) const {
    std::vector<Square> result;
    for (Square square = 0; square < 64; ++square) {
        const Piece piece = at(square);
        if (piece.empty() || piece.color != owner || piece.type == PieceType::King) continue;
        const std::vector<Square> attackedBy = attackers(square, opposite(owner));
        if (attackedBy.empty()) continue;
        int cheapest = 100;
        for (const Square from : attackedBy) {
            // A king only counts as an attacker of an undefended piece.
            const PieceType type = at(from).type;
            cheapest = std::min(cheapest, type == PieceType::King ? 99 : pieceValue(type));
        }
        const int defenders = attackerCount(square, owner);
        const bool undefended = defenders == 0;
        const bool cheaperAttacker = cheapest < pieceValue(piece.type);
        const bool outnumbered = static_cast<int>(attackedBy.size()) > defenders && cheapest <= pieceValue(piece.type);
        if (undefended || cheaperAttacker || (outnumbered && cheapest != 99)) result.push_back(square);
    }
    return result;
}

int Position::mobility(Square square) const {
    const Piece piece = at(square);
    if (piece.empty()) return 0;
    const std::uint64_t occupied = occupancy(board_);
    std::uint64_t own = 0;
    std::uint64_t enemy = 0;
    for (Square s = 0; s < 64; ++s) {
        const Piece other = at(s);
        if (other.empty()) continue;
        (other.color == piece.color ? own : enemy) |= bit(s);
    }
    if (piece.type != PieceType::Pawn) {
        return chess::Bitboard(attackBits(piece, square, occupied) & ~own).count();
    }
    // Pawns: captures onto enemy pieces plus pushes onto empty squares.
    int moves = chess::Bitboard(attackBits(piece, square, occupied) & enemy).count();
    const int step = piece.color == Color::White ? 8 : -8;
    const Square one = square + step;
    if (isValidSquare(one) && (occupied & bit(one)) == 0) {
        ++moves;
        const bool home = rankOf(square) == (piece.color == Color::White ? 1 : 6);
        const Square two = one + step;
        if (home && isValidSquare(two) && (occupied & bit(two)) == 0) ++moves;
    }
    return moves;
}

Position::Pawns Position::pawns(Color color) const {
    Pawns result;
    const Piece own{PieceType::Pawn, color};
    const Piece enemy{PieceType::Pawn, opposite(color)};
    const int forward = color == Color::White ? 1 : -1;
    const auto hasPawn = [&](const Piece& pawn, int file, int fromRank, int toRank) {
        if (file < 0 || file > 7) return false;
        for (int rank = std::max(fromRank, 0); rank <= std::min(toRank, 7); ++rank) {
            if (at(makeSquare(file, rank)) == pawn) return true;
        }
        return false;
    };

    for (Square square = 0; square < 64; ++square) {
        if (!(at(square) == own)) continue;
        const int file = fileOf(square);
        const int rank = rankOf(square);
        if (!hasPawn(own, file - 1, 0, 7) && !hasPawn(own, file + 1, 0, 7)) result.isolated.push_back(square);

        // Squares in front of the pawn, towards promotion.
        const int aheadFrom = forward > 0 ? rank + 1 : 0;
        const int aheadTo = forward > 0 ? 7 : rank - 1;
        if (hasPawn(own, file, aheadFrom, aheadTo)) result.doubled.push_back(square);
        if (!hasPawn(enemy, file, aheadFrom, aheadTo) && !hasPawn(enemy, file - 1, aheadFrom, aheadTo) &&
            !hasPawn(enemy, file + 1, aheadFrom, aheadTo)) {
            result.passed.push_back(square);
        }
    }
    return result;
}

std::vector<std::string> positionNotes(std::string_view fen, Color color) {
    std::vector<std::string> notes;
    const Position position(fen);
    if (position.kingSquare(color) == kNoSquare) return notes;

    for (const Square square : position.hangingPieces(color)) {
        notes.push_back(std::string("The ") + pieceName(position.at(square).type) + " on " + squareName(square) + " can be won.");
    }

    const Position::Pawns pawns = position.pawns(color);
    if (!pawns.passed.empty()) {
        notes.push_back(std::string(pawns.passed.size() == 1 ? "Passed pawn on " : "Passed pawns on ") + joinSquares(pawns.passed) + ".");
    }
    if (!pawns.isolated.empty()) {
        notes.push_back(std::string(pawns.isolated.size() == 1 ? "Isolated pawn on " : "Isolated pawns on ") +
                        joinSquares(pawns.isolated) + ".");
    }
    if (!pawns.doubled.empty()) {
        notes.push_back(std::string(pawns.doubled.size() == 1 ? "Doubled pawn on " : "Doubled pawns on ") +
                        joinSquares(pawns.doubled) + ".");
    }

    // King safety only matters while the enemy still has a queen.
    bool enemyQueen = false;
    for (Square square = 0; square < 64; ++square) {
        if (position.at(square) == Piece{PieceType::Queen, opposite(color)}) enemyQueen = true;
    }
    if (enemyQueen) {
        const Square king = position.kingSquare(color);
        const int forward = color == Color::White ? 1 : -1;
        int shield = 0;
        for (int df = -1; df <= 1; ++df) {
            const int file = fileOf(king) + df;
            if (file < 0 || file > 7) continue;
            for (int step = 1; step <= 2; ++step) {
                const int rank = rankOf(king) + forward * step;
                if (rank < 0 || rank > 7) continue;
                if (position.at(makeSquare(file, rank)) == Piece{PieceType::Pawn, color}) {
                    ++shield;
                    break;
                }
            }
        }
        const bool castledRank = rankOf(king) == (color == Color::White ? 0 : 7);
        const bool central = fileOf(king) >= 3 && fileOf(king) <= 4;
        if (!castledRank) {
            notes.push_back("The king has left the back rank and is exposed.");
        } else if (shield <= 1 && !central) {
            notes.push_back("The king has little pawn cover.");
        }
    }
    return notes;
}

}  // namespace core

#include "chess/Game.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <utility>

#include "chess/chess.hpp"

namespace core {

namespace {

// --- Conversions between the public types and chess-library -----------------

Color toColor(chess::Color color) { return color == chess::Color::BLACK ? Color::Black : Color::White; }

chess::Color fromColor(Color color) { return color == Color::Black ? chess::Color::BLACK : chess::Color::WHITE; }

PieceType toPieceType(chess::PieceType type) {
    const int value = static_cast<int>(type.internal());
    return value >= 0 && value <= 5 ? static_cast<PieceType>(value) : PieceType::None;
}

Piece toPiece(chess::Piece piece) {
    if (piece == chess::Piece::NONE) return {};
    return {toPieceType(piece.type()), toColor(piece.color())};
}

// chess-library encodes castling as "king takes own rook"; users see the king
// landing on the g- or c-file.
Square userTarget(chess::Move move) {
    if (move.typeOf() == chess::Move::CASTLING) {
        const bool kingSide = move.to() > move.from();
        return makeSquare(kingSide ? 6 : 2, rankOf(move.from().index()));
    }
    return move.to().index();
}

Move toUserMove(chess::Move move) {
    Move result;
    result.from = move.from().index();
    result.to = userTarget(move);
    if (move.typeOf() == chess::Move::PROMOTION) result.promotion = toPieceType(move.promotionType());
    return result;
}

std::vector<std::string_view> splitFields(std::string_view text) {
    std::vector<std::string_view> fields;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
        std::size_t end = pos;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
        if (end > pos) fields.push_back(text.substr(pos, end - pos));
        pos = end;
    }
    return fields;
}

bool isNumber(std::string_view text) {
    return !text.empty() && text.size() <= 4 &&
           std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool fail(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
    return false;
}

// Checks the FEN and rewrites it into a complete six-field string.
bool normalizeFen(std::string_view fen, std::string& normalized, std::string* error) {
    const std::vector<std::string_view> fields = splitFields(fen);
    if (fields.empty()) return fail(error, "FEN is empty");
    if (fields.size() > 6) return fail(error, "FEN has too many fields");

    // Piece placement: rank 8 first.
    std::array<char, 64> squares{};
    squares.fill(' ');
    int rank = 7;
    int file = 0;
    for (const char c : fields[0]) {
        if (c == '/') {
            if (file != 8) return fail(error, "A rank does not describe 8 squares");
            if (--rank < 0) return fail(error, "Too many ranks");
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return fail(error, "A rank describes more than 8 squares");
        } else if (std::string_view("pnbrqkPNBRQK").find(c) != std::string_view::npos) {
            if (file >= 8) return fail(error, "A rank describes more than 8 squares");
            squares[static_cast<std::size_t>(makeSquare(file, rank))] = c;
            ++file;
        } else {
            return fail(error, "Unknown character in piece placement");
        }
    }
    if (rank != 0 || file != 8) return fail(error, "Piece placement must describe 8 ranks of 8 squares");

    const auto count = [&](char piece) { return std::count(squares.begin(), squares.end(), piece); };
    if (count('K') != 1 || count('k') != 1) return fail(error, "Each side needs exactly one king");
    for (int f = 0; f < 8; ++f) {
        for (const int backRank : {0, 7}) {
            const char c = squares[static_cast<std::size_t>(makeSquare(f, backRank))];
            if (c == 'P' || c == 'p') return fail(error, "Pawns cannot stand on the first or last rank");
        }
    }

    const std::string_view side = fields.size() > 1 ? fields[1] : std::string_view("w");
    if (side != "w" && side != "b") return fail(error, "Side to move must be 'w' or 'b'");

    // Castling rights: keep only the ones the position can actually support.
    const std::string_view castlingIn = fields.size() > 2 ? fields[2] : std::string_view("-");
    std::string castling;
    if (castlingIn != "-") {
        const auto at = [&](int f, int r) { return squares[static_cast<std::size_t>(makeSquare(f, r))]; };
        for (const char c : castlingIn) {
            if (std::string_view("KQkq").find(c) == std::string_view::npos)
                return fail(error, "Castling rights must use the letters KQkq");
            if (castling.find(c) != std::string::npos) continue;
            const bool white = c == 'K' || c == 'Q';
            const int r = white ? 0 : 7;
            const int rookFile = (c == 'K' || c == 'k') ? 7 : 0;
            if (at(4, r) == (white ? 'K' : 'k') && at(rookFile, r) == (white ? 'R' : 'r')) castling += c;
        }
        // FEN order.
        std::string ordered;
        for (const char c : std::string_view("KQkq"))
            if (castling.find(c) != std::string::npos) ordered += c;
        castling = ordered;
    }
    if (castling.empty()) castling = "-";

    const std::string_view enPassant = fields.size() > 3 ? fields[3] : std::string_view("-");
    if (enPassant != "-") {
        const bool shape = enPassant.size() == 2 && enPassant[0] >= 'a' && enPassant[0] <= 'h' &&
                           (enPassant[1] == '3' || enPassant[1] == '6');
        if (!shape) return fail(error, "En passant square is not valid");
        if ((enPassant[1] == '6') != (side == "w")) return fail(error, "En passant square does not match the side to move");
    }

    const std::string_view halfMoves = fields.size() > 4 ? fields[4] : std::string_view("0");
    const std::string_view fullMoves = fields.size() > 5 ? fields[5] : std::string_view("1");
    if (!isNumber(halfMoves) || !isNumber(fullMoves)) return fail(error, "Move counters must be numbers");

    normalized.clear();
    normalized.append(fields[0]).append(" ").append(side).append(" ").append(castling).append(" ");
    normalized.append(enPassant).append(" ").append(halfMoves).append(" ");
    normalized.append(fullMoves == "0" ? std::string_view("1") : fullMoves);
    return true;
}

// Full check: syntax plus "the side that just moved is not left in check".
bool loadBoard(std::string_view fen, chess::Board& board, std::string* error) {
    std::string normalized;
    if (!normalizeFen(fen, normalized, error)) return false;

    chess::Board candidate;
    if (!candidate.setFen(normalized)) return fail(error, "FEN could not be parsed");

    const chess::Color mover = candidate.sideToMove();
    if (candidate.isAttacked(candidate.kingSq(~mover), mover))
        return fail(error, "The side not to move is in check");

    board = std::move(candidate);
    return true;
}

}  // namespace

bool validateFen(std::string_view fen, std::string* error) {
    chess::Board board;
    return loadBoard(fen, board, error);
}

// --- Game::Impl ----------------------------------------------------------------

struct Game::Impl {
    enum class Manual { None, WhiteResigned, BlackResigned, DrawAgreed, WhiteFlagged, BlackFlagged };

    chess::Board board;
    std::string startFen{kStartFen};
    Color startSide = Color::White;
    int startMoveNumber = 1;
    std::vector<chess::Move> moves;    // parallel to `records`
    std::vector<MoveRecord> records;   // moves played up to the current position
    std::vector<Move> redoStack;       // most recently undone move last
    Manual manual = Manual::None;
    bool enforceDraws = true;
    Status status = Status::Ongoing;
    Result result = Result::None;

    std::optional<chess::Move> findLegal(Move move) const {
        if (!isValidSquare(move.from) || !isValidSquare(move.to)) return std::nullopt;
        chess::Movelist list;
        chess::movegen::legalmoves(list, board);
        for (const chess::Move candidate : list) {
            if (toUserMove(candidate) == move) return candidate;
        }
        return std::nullopt;
    }

    void play(chess::Move move) {
        MoveRecord record;
        record.move = toUserMove(move);
        record.san = chess::uci::moveToSan(board, move);
        record.uci = chess::uci::moveToUci(move);
        record.moved = toPiece(board.at(move.from()));
        record.isCastling = move.typeOf() == chess::Move::CASTLING;
        if (move.typeOf() == chess::Move::ENPASSANT) {
            record.captured = {PieceType::Pawn, opposite(record.moved.color)};
        } else if (!record.isCastling) {
            record.captured = toPiece(board.at(move.to()));
        }
        board.makeMove(move);
        record.isCheck = board.inCheck();
        moves.push_back(move);
        records.push_back(std::move(record));
        refresh();
    }

    void refresh() {
        if (manual == Manual::DrawAgreed) {
            status = Status::DrawAgreed;
            result = Result::Draw;
            return;
        }
        if (manual == Manual::WhiteResigned || manual == Manual::BlackResigned) {
            status = Status::Resigned;
            result = manual == Manual::WhiteResigned ? Result::BlackWins : Result::WhiteWins;
            return;
        }
        if (manual == Manual::WhiteFlagged || manual == Manual::BlackFlagged) {
            status = Status::Timeout;
            const chess::Color winner = manual == Manual::WhiteFlagged ? chess::Color::BLACK : chess::Color::WHITE;
            // A lone king, or king and one minor piece, cannot mate: the game is drawn.
            const int heavy = board.pieces(chess::PieceType::PAWN, winner).count() +
                              board.pieces(chess::PieceType::ROOK, winner).count() +
                              board.pieces(chess::PieceType::QUEEN, winner).count();
            const int minors = board.pieces(chess::PieceType::KNIGHT, winner).count() +
                               board.pieces(chess::PieceType::BISHOP, winner).count();
            if (heavy == 0 && minors <= 1) {
                result = Result::Draw;
            } else {
                result = winner == chess::Color::WHITE ? Result::WhiteWins : Result::BlackWins;
            }
            return;
        }

        auto [reason, outcome] = board.isGameOver();
        if (!enforceDraws && reason != chess::GameResultReason::CHECKMATE && reason != chess::GameResultReason::NONE) {
            // Only positions without legal moves end the game: look past the draw rules.
            chess::Movelist list;
            chess::movegen::legalmoves(list, board);
            if (!list.empty()) {
                reason = chess::GameResultReason::NONE;
            } else {
                reason = board.inCheck() ? chess::GameResultReason::CHECKMATE : chess::GameResultReason::STALEMATE;
            }
        }
        switch (reason) {
            case chess::GameResultReason::CHECKMATE:
                status = Status::Checkmate;
                // The side to move has been mated.
                result = board.sideToMove() == chess::Color::WHITE ? Result::BlackWins : Result::WhiteWins;
                break;
            case chess::GameResultReason::STALEMATE:
                status = Status::Stalemate;
                result = Result::Draw;
                break;
            case chess::GameResultReason::INSUFFICIENT_MATERIAL:
                status = Status::DrawInsufficientMaterial;
                result = Result::Draw;
                break;
            case chess::GameResultReason::FIFTY_MOVE_RULE:
                status = Status::DrawFiftyMove;
                result = Result::Draw;
                break;
            case chess::GameResultReason::THREEFOLD_REPETITION:
                status = Status::DrawRepetition;
                result = Result::Draw;
                break;
            default:
                status = Status::Ongoing;
                result = Result::None;
                break;
        }
        (void)outcome;
    }
};

// --- Game ----------------------------------------------------------------------

Game::Game() : impl_(std::make_unique<Impl>()) { impl_->refresh(); }
Game::~Game() = default;
Game::Game(Game&&) noexcept = default;
Game& Game::operator=(Game&&) noexcept = default;

bool Game::reset(std::string_view fen, std::string* error) {
    chess::Board board;
    if (!loadBoard(fen, board, error)) return false;

    impl_->board = std::move(board);
    impl_->startFen = impl_->board.getFen();
    impl_->startSide = toColor(impl_->board.sideToMove());
    impl_->startMoveNumber = static_cast<int>(impl_->board.fullMoveNumber());
    impl_->moves.clear();
    impl_->records.clear();
    impl_->redoStack.clear();
    impl_->manual = Impl::Manual::None;
    impl_->refresh();
    return true;
}

Piece Game::pieceAt(Square square) const {
    if (!isValidSquare(square)) return {};
    return toPiece(impl_->board.at(chess::Square(square)));
}

Color Game::sideToMove() const { return toColor(impl_->board.sideToMove()); }

bool Game::inCheck() const { return impl_->board.inCheck(); }

Square Game::kingSquare(Color color) const { return impl_->board.kingSq(fromColor(color)).index(); }

std::string Game::fen() const { return impl_->board.getFen(); }

const std::string& Game::startFen() const { return impl_->startFen; }

Color Game::startSideToMove() const { return impl_->startSide; }

int Game::startFullMoveNumber() const { return impl_->startMoveNumber; }

int Game::fullMoveNumber() const { return static_cast<int>(impl_->board.fullMoveNumber()); }

std::vector<Move> Game::legalMoves() const {
    std::vector<Move> result;
    if (isOver()) return result;
    chess::Movelist list;
    chess::movegen::legalmoves(list, impl_->board);
    result.reserve(static_cast<std::size_t>(list.size()));
    for (const chess::Move move : list) result.push_back(toUserMove(move));
    return result;
}

std::vector<Move> Game::legalMovesFrom(Square from) const {
    std::vector<Move> result;
    for (const Move& move : legalMoves())
        if (move.from == from) result.push_back(move);
    return result;
}

bool Game::isPromotion(Square from, Square to) const {
    for (const Move& move : legalMoves())
        if (move.from == from && move.to == to && move.promotion != PieceType::None) return true;
    return false;
}

bool Game::makeMove(Move move) {
    if (isOver()) return false;
    const std::optional<chess::Move> legal = impl_->findLegal(move);
    if (!legal) return false;
    impl_->redoStack.clear();
    impl_->play(*legal);
    return true;
}

std::optional<Move> parseUciMove(std::string_view uci) {
    if (uci.size() < 4 || uci.size() > 5) return std::nullopt;
    const auto parseSquare = [](std::string_view text) -> Square {
        if (text[0] < 'a' || text[0] > 'h' || text[1] < '1' || text[1] > '8') return kNoSquare;
        return makeSquare(text[0] - 'a', text[1] - '1');
    };
    Move move;
    move.from = parseSquare(uci.substr(0, 2));
    move.to = parseSquare(uci.substr(2, 2));
    if (move.from == kNoSquare || move.to == kNoSquare) return std::nullopt;
    if (uci.size() == 5) {
        switch (uci[4]) {
            case 'q': move.promotion = PieceType::Queen; break;
            case 'r': move.promotion = PieceType::Rook; break;
            case 'b': move.promotion = PieceType::Bishop; break;
            case 'n': move.promotion = PieceType::Knight; break;
            default: return std::nullopt;
        }
    }
    return move;
}

bool Game::makeUciMove(std::string_view uci) {
    std::optional<Move> move = parseUciMove(uci);
    if (!move) return false;
    if (makeMove(*move)) return true;

    // Some sources write castling as "king takes own rook" (e1h1); accept that too.
    const Piece king = pieceAt(move->from);
    const Piece target = pieceAt(move->to);
    if (king.type == PieceType::King && target.type == PieceType::Rook && target.color == king.color &&
        rankOf(move->from) == rankOf(move->to)) {
        move->to = makeSquare(fileOf(move->to) > fileOf(move->from) ? 6 : 2, rankOf(move->from));
        return makeMove(*move);
    }
    return false;
}

bool Game::makeSanMove(std::string_view san) {
    if (isOver() || san.empty()) return false;
    chess::Move move = chess::Move::NO_MOVE;
    try {
        move = chess::uci::parseSan(impl_->board, san);
    } catch (const std::exception&) {
        return false;
    }
    if (move == chess::Move::NO_MOVE) return false;
    // Go through the usual legality check rather than trusting the parser.
    return makeMove(toUserMove(move));
}

void Game::setEnforceDraws(bool enforce) {
    impl_->enforceDraws = enforce;
    impl_->refresh();
}

bool Game::canUndo() const { return !impl_->records.empty(); }

bool Game::canRedo() const { return !impl_->redoStack.empty(); }

bool Game::undo() {
    if (!canUndo()) return false;
    impl_->manual = Impl::Manual::None;
    impl_->board.unmakeMove(impl_->moves.back());
    impl_->redoStack.push_back(impl_->records.back().move);
    impl_->moves.pop_back();
    impl_->records.pop_back();
    impl_->refresh();
    return true;
}

bool Game::redo() {
    if (!canRedo()) return false;
    const Move move = impl_->redoStack.back();
    const std::optional<chess::Move> legal = impl_->findLegal(move);
    if (!legal) {
        impl_->redoStack.clear();
        return false;
    }
    impl_->redoStack.pop_back();
    impl_->play(*legal);
    return true;
}

const std::vector<MoveRecord>& Game::history() const { return impl_->records; }

int Game::ply() const { return static_cast<int>(impl_->records.size()); }

std::optional<Move> Game::lastMove() const {
    if (impl_->records.empty()) return std::nullopt;
    return impl_->records.back().move;
}

std::string Game::uciMoves() const {
    std::string result;
    for (const MoveRecord& record : impl_->records) {
        if (!result.empty()) result += ' ';
        result += record.uci;
    }
    return result;
}

std::vector<PieceType> Game::capturedBy(Color capturer) const {
    std::vector<PieceType> result;
    for (const MoveRecord& record : impl_->records) {
        if (!record.captured.empty() && record.moved.color == capturer) result.push_back(record.captured.type);
    }
    return result;
}

Status Game::status() const { return impl_->status; }

Result Game::result() const { return impl_->result; }

void Game::resign(Color loser) {
    if (isOver()) return;
    impl_->manual = loser == Color::White ? Impl::Manual::WhiteResigned : Impl::Manual::BlackResigned;
    impl_->refresh();
}

void Game::flag(Color loser) {
    if (isOver()) return;
    impl_->manual = loser == Color::White ? Impl::Manual::WhiteFlagged : Impl::Manual::BlackFlagged;
    impl_->refresh();
}

void Game::agreeDraw() {
    if (isOver()) return;
    impl_->manual = Impl::Manual::DrawAgreed;
    impl_->refresh();
}

}  // namespace core

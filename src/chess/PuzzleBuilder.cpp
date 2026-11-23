#include "chess/PuzzleBuilder.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "chess/Position.h"

namespace core {

namespace {

double shareFor(const Eval& eval, Color color) {
    const double white = eval.whiteWinPercent();
    return color == Color::White ? white : 100.0 - white;
}

bool matesFor(const Eval& eval, Color color) {
    return eval.mate && *eval.mate != 0 && (*eval.mate > 0) == (color == Color::White);
}

// FNV-1a, so that the same puzzle always gets the same id.
std::string stableId(const std::string& fen, const std::vector<std::string>& moves) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto feed = [&](const std::string& text) {
        for (const unsigned char c : text) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
    };
    feed(fen);
    for (const std::string& move : moves) feed(move);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "own-%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

void addTheme(std::string& themes, const char* theme) {
    if (!themes.empty()) themes += ' ';
    themes += theme;
}

}  // namespace

std::vector<NodeId> puzzleCandidates(const GameTree& tree, const GameReview& review) {
    std::vector<NodeId> nodes;
    for (const CriticalMoment& moment : review.moments) {
        if (!tree.contains(moment.node)) continue;
        const TreeNode& node = tree.node(moment.node);
        if (!node.eval || (node.eval->mate && *node.eval->mate == 0)) continue;
        const Color solver = opposite(tree.mover(moment.node));
        // The side to move must come out clearly better, otherwise there is nothing to find.
        if (shareFor(*node.eval, solver) < 65.0) continue;
        nodes.push_back(moment.node);
    }
    return nodes;
}

bool isOnlyGoodMove(const Eval& best, const std::optional<Eval>& second, Color solver) {
    const double bestShare = shareFor(best, solver);
    if (bestShare < 70.0) return false;
    if (!second) return true;
    const double secondShare = shareFor(*second, solver);
    if (matesFor(best, solver)) {
        // A second mating move, or one that wins trivially anyway, makes the answer ambiguous.
        return !matesFor(*second, solver) && secondShare <= 93.0;
    }
    return bestShare - secondShare >= 25.0;
}

std::optional<Puzzle> buildPuzzle(const std::string& fen, const std::vector<std::string>& moves, double solverShare) {
    // An opponent move followed by solver moves with replies in between: an even count.
    if (moves.size() < 2 || moves.size() % 2 != 0) return std::nullopt;
    Game game;
    game.setEnforceDraws(false);
    if (!game.reset(fen)) return std::nullopt;

    bool fork = false;
    bool sacrifice = false;
    bool promotion = false;
    bool hangingPiece = false;
    bool quietStart = false;
    int material = 0;  // material the solver is ahead by since the line started
    int lowestMaterial = 0;

    for (std::size_t i = 0; i < moves.size(); ++i) {
        const Position before(game.fen());
        if (!game.makeUciMove(moves[i])) return std::nullopt;
        const MoveRecord& record = game.history().back();
        const bool solverMove = i % 2 == 1;
        const int gained = record.captured.empty() ? 0 : pieceValue(record.captured.type);
        if (i > 0) {
            material += solverMove ? gained : -gained;
            lowestMaterial = std::min(lowestMaterial, material);
        }
        if (!solverMove) continue;

        if (record.move.promotion != PieceType::None) promotion = true;
        if (i == 1) {
            quietStart = record.captured.empty() && !record.isCheck;
            hangingPiece = !record.captured.empty() && before.attackerCount(record.move.to, opposite(record.moved.color)) == 0;
        }
        // A fork: the moved piece attacks two or more targets worth more than itself or undefended.
        const Position after(game.fen());
        int targets = record.isCheck ? 1 : 0;
        for (const Square square : after.attacksFrom(record.move.to)) {
            const Piece piece = after.at(square);
            if (piece.empty() || piece.color == record.moved.color || piece.type == PieceType::King) continue;
            if (pieceValue(piece.type) > pieceValue(record.moved.type) || after.attackerCount(square, piece.color) == 0) ++targets;
        }
        if (targets >= 2) fork = true;
    }
    // Giving up two points or more along the way and still coming out ahead is a sacrifice.
    sacrifice = lowestMaterial <= -2;

    const int solverMoves = static_cast<int>(moves.size() / 2);
    const bool mate = game.status() == Status::Checkmate;

    Puzzle puzzle;
    puzzle.fen = fen;
    puzzle.moves = moves;
    puzzle.id = stableId(fen, moves);

    std::string& themes = puzzle.themes;
    addTheme(themes, kOwnGameTheme);
    if (mate) {
        addTheme(themes, "mate");
        if (solverMoves <= 5) addTheme(themes, ("mateIn" + std::to_string(solverMoves)).c_str());
        // Mate along the back rank by a rook or queen.
        const MoveRecord& last = game.history().back();
        const Position mated(game.fen());
        const Square king = mated.kingSquare(opposite(last.moved.color));
        const int backRank = last.moved.color == Color::White ? 7 : 0;
        if (king != kNoSquare && rankOf(king) == backRank && rankOf(last.move.to) == backRank &&
            (last.moved.type == PieceType::Rook || last.moved.type == PieceType::Queen)) {
            addTheme(themes, "backRankMate");
        }
    } else {
        addTheme(themes, solverShare >= 90.0 ? "crushing" : "advantage");
    }
    if (fork) addTheme(themes, "fork");
    if (sacrifice) addTheme(themes, "sacrifice");
    if (promotion) addTheme(themes, "promotion");
    if (hangingPiece && solverMoves == 1) addTheme(themes, "hangingPiece");
    if (quietStart && !mate) addTheme(themes, "quietMove");
    addTheme(themes, solverMoves == 1 ? "oneMove" : solverMoves == 2 ? "short" : solverMoves == 3 ? "long" : "veryLong");

    // Game phase from the move number in the FEN.
    int fullMove = 1;
    if (const std::size_t space = fen.rfind(' '); space != std::string::npos) fullMove = std::max(1, std::atoi(fen.c_str() + space + 1));
    switch (gamePhase(fen, (fullMove - 1) * 2)) {
        case GamePhase::Opening: addTheme(themes, "opening"); break;
        case GamePhase::Middlegame: addTheme(themes, "middlegame"); break;
        case GamePhase::Endgame: addTheme(themes, "endgame"); break;
    }

    // Difficulty: longer lines, quiet first moves and sacrifices are harder to see;
    // taking a loose piece or mating in one is easy.
    int rating = 950 + 260 * (solverMoves - 1);
    if (quietStart) rating += 220;
    if (sacrifice) rating += 180;
    if (fork) rating += 60;
    if (hangingPiece && solverMoves == 1) rating -= 200;
    if (mate && solverMoves == 1) rating -= 150;
    puzzle.rating = std::clamp(rating, 600, 2600);
    return puzzle;
}

}  // namespace core

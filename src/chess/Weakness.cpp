#include "chess/Weakness.h"

#include <algorithm>
#include <cstdlib>

#include "chess/Game.h"
#include "chess/Position.h"

namespace core {

namespace {

bool isSlider(PieceType type) { return type == PieceType::Bishop || type == PieceType::Rook || type == PieceType::Queen; }

// A target worth attacking: the king, anything worth more than the attacker, or anything undefended.
bool worthwhileTarget(const Position& position, Square square, PieceType attacker) {
    const Piece piece = position.at(square);
    if (piece.type == PieceType::King) return true;
    if (pieceValue(piece.type) > pieceValue(attacker)) return true;
    return position.attackerCount(square, piece.color) == 0 && piece.type != PieceType::Pawn;
}

// Pin and skewer along the lines of the slider on `from`.
void lineTactics(const Position& position, Square from, MoveTactics& tactics) {
    const Piece slider = position.at(from);
    static constexpr int kDirections[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    for (const auto& direction : kDirections) {
        const bool diagonal = direction[0] != 0 && direction[1] != 0;
        if (slider.type == PieceType::Bishop && !diagonal) continue;
        if (slider.type == PieceType::Rook && diagonal) continue;
        Square first = kNoSquare;
        int file = fileOf(from) + direction[0];
        int rank = rankOf(from) + direction[1];
        for (; file >= 0 && file < 8 && rank >= 0 && rank < 8; file += direction[0], rank += direction[1]) {
            const Square square = makeSquare(file, rank);
            const Piece piece = position.at(square);
            if (piece.empty()) continue;
            if (piece.color == slider.color) break;
            if (first == kNoSquare) {
                first = square;
                continue;
            }
            // Two enemy pieces in a row on the line.
            const Piece front = position.at(first);
            const int frontValue = front.type == PieceType::King ? 100 : pieceValue(front.type);
            const int backValue = piece.type == PieceType::King ? 100 : pieceValue(piece.type);
            if (backValue > frontValue && front.type != PieceType::King) {
                // Pinned to something bigger; worth it when the front piece is not just a defended pawn.
                if (front.type != PieceType::Pawn || position.attackerCount(first, front.color) == 0) tactics.pin = true;
            } else if (frontValue > backValue && (backValue >= 3 || position.attackerCount(square, piece.color) == 0)) {
                tactics.skewer = true;
            }
            break;
        }
    }
}

bool matesFor(const std::optional<Eval>& eval, Color color) {
    return eval && eval->mate && *eval->mate != 0 && (*eval->mate > 0) == (color == Color::White);
}

// Evaluation in centipawns from White's side, mates as +-1000, everything capped at 1000.
int cappedCp(const Eval& eval) {
    if (eval.mate) {
        if (*eval.mate == 0) return eval.cp >= 0 ? 1000 : -1000;
        return *eval.mate > 0 ? 1000 : -1000;
    }
    return std::clamp(eval.cp, -1000, 1000);
}

}  // namespace

std::optional<MoveTactics> analyseMove(std::string_view fen, std::string_view uci) {
    Game game;
    game.setEnforceDraws(false);
    if (!game.reset(fen) || !game.makeUciMove(uci)) return std::nullopt;
    const MoveRecord& record = game.history().back();
    const Position before(fen);
    const Position after(game.fen());
    const Color mover = record.moved.color;
    const Color enemy = opposite(mover);

    MoveTactics tactics;
    tactics.check = record.isCheck;
    tactics.mate = game.status() == Status::Checkmate;
    tactics.promotion = record.move.promotion != PieceType::None;

    if (!record.captured.empty()) {
        const std::vector<Square> hanging = before.hangingPieces(enemy);
        // En passant captures land on an empty square; they are never "hanging pieces".
        tactics.winsHanging = std::find(hanging.begin(), hanging.end(), record.move.to) != hanging.end();
    }

    // Fork: the moved piece attacks two worthwhile targets (a check counts as one).
    // The piece itself must not simply be lost: it needs to be safe or worth less than the targets.
    {
        const PieceType moved = record.move.promotion != PieceType::None ? record.move.promotion : record.moved.type;
        int targets = 0;
        for (const Square square : after.attacksFrom(record.move.to)) {
            const Piece piece = after.at(square);
            if (piece.empty() || piece.color != enemy) continue;
            if (worthwhileTarget(after, square, moved)) ++targets;
        }
        const bool safe = after.attackerCount(record.move.to, enemy) == 0 ||
                          after.attackerCount(record.move.to, mover) > 0 || moved == PieceType::Pawn ||
                          moved == PieceType::Knight;
        if (targets >= 2 && safe) tactics.fork = true;
    }

    if (isSlider(after.at(record.move.to).type)) lineTactics(after, record.move.to, tactics);

    // Discovered attack: another piece of the mover attacks a worthwhile target it did not attack before.
    for (Square square = 0; square < 64 && !tactics.discovery && !record.isCastling; ++square) {
        const Piece piece = after.at(square);
        if (piece.empty() || piece.color != mover || square == record.move.to || !isSlider(piece.type)) continue;
        const std::vector<Square> was = before.at(square) == piece ? before.attacksFrom(square) : std::vector<Square>{};
        for (const Square target : after.attacksFrom(square)) {
            const Piece victim = after.at(target);
            if (victim.empty() || victim.color != enemy) continue;
            if (std::find(was.begin(), was.end(), target) != was.end()) continue;
            if (victim.type == PieceType::King || pieceValue(victim.type) >= pieceValue(piece.type) ||
                (after.attackerCount(target, enemy) == 0 && pieceValue(victim.type) >= 3)) {
                tactics.discovery = true;
                break;
            }
        }
    }
    return tactics;
}

const char* motifName(Motif motif) {
    switch (motif) {
        case Motif::MissedMate: return "Missed a mate";
        case Motif::AllowedMate: return "Allowed a mate";
        case Motif::MissedFork: return "Missed a fork";
        case Motif::AllowedFork: return "Allowed a fork";
        case Motif::MissedPin: return "Missed a pin";
        case Motif::AllowedPin: return "Got pinned";
        case Motif::MissedSkewer: return "Missed a skewer";
        case Motif::AllowedSkewer: return "Got skewered";
        case Motif::MissedDiscovery: return "Missed a discovered attack";
        case Motif::AllowedDiscovery: return "Allowed a discovered attack";
        case Motif::MissedCapture: return "Missed free material";
        case Motif::HungPiece: return "Left a piece hanging";
        case Motif::Positional: return "Positional or deeper";
    }
    return "";
}

const char* motifAdvice(Motif motif) {
    switch (motif) {
        case Motif::MissedMate: return "Before every move, look at all checks: some of them end the game.";
        case Motif::AllowedMate: return "After choosing a move, ask what checks your opponent has next.";
        case Motif::MissedFork: return "Look for squares where one piece, often a knight, hits two targets at once.";
        case Motif::AllowedFork: return "Watch for squares where an enemy knight or pawn can attack two of your pieces.";
        case Motif::MissedPin: return "Pieces in front of a king or queen on a line can often be pinned.";
        case Motif::AllowedPin: return "Avoid lining up your king or queen behind another piece on an open line.";
        case Motif::MissedSkewer: return "A check or attack on a valuable piece can win the piece behind it.";
        case Motif::AllowedSkewer: return "Keep your king and big pieces off the same open line.";
        case Motif::MissedDiscovery: return "Moving a piece out of a line can unleash the piece behind it.";
        case Motif::AllowedDiscovery: return "Mind enemy pieces that stand in front of their own rooks and bishops.";
        case Motif::MissedCapture: return "Count attackers and defenders: some captures are simply free.";
        case Motif::HungPiece: return "Before moving, check that every piece of yours is still defended.";
        case Motif::Positional: return "No quick tactic: these are about plans, piece placement and long lines.";
    }
    return "";
}

const char* motifTheme(Motif motif) {
    switch (motif) {
        case Motif::MissedMate:
        case Motif::AllowedMate: return "mate";
        case Motif::MissedFork:
        case Motif::AllowedFork: return "fork";
        case Motif::MissedPin:
        case Motif::AllowedPin: return "pin";
        case Motif::MissedSkewer:
        case Motif::AllowedSkewer: return "skewer";
        case Motif::MissedDiscovery:
        case Motif::AllowedDiscovery: return "discoveredAttack";
        case Motif::MissedCapture:
        case Motif::HungPiece: return "hangingPiece";
        case Motif::Positional: return "";
    }
    return "";
}

std::vector<Motif> mistakeMotifs(const GameTree& tree, NodeId id) {
    std::vector<Motif> motifs;
    if (!tree.contains(id) || id == kRootNode) return {Motif::Positional};
    const TreeNode& node = tree.node(id);
    const TreeNode& before = tree.node(node.parent);
    const Color user = node.record.moved.color;
    const Color opponent = opposite(user);
    const auto add = [&](Motif motif) {
        if (std::find(motifs.begin(), motifs.end(), motif) == motifs.end()) motifs.push_back(motif);
    };

    // What the engine's move would have done.
    if (matesFor(before.eval, user) && !matesFor(node.eval, user)) add(Motif::MissedMate);
    if (!before.bestMove.empty() && before.bestMove != node.record.uci) {
        if (const std::optional<MoveTactics> best = analyseMove(before.fen, before.bestMove)) {
            if (best->mate) add(Motif::MissedMate);
            if (best->fork) add(Motif::MissedFork);
            if (best->pin) add(Motif::MissedPin);
            if (best->skewer) add(Motif::MissedSkewer);
            if (best->discovery) add(Motif::MissedDiscovery);
            if (best->winsHanging) add(Motif::MissedCapture);
        }
    }
    // What the move played allowed.
    if (matesFor(node.eval, opponent) && !matesFor(before.eval, opponent)) add(Motif::AllowedMate);
    if (!node.bestMove.empty()) {
        if (const std::optional<MoveTactics> reply = analyseMove(node.fen, node.bestMove)) {
            if (reply->mate) add(Motif::AllowedMate);
            if (reply->fork) add(Motif::AllowedFork);
            if (reply->pin) add(Motif::AllowedPin);
            if (reply->skewer) add(Motif::AllowedSkewer);
            if (reply->discovery) add(Motif::AllowedDiscovery);
            if (reply->winsHanging) add(Motif::HungPiece);
        }
    }
    if (motifs.empty()) motifs.push_back(Motif::Positional);
    return motifs;
}

std::vector<std::pair<Motif, int>> TrainingReport::rankedMotifs() const {
    std::vector<std::pair<Motif, int>> ranked;
    for (int i = 0; i < kMotifCount; ++i) {
        if (motifCounts[static_cast<std::size_t>(i)] > 0) ranked.emplace_back(static_cast<Motif>(i), motifCounts[static_cast<std::size_t>(i)]);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return ranked;
}

void addToReport(TrainingReport& report, const GameRecord& record, Color user, std::int64_t gameId) {
    const GameTree& tree = record.tree;
    const std::vector<NodeId> line = tree.mainLine();
    ++report.games;
    int blunders = 0;
    long long lossSum = static_cast<long long>(report.centipawnLoss * report.moves);
    const std::string opponent = record.tag(user == Color::White ? "Black" : "White");

    for (const NodeId id : line) {
        if (tree.mover(id) != user) continue;
        const TreeNode& node = tree.node(id);
        const TreeNode& before = tree.node(node.parent);
        if (!node.eval || !before.eval) continue;
        ++report.moves;
        const int sign = user == Color::White ? 1 : -1;
        lossSum += std::max(0, (cappedCp(*before.eval) - cappedCp(*node.eval)) * sign);

        const MoveQuality quality = classifyMove(tree, id);
        if (quality == MoveQuality::Inaccuracy) ++report.inaccuracies;
        if (quality != MoveQuality::Mistake && quality != MoveQuality::Blunder) continue;
        if (quality == MoveQuality::Blunder) {
            ++report.blunders;
            ++blunders;
        } else {
            ++report.mistakes;
        }

        MistakeExample example;
        example.gameId = gameId;
        example.node = id;
        example.date = record.tag("Date");
        example.opponent = opponent;
        example.label = std::to_string(tree.moveNumber(id)) + (user == Color::White ? ". " : "... ") + node.record.san +
                        (quality == MoveQuality::Blunder ? "??" : "?");
        example.fenBefore = before.fen;
        example.played = node.record.uci;
        example.best = before.bestMove;
        example.quality = quality;
        example.phase = gamePhase(before.fen, before.ply);
        example.loss = expectedScoreLoss(tree, id).value_or(0.0);
        example.motifs = mistakeMotifs(tree, id);
        if (!example.best.empty()) {
            Game scratch;
            scratch.setEnforceDraws(false);
            if (scratch.reset(before.fen) && scratch.makeUciMove(example.best)) example.bestSan = scratch.history().back().san;
        }
        for (const Motif motif : example.motifs) ++report.motifCounts[static_cast<std::size_t>(motif)];
        ++report.phaseErrors[static_cast<std::size_t>(example.phase)];
        report.examples.push_back(std::move(example));
    }
    report.centipawnLoss = report.moves > 0 ? static_cast<double>(lossSum) / report.moves : 0.0;
    report.blundersByGame.push_back(blunders);
}

std::string engineStrengthLabel(std::string_view opponent) {
    const std::size_t open = opponent.rfind('(');
    const std::size_t close = opponent.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) return {};
    const std::string_view inside = opponent.substr(open + 1, close - open - 1);
    if (inside.rfind("level ", 0) == 0 || inside.find(" Elo") != std::string_view::npos) return std::string(inside);
    return {};
}

}  // namespace core

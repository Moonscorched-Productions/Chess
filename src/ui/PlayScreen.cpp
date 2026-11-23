#include "ui/PlayScreen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>

#include <imgui.h>

#include "app/Assets.h"
#include "app/GameAnalyzer.h"
#include "app/Paths.h"
#include "chess/Pgn.h"
#include "chess/OpeningBook.h"
#include "storage/GameStore.h"
#include "storage/TrainingStore.h"
#include "ui/Common.h"
#include "ui/QualityBadge.h"

namespace ui {

namespace {

using core::Color;
using core::PieceType;

constexpr float kBarHeight = 34.0f;
constexpr auto kMinEngineDelay = std::chrono::milliseconds(350);

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

// Material on the board, White minus Black, in pawns.
int materialBalance(const core::Game& game) {
    int balance = 0;
    for (core::Square square = 0; square < 64; ++square) {
        const core::Piece piece = game.pieceAt(square);
        if (piece.empty()) continue;
        balance += piece.color == Color::White ? pieceValue(piece.type) : -pieceValue(piece.type);
    }
    return balance;
}

const char* colorName(Color color) { return color == Color::White ? "White" : "Black"; }


// "4:07" or "1:02:45".
std::string formatDuration(double seconds) {
    const long total = static_cast<long>(seconds);
    char text[32];
    if (total >= 3600) {
        std::snprintf(text, sizeof(text), "%ld:%02ld:%02ld", total / 3600, total / 60 % 60, total % 60);
    } else {
        std::snprintf(text, sizeof(text), "%ld:%02ld", total / 60, total % 60);
    }
    return text;
}

void copyToBuffer(std::array<char, 128>& buffer, const std::string& text) {
    const std::size_t length = std::min(text.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), text.data(), length);
    buffer[length] = '\0';
}

}  // namespace

PlayScreen::PlayScreen(app::Settings& settings, app::Assets& assets, storage::GameStore* store)
    : settings_(settings), assets_(assets), store_(store), badgeAnalyzer_(settings) {
    startNewGame(settings_.playAgainstEngine, settings_.playAs, std::string(core::kStartFen));
}

// --- Engine ------------------------------------------------------------------

void PlayScreen::startEngine() {
    engineError_.clear();
    pendingSearch_ = 0;
    pendingMove_.reset();

    const std::filesystem::path path =
        app::resolveEnginePath(settings_.enginePath);
    if (path.empty()) {
        engine_.shutdown();
        engineError_ = "No chess engine found. Set the engine path in Settings.";
        return;
    }
    std::string error;
    if (!engine_.start(path, &error)) engineError_ = error;
}

void PlayScreen::applyEngineOptions() {
    if (engine_.state() != engine::UciEngine::State::Ready) return;

    const bool hasLimit = engine_.option("UCI_LimitStrength").has_value();
    const auto elo = engine_.option("UCI_Elo");
    const auto skill = engine_.option("Skill Level");

    // Training positions are always played at full strength.
    const bool fullStrength = challenge_.has_value();
    if (!fullStrength && settings_.strengthMode == app::StrengthMode::Elo && hasLimit && elo) {
        const int value = std::clamp(settings_.elo, elo->min.value_or(settings_.elo), elo->max.value_or(settings_.elo));
        if (skill && skill->max) engine_.setOption("Skill Level", std::to_string(*skill->max));
        engine_.setOption("UCI_LimitStrength", "true");
        engine_.setOption("UCI_Elo", std::to_string(value));
    } else {
        if (hasLimit) engine_.setOption("UCI_LimitStrength", "false");
        if (skill) {
            const int wanted = fullStrength ? 20 : app::levelPreset(settings_.level).skillLevel;
            const int value = std::clamp(wanted, skill->min.value_or(wanted), skill->max.value_or(wanted));
            engine_.setOption("Skill Level", std::to_string(value));
        }
    }
    app::applyTablebases(engine_, settings_);
    app::applyExtraOptions(engine_, settings_.engineOptions);
}

void PlayScreen::requestEngineMove() {
    engine::SearchLimits limits;
    limits.moveTimeMs = settings_.thinkTimeMs;
    limits.depth = settings_.depthLimit;
    if (timed()) {
        // With a clock the engine plans its own time; its strength settings still apply.
        const int depth = limits.depth;
        limits = clockLimits();
        limits.depth = depth;
    }
    if (challenge_) {
        limits.depth = 0;
        limits.moveTimeMs = std::max(settings_.thinkTimeMs, 600);
    } else if (settings_.strengthMode == app::StrengthMode::Level) {
        const int cap = app::levelPreset(settings_.level).depth;
        if (cap > 0 && (limits.depth == 0 || cap < limits.depth)) limits.depth = cap;
    }
    pendingSearch_ = engine_.search(game_.startFen(), game_.uciMoves(), limits);
    moveNotBefore_ = Clock::now() + kMinEngineDelay;
}

void PlayScreen::cancelEngineMove() {
    if (pendingSearch_ != 0) engine_.stopSearch();
    pendingSearch_ = 0;
    pendingMove_.reset();
}

// --- Hints -------------------------------------------------------------------

namespace {
constexpr int kHintThinkTimeMs = 1500;
constexpr ImU32 kHintArrowColor = IM_COL32(40, 120, 210, 200);
}  // namespace

void PlayScreen::requestHint() {
    if (!humanCanMove()) return;
    // After an error, a new request starts over (and retries the engine).
    if (game_.fen() != hintFen_ || !hintError_.empty()) clearHint();
    hintError_.clear();
    if (hintLevel_ >= 2) return;
    ++hintLevel_;
    hintFen_ = game_.fen();
    notice_.clear();
    if (hintMove_ || hintSearch_ != 0) return;

    using State = engine::UciEngine::State;
    if (hintEngine_.state() == State::Stopped || hintEngine_.state() == State::Failed) {
        hintError_.clear();
        const std::filesystem::path path = app::resolveEnginePath(settings_.enginePath);
        std::string error;
        if (path.empty()) {
            hintError_ = "No chess engine found for hints. Set the engine path in Settings.";
        } else if (!hintEngine_.start(path, &error)) {
            hintError_ = error;
        }
        // The search starts once the engine reports Ready (see updateHint).
        return;
    }
    if (hintEngine_.state() == State::Ready) {
        hintSearch_ = hintEngine_.search(game_.startFen(), game_.uciMoves(), engine::SearchLimits{kHintThinkTimeMs, 0});
    }
}

void PlayScreen::clearHint() {
    if (hintSearch_ != 0) hintEngine_.stopSearch();
    hintSearch_ = 0;
    hintLevel_ = 0;
    hintMove_.reset();
    hintFen_.clear();
}

void PlayScreen::updateHint() {
    for (const engine::EngineEvent& event : hintEngine_.poll()) {
        switch (event.type) {
            case engine::EngineEvent::Type::Ready:
                hintError_.clear();
                app::applyTablebases(hintEngine_, settings_);
                app::applyExtraOptions(hintEngine_, settings_.engineOptions);
                if (hintLevel_ > 0 && hintSearch_ == 0 && !hintMove_ && game_.fen() == hintFen_) {
                    hintSearch_ = hintEngine_.search(game_.startFen(), game_.uciMoves(),
                                                     engine::SearchLimits{kHintThinkTimeMs, 0});
                }
                break;
            case engine::EngineEvent::Type::Info:
                break;
            case engine::EngineEvent::Type::BestMove:
                if (event.searchId == hintSearch_) {
                    hintSearch_ = 0;
                    hintMove_ = core::parseUciMove(event.bestMove);
                }
                break;
            case engine::EngineEvent::Type::Terminated:
                hintSearch_ = 0;
                hintError_ = event.message.empty() ? "The hint engine stopped unexpectedly." : event.message;
                break;
        }
    }
    // A hint belongs to one position: any move, undo or new game drops it.
    if (hintLevel_ > 0 && game_.fen() != hintFen_) clearHint();
}

std::string PlayScreen::hintButtonLabel() const {
    if (hintLevel_ > 0 && !hintMove_ && hintError_.empty()) {
        return "Thinking" + std::string(static_cast<std::size_t>(ImGui::GetTime() * 3.0) % 4, '.');
    }
    if (hintLevel_ == 1) return "Show the move";
    if (hintLevel_ >= 2) return "Hint shown";
    return "Hint";
}

void PlayScreen::update() {
    using State = engine::UciEngine::State;

    updateHint();
    updatePlayTime();

    for (const engine::EngineEvent& event : engine_.poll()) {
        switch (event.type) {
            case engine::EngineEvent::Type::Ready:
                engineError_.clear();
                engine_.newGame();
                applyEngineOptions();
                break;
            case engine::EngineEvent::Type::Info:
                if (event.searchId == pendingSearch_) {
                    if (event.info.mateIn) {
                        engineScoreCp_ = *event.info.mateIn > 0 ? 100000 : -100000;
                    } else if (event.info.scoreCp) {
                        engineScoreCp_ = *event.info.scoreCp;
                    }
                }
                break;
            case engine::EngineEvent::Type::BestMove:
                if (event.searchId == pendingSearch_) {
                    pendingSearch_ = 0;
                    pendingMove_ = event.bestMove;
                }
                break;
            case engine::EngineEvent::Type::Terminated:
                pendingSearch_ = 0;
                pendingMove_.reset();
                engineError_ = event.message.empty() ? "The engine stopped unexpectedly." : event.message;
                break;
        }
    }

    // The analysis behind badges and the coach runs first, so the coach can hold the reply.
    syncBadges();
    badgeAnalyzer_.update();
    badgeAnalyzer_.takeFinished();
    const bool held = coachHolds();

    // Play the engine's answer once the minimum delay has passed, so instant
    // replies do not land in the same frame as the user's move.
    if (pendingMove_ && Clock::now() >= moveNotBefore_ && !held) {
        const std::string move = *pendingMove_;
        pendingMove_.reset();
        if (engineToMove()) {
            if (game_.makeUciMove(move)) {
                board_.clearInteraction();
                afterMove();
            } else {
                engineError_ = "The engine returned an illegal move (" + move + ").";
            }
        }
    }

    // Taking moves back into the book resumes the practice.
    if (book_ && book_->left) {
        const core::NodeId node = bookNode();
        if (node != core::kNoNode && !book_->tree.node(node).children.empty()) {
            book_->left = false;
            book_->note.clear();
        }
    }

    if (engineToMove() && pendingSearch_ == 0 && !pendingMove_) {
        // From the opening being practised, the engine answers from the book.
        if (const std::optional<std::string> book = bookMove()) {
            pendingMove_ = *book;
            moveNotBefore_ = Clock::now() + std::chrono::milliseconds(500);
        } else if (engineError_.empty() && engine_.state() == State::Ready) {
            requestEngineMove();
        }
    }

    updateClocks(held);

    if (autoplay_ && humanCanMove() && !held) {
        const Clock::time_point now = Clock::now();
        if (autoplayAt_ == Clock::time_point{}) {
            autoplayAt_ = now + std::chrono::milliseconds(120);
        } else if (now >= autoplayAt_) {
            autoplayAt_ = {};
            const std::vector<core::Move> moves = game_.legalMoves();
            if (!moves.empty()) {
                std::uniform_int_distribution<std::size_t> pick(0, moves.size() - 1);
                playMove(moves[pick(rng_)]);
            }
        }
    }

    if (game_.isOver()) saveFinishedGame();
    checkChallenge();
}

// --- Clocks ------------------------------------------------------------------

void PlayScreen::resetClocks(int baseSeconds, int incrementSeconds) {
    clockBaseMs_ = baseSeconds * 1000.0;
    clockIncrementMs_ = baseSeconds > 0 ? incrementSeconds * 1000.0 : 0.0;
    remainingMs_ = {clockBaseMs_, clockBaseMs_};
    clockAtPly_.assign(static_cast<std::size_t>(game_.ply()) + 1, remainingMs_);
    clockTick_ = Clock::now();
}

void PlayScreen::updateClocks(bool paused) {
    const Clock::time_point now = Clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(now - clockTick_).count();
    clockTick_ = now;
    clocksPaused_ = paused;
    // The clocks start once both sides have made a move, and stop while the coach talks.
    if (!timed() || game_.isOver() || game_.ply() < 2 || paused) return;
    const Color side = game_.sideToMove();
    double& left = remainingMs_[side == Color::White ? 0 : 1];
    left -= std::min(elapsed, 1000.0);  // a stalled frame or a sleeping machine costs a second at most
    if (left > 0.0) return;
    left = 0.0;
    cancelEngineMove();
    coach_ = {};
    game_.flag(side);
    if (settings_.soundEnabled) assets_.play(app::SoundId::GameEnd, settings_.soundVolume);
}

void PlayScreen::recordClocks() {
    if (!timed()) return;
    // The side that just moved gets its increment, once the clocks are running.
    if (game_.ply() > 2) {
        remainingMs_[game_.sideToMove() == Color::White ? 1 : 0] += clockIncrementMs_;
    }
    clockAtPly_.resize(static_cast<std::size_t>(game_.ply()) + 1);
    clockAtPly_[static_cast<std::size_t>(game_.ply())] = remainingMs_;
}

void PlayScreen::restoreClocks() {
    if (!timed()) return;
    const auto ply = static_cast<std::size_t>(game_.ply());
    if (ply < clockAtPly_.size()) remainingMs_ = clockAtPly_[ply];
}

engine::SearchLimits PlayScreen::clockLimits() const {
    engine::SearchLimits limits;
    limits.whiteTimeMs = static_cast<int>(remainingMs_[0]);
    limits.blackTimeMs = static_cast<int>(remainingMs_[1]);
    limits.whiteIncrementMs = static_cast<int>(clockIncrementMs_);
    limits.blackIncrementMs = static_cast<int>(clockIncrementMs_);
    return limits;
}

std::string PlayScreen::timeControlText() const {
    if (!timed()) return "-";
    return std::to_string(static_cast<int>(clockBaseMs_ / 1000.0)) + "+" + std::to_string(static_cast<int>(clockIncrementMs_ / 1000.0));
}

// --- Coach -------------------------------------------------------------------

bool PlayScreen::coachHolds() {
    if (!settings_.coachMode || !againstEngine_ || game_.isOver() || game_.history().empty() ||
        game_.sideToMove() == humanColor_) {
        return false;
    }
    // The user's move is the last one; its node in the analysed tree identifies it.
    const std::size_t index = game_.history().size() - 1;
    if (index >= badgeLine_.size()) return true;  // not in the tree yet: next frame
    const core::NodeId node = badgeLine_[index];
    if (coach_.node != node) coach_ = Coach{node, CoachState::Checking, Clock::now()};

    switch (coach_.state) {
        case CoachState::Cleared: return false;
        case CoachState::Warned: return true;
        case CoachState::Checking: break;
    }
    const core::MoveQuality quality = judgedQuality(index);
    if (quality == core::MoveQuality::None) {
        // No verdict without an engine, or when it takes far too long: play on.
        if (!badgeAnalyzer_.error().empty() || Clock::now() - coach_.since > std::chrono::seconds(12)) {
            coach_.state = CoachState::Cleared;
            return false;
        }
        return true;
    }
    if (quality == core::MoveQuality::Mistake || quality == core::MoveQuality::Blunder) {
        coach_.state = CoachState::Warned;
        coach_.quality = quality;
        coach_.better.reset();
        coach_.betterSan.clear();
        const core::TreeNode& before = badgeTree_.node(badgeTree_.node(node).parent);
        core::Game scratch;
        scratch.setEnforceDraws(false);
        if (!before.bestMove.empty() && scratch.reset(before.fen) && scratch.makeUciMove(before.bestMove)) {
            coach_.better = scratch.history().back().move;
            coach_.betterSan = scratch.history().back().san;
        }
        if (settings_.soundEnabled) assets_.play(app::SoundId::GameEnd, settings_.soundVolume / 2);
        return true;
    }
    coach_.state = CoachState::Cleared;
    return false;
}

// --- Opening practice --------------------------------------------------------

core::NodeId PlayScreen::bookNode() const {
    if (!book_) return core::kNoNode;
    core::NodeId node = core::kRootNode;
    for (const core::MoveRecord& played : game_.history()) {
        core::NodeId next = core::kNoNode;
        for (const core::NodeId child : book_->tree.node(node).children) {
            if (book_->tree.node(child).record.uci == played.uci) next = child;
        }
        if (next == core::kNoNode) return core::kNoNode;
        node = next;
    }
    return node;
}

std::optional<std::string> PlayScreen::bookMove() {
    const core::NodeId node = bookNode();
    if (node == core::kNoNode) return std::nullopt;
    const std::vector<core::NodeId>& children = book_->tree.node(node).children;
    if (children.empty()) return std::nullopt;
    std::uniform_int_distribution<std::size_t> pick(0, children.size() - 1);
    return book_->tree.node(children[pick(rng_)]).record.uci;
}

void PlayScreen::checkBookAfterUserMove() {
    if (!book_ || book_->left) return;
    const std::vector<core::MoveRecord>& history = game_.history();
    if (history.empty()) return;
    // The book position before the user's move.
    const core::GameTree& tree = book_->tree;
    core::NodeId node = core::kRootNode;
    for (std::size_t i = 0; i + 1 < history.size() && node != core::kNoNode; ++i) {
        core::NodeId next = core::kNoNode;
        for (const core::NodeId child : tree.node(node).children) {
            if (tree.node(child).record.uci == history[i].uci) next = child;
        }
        node = next;
    }
    if (node == core::kNoNode) return;

    const std::vector<core::NodeId>& children = tree.node(node).children;
    core::NodeId played = core::kNoNode;
    for (const core::NodeId child : children) {
        if (tree.node(child).record.uci == history.back().uci) played = child;
    }
    if (children.empty() || (played != core::kNoNode && tree.node(played).children.empty())) {
        book_->left = true;
        book_->note = "End of the prepared line after " + std::to_string(game_.fullMoveNumber() - (game_.sideToMove() == Color::White ? 1 : 0)) +
                      " moves. You are on your own now.";
    } else if (played == core::kNoNode) {
        book_->left = true;
        std::string expected;
        for (const core::NodeId child : children) {
            if (!expected.empty()) expected += ", ";
            expected += tree.node(child).record.san;
        }
        book_->note = "You left the " + std::string(book_->fromRepertoire ? "repertoire" : "opening") + " with " +
                      history.back().san + ". It expected " + expected + ".";
    }
}

void PlayScreen::updatePlayTime() {
    const Clock::time_point now = Clock::now();
    if (lastTick_ != Clock::time_point{} && game_.ply() > 0 && !game_.isOver()) {
        // A long gap (the machine slept, the window was dragged) counts as one second at most.
        playSeconds_ += std::min(std::chrono::duration<double>(now - lastTick_).count(), 1.0);
    }
    lastTick_ = now;
}

// --- Move badges -------------------------------------------------------------

void PlayScreen::syncBadges() {
    // The coach needs the same judgements as the badges.
    const bool wanted = settings_.playMoveBadges || (settings_.coachMode && againstEngine_);
    const bool turnedOn = wanted && !badgesOn_;
    badgesOn_ = wanted;
    if (!badgesOn_) {
        badgeAnalyzer_.stop();
        return;
    }

    // The tree keeps moves taken back, so undo followed by redo does not lose their evaluations.
    // A different move replaces the old continuation.
    const std::vector<core::MoveRecord>& history = game_.history();
    std::vector<core::NodeId> line = badgeTree_.mainLine();
    std::size_t common = 0;
    while (common < line.size() && common < history.size() &&
           badgeTree_.node(line[common]).record.uci == history[common].uci) {
        ++common;
    }
    bool changed = false;
    if (common < history.size()) {
        if (common < line.size()) {
            badgeAnalyzer_.stop();  // it may be searching a position that is about to go
            badgeTree_.remove(line[common]);
        }
        line.resize(common);
        core::Game replay;
        replay.setEnforceDraws(false);
        core::NodeId current = common == 0 ? core::kRootNode : line.back();
        if (core::loadPosition(badgeTree_, current, replay)) {
            for (std::size_t i = common; i < history.size(); ++i) {
                const core::NodeId next = core::playMove(badgeTree_, current, replay, history[i].move);
                if (next == core::kNoNode) break;
                line.push_back(next);
                current = next;
            }
        }
        changed = true;
    }
    line.resize(std::min(line.size(), history.size()));
    badgeLine_ = std::move(line);
    if (changed || turnedOn) badgeAnalyzer_.follow(badgeTree_);
}

core::MoveQuality PlayScreen::badgeQuality(std::size_t index) const {
    return settings_.playMoveBadges ? judgedQuality(index) : core::MoveQuality::None;
}

core::MoveQuality PlayScreen::judgedQuality(std::size_t index) const {
    if (!badgesOn_ || index >= badgeLine_.size() || !badgeTree_.contains(badgeLine_[index])) return core::MoveQuality::None;
    return core::classifyMove(badgeTree_, badgeLine_[index]);
}

core::GameRecord PlayScreen::currentRecord() const {
    core::GameRecord record = core::recordFromGame(game_);
    std::string engineName = engine_.name();
    if (engineName.empty()) engineName = "Computer";
    if (settings_.strengthMode == app::StrengthMode::Elo) {
        engineName += " (" + std::to_string(settings_.elo) + " Elo)";
    } else {
        engineName += " (level " + std::to_string(settings_.level) + ")";
    }

    record.setTag("Event", againstEngine_ ? "Game against the computer" : "Game on one board");
    record.setTag("Site", "Chess Desktop");
    record.setTag("Date", core::pgnDateToday());
    record.setTag("Round", "-");
    record.setTag("TimeControl", timeControlText());
    if (book_) record.setTag("Annotator", "Practised: " + book_->name);
    if (againstEngine_) {
        record.setTag("White", humanColor_ == Color::White ? "You" : engineName);
        record.setTag("Black", humanColor_ == Color::Black ? "You" : engineName);
    } else {
        record.setTag("White", "White");
        record.setTag("Black", "Black");
    }
    const char* termination = nullptr;
    switch (game_.status()) {
        case core::Status::Resigned: termination = "resignation"; break;
        case core::Status::Timeout: termination = "time forfeit"; break;
        case core::Status::DrawAgreed: termination = "draw agreed"; break;
        case core::Status::DrawRepetition: termination = "threefold repetition"; break;
        case core::Status::DrawFiftyMove: termination = "fifty-move rule"; break;
        case core::Status::DrawInsufficientMaterial: termination = "insufficient material"; break;
        default: break;
    }
    if (termination != nullptr) record.setTag("Termination", termination);
    return record;
}

void PlayScreen::saveFinishedGame() {
    if (store_ == nullptr || !settings_.saveGamesToLibrary || game_.ply() == 0 || challenge_) return;
    // Already stored in this state? (Undoing and finishing differently updates the entry.)
    if (savedGameId_ && savedResult_ == game_.result() && savedPly_ == game_.ply()) return;

    const core::GameRecord record = currentRecord();
    if (savedGameId_) {
        store_->update(*savedGameId_, record);
    } else {
        savedGameId_ = store_->add(record, "play");
    }
    savedResult_ = game_.result();
    savedPly_ = game_.ply();
    libraryChanged_ = true;
}

void PlayScreen::startChallenge(const core::EndgamePosition& position) {
    startNewGame(true, position.userColor == Color::White ? app::ColorChoice::White : app::ColorChoice::Black, position.fen);
    resetClocks(0, 0);  // training positions are untimed
    challenge_ = Challenge{position.id, position.name, position.goal};
    challengeReported_ = false;
    applyEngineOptions();  // the strength depends on the challenge
    notice_ = position.hint;
}

std::optional<PlayScreen::ChallengeResult> PlayScreen::takeChallengeResult() {
    return std::exchange(challengeResult_, std::nullopt);
}

void PlayScreen::checkChallenge() {
    if (!challenge_ || challengeReported_) return;

    if (!game_.isOver()) {
        // A drawn position can take the engine very long to give up on. After 25 moves each,
        // if the engine still sees no real advantage, the defence has succeeded.
        const bool held = challenge_->goal == core::EndgameGoal::Draw && game_.sideToMove() == humanColor_ &&
                          game_.ply() >= 50 && engineScoreCp_ && *engineScoreCp_ <= 100;
        if (!held) return;
        cancelEngineMove();
        game_.agreeDraw();
    }

    const core::Result result = game_.result();
    const bool userWon = (result == core::Result::WhiteWins && humanColor_ == Color::White) ||
                         (result == core::Result::BlackWins && humanColor_ == Color::Black);
    const bool success = challenge_->goal == core::EndgameGoal::Win ? userWon : (userWon || result == core::Result::Draw);
    challengeReported_ = true;
    challengeResult_ = ChallengeResult{challenge_->id, success};
    notice_ = success ? "Goal reached." : "Goal missed. Start the position again from Endgames to retry.";
}

app::PresenceText PlayScreen::presence() const {
    app::PresenceText text;
    if (againstEngine_) {
        std::string opponent = engine_.name();
        if (opponent.empty()) opponent = "the computer";
        if (settings_.strengthMode == app::StrengthMode::Elo) {
            opponent += " (" + std::to_string(settings_.elo) + " Elo)";
        } else {
            opponent += " (level " + std::to_string(settings_.level) + ")";
        }
        text.details = challenge_ ? "Practising an endgame" : "Playing " + opponent;
    } else {
        text.details = "Playing over the board";
    }

    if (game_.isOver()) {
        text.state = statusText();
    } else if (game_.ply() == 0) {
        text.state = againstEngine_ ? std::string("Starting as ") + (humanColor_ == Color::White ? "White" : "Black")
                                    : "Starting a game";
    } else {
        text.state = "Move " + std::to_string(game_.fullMoveNumber());
        if (againstEngine_) text.state += std::string(" as ") + (humanColor_ == Color::White ? "White" : "Black");
    }
    return text;
}

std::optional<PlayScreen::ReviewRequest> PlayScreen::takeReviewRequest() { return std::exchange(reviewRequest_, std::nullopt); }

std::optional<core::GameRecord> PlayScreen::takeAnalysisRequest() { return std::exchange(analysisRequest_, std::nullopt); }

bool PlayScreen::consumeLibraryChanged() { return std::exchange(libraryChanged_, false); }

void PlayScreen::onSettingsChanged(bool enginePathChanged) {
    if (enginePathChanged) {
        clearHint();
        hintEngine_.shutdown();  // restarted with the new path on the next hint
        hintError_.clear();
        cancelEngineMove();
        if (againstEngine_) startEngine();
    } else {
        applyEngineOptions();
    }
}

bool PlayScreen::consumeSettingsChanged() { return std::exchange(settingsChanged_, false); }

// --- Game actions ------------------------------------------------------------

void PlayScreen::startNewGame(bool againstEngine, app::ColorChoice playAs, const std::string& fen) {
    cancelEngineMove();
    if (!game_.reset(fen)) game_.reset();

    againstEngine_ = againstEngine;
    switch (playAs) {
        case app::ColorChoice::White: humanColor_ = Color::White; break;
        case app::ColorChoice::Black: humanColor_ = Color::Black; break;
        case app::ColorChoice::Random: humanColor_ = (rng_() & 1u) != 0 ? Color::White : Color::Black; break;
    }
    flipped_ = againstEngine_ && humanColor_ == Color::Black;

    board_.clearInteraction();
    notice_.clear();
    engineScoreCp_.reset();
    lastListedPly_ = -1;
    savedGameId_.reset();
    savedResult_ = core::Result::None;
    savedPly_ = -1;
    challenge_.reset();
    challengeReported_ = false;
    playSeconds_ = 0.0;

    badgeAnalyzer_.stop();
    badgeTree_ = core::GameTree(game_.startFen());
    badgeLine_.clear();
    badgesOn_ = false;  // the next update starts the analysis again if badges are on
    coach_ = {};
    book_.reset();
    resetClocks(settings_.clockBaseSeconds, settings_.clockIncrementSeconds);

    if (!againstEngine_) return;
    const engine::UciEngine::State state = engine_.state();
    if (state == engine::UciEngine::State::Ready) {
        engineError_.clear();
        engine_.newGame();
        applyEngineOptions();
    } else if (state != engine::UciEngine::State::Starting) {
        startEngine();
    }
}

void PlayScreen::playMove(core::Move move) {
    if (!humanCanMove()) return;
    if (!game_.makeMove(move)) return;
    notice_.clear();
    board_.clearInteraction();
    afterMove();
    checkBookAfterUserMove();
}

void PlayScreen::afterMove() {
    recordClocks();
    playSoundForLastMove();
}

void PlayScreen::playSoundForLastMove() {
    if (!settings_.soundEnabled || game_.history().empty()) return;
    const core::MoveRecord& record = game_.history().back();
    app::SoundId id = app::SoundId::Move;
    if (game_.isOver()) {
        id = app::SoundId::GameEnd;
    } else if (!record.captured.empty()) {
        id = app::SoundId::Capture;
    }
    assets_.play(id, settings_.soundVolume);
}

void PlayScreen::undo() {
    if (!game_.canUndo()) return;
    cancelEngineMove();
    notice_.clear();
    game_.undo();
    // Against the engine, take back the reply as well so it is the user's turn again.
    if (againstEngine_ && game_.sideToMove() != humanColor_ && game_.canUndo()) game_.undo();
    board_.clearInteraction();
    restoreClocks();
}

void PlayScreen::redo() {
    if (!game_.canRedo()) return;
    cancelEngineMove();
    notice_.clear();
    game_.redo();
    if (againstEngine_ && game_.sideToMove() != humanColor_ && game_.canRedo()) game_.redo();
    board_.clearInteraction();
    restoreClocks();
}

void PlayScreen::markDraw() {
    if (game_.isOver()) return;
    cancelEngineMove();
    game_.agreeDraw();
    notice_ = "Game marked as a draw. Undo takes it back.";
    playSoundForLastMove();
    if (settings_.soundEnabled && game_.history().empty()) assets_.play(app::SoundId::GameEnd, settings_.soundVolume);
}

bool PlayScreen::engineToMove() const {
    return againstEngine_ && !game_.isOver() && game_.sideToMove() != humanColor_;
}

bool PlayScreen::humanCanMove() const {
    return !game_.isOver() && (!againstEngine_ || game_.sideToMove() == humanColor_);
}

std::string PlayScreen::engineLabel() const {
    std::string label = engine_.name();
    if (label.empty()) label = "Computer";
    if (challenge_) {
        label += "  \xC2\xB7  full strength";
    } else if (settings_.strengthMode == app::StrengthMode::Elo) {
        label += "  \xC2\xB7  " + std::to_string(settings_.elo) + " Elo";
    } else {
        label += "  \xC2\xB7  Level " + std::to_string(settings_.level) + " (" + app::levelPreset(settings_.level).name + ")";
    }
    return label;
}

std::string PlayScreen::statusText() const {
    const auto winner = [&]() { return game_.result() == core::Result::WhiteWins ? "White" : "Black"; };
    switch (game_.status()) {
        case core::Status::Checkmate: return std::string("Checkmate  \xC2\xB7  ") + winner() + " wins";
        case core::Status::Stalemate: return "Draw by stalemate";
        case core::Status::DrawRepetition: return "Draw by repetition";
        case core::Status::DrawFiftyMove: return "Draw by the fifty-move rule";
        case core::Status::DrawInsufficientMaterial: return "Draw  \xC2\xB7  insufficient material";
        case core::Status::DrawAgreed: return "Drawn";
        case core::Status::Resigned:
            return std::string(game_.result() == core::Result::WhiteWins ? "Black" : "White") + " resigned  \xC2\xB7  " +
                   winner() + " wins";
        case core::Status::Timeout:
            // The side to move is the one whose clock ran out.
            return std::string(colorName(game_.sideToMove())) + " ran out of time  \xC2\xB7  " +
                   (game_.result() == core::Result::Draw ? std::string("draw") : std::string(winner()) + " wins");
        case core::Status::Ongoing: break;
    }
    if (!againstEngine_) return std::string(colorName(game_.sideToMove())) + " to move";
    if (game_.sideToMove() == humanColor_) return "Your move";
    if (settings_.coachMode && coach_.node != core::kNoNode && coach_.state == CoachState::Checking) {
        return "Coach is checking your move" + std::string(static_cast<std::size_t>(ImGui::GetTime() * 3.0) % 4, '.');
    }
    if (settings_.coachMode && coach_.state == CoachState::Warned) return "Coach: think again?";
    if (!engineError_.empty()) return "Engine unavailable";
    if (engine_.state() == engine::UciEngine::State::Starting) return "Starting engine...";
    return "Computer is thinking" + std::string(static_cast<std::size_t>(ImGui::GetTime() * 3.0) % 4, '.');
}

// --- Drawing -----------------------------------------------------------------

void PlayScreen::draw() {
    handleShortcuts();

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float panelWidth = std::clamp(avail.x * 0.3f, 280.0f, 380.0f);
    const float barBlock = kBarHeight + style.ItemSpacing.y;
    float boardSize = std::min(avail.x - panelWidth - style.ItemSpacing.x, avail.y - 2.0f * barBlock);
    boardSize = std::floor(std::max(boardSize, 240.0f) / 8.0f) * 8.0f;

    // Centre the board and panel when the window is wider than they need.
    const float used = boardSize + style.ItemSpacing.x + panelWidth;
    if (avail.x > used) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((avail.x - used) * 0.5f));

    const Color topColor = flipped_ ? Color::White : Color::Black;

    BoardOptions options = boardOptionsFrom(settings_);
    options.flipped = flipped_;
    options.interactive = humanCanMove();
    // Blindfold: play from the move list alone. The pieces come back when the game is over.
    options.hidePieces = !game_.isOver() && blindfoldHidden(settings_);
    if (options.hidePieces) options.showLegalMoves = false;
    if (!options.hidePieces && game_.ply() > 0) {
        const std::size_t lastIndex = game_.history().size() - 1;
        if (const core::MoveQuality quality = badgeQuality(lastIndex); quality != core::MoveQuality::None) {
            options.badges.push_back(BoardBadge{game_.history().back().move.to, quality});
        }
    }
    if (settings_.coachMode && coach_.state == CoachState::Warned && !game_.isOver() && !options.hidePieces) {
        const core::Move played = game_.history().back().move;
        if (options.badges.empty()) options.badges.push_back(BoardBadge{played.to, coach_.quality});
        if (coach_.better) options.arrows.push_back(BoardArrow{coach_.better->from, coach_.better->to, IM_COL32(96, 160, 48, 215)});
    }
    if (hintLevel_ > 0 && hintMove_ && humanCanMove()) {
        options.highlights.push_back(hintMove_->from);
        if (hintLevel_ >= 2) options.arrows.push_back(BoardArrow{hintMove_->from, hintMove_->to, kHintArrowColor});
    }

    ImGui::BeginGroup();
    drawPlayerBar(topColor, boardSize);
    const std::optional<core::Move> move = board_.draw(game_, assets_, options, boardSize);
    drawPlayerBar(core::opposite(topColor), boardSize);
    ImGui::EndGroup();

    ImGui::SameLine();
    drawSidePanel(panelWidth, boardSize + 2.0f * barBlock);

    drawNewGameDialog();
    drawResignDialog();

    if (move) playMove(*move);
}

void PlayScreen::drawPlayerBar(Color color, float width) {
    ImGui::PushID(color == Color::White ? "bar-white" : "bar-black");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##bar", ImVec2(width, kBarHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const float textY = (kBarHeight - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::SetCursorPosY(textY);

    std::string name;
    if (!againstEngine_) {
        name = colorName(color);
    } else if (color == humanColor_) {
        name = std::string("You  \xC2\xB7  ") + colorName(color);
    } else {
        name = engineLabel();
    }
    const bool toMove = !game_.isOver() && game_.sideToMove() == color;
    ImGui::PushStyleColor(ImGuiCol_Text, toMove ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(name.c_str());
    ImGui::PopStyleColor();

    // Captured pieces, most valuable first, followed by the material lead.
    std::vector<PieceType> captured = game_.capturedBy(color);
    std::stable_sort(captured.begin(), captured.end(),
                     [](PieceType a, PieceType b) { return pieceValue(a) > pieceValue(b); });

    ImGui::SameLine(0.0f, 14.0f);
    const float iconSize = 24.0f;
    const float step = 15.0f;
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    cursor.y = ImGui::GetWindowPos().y + (kBarHeight - iconSize) * 0.5f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float x = cursor.x;
    PieceType previous = PieceType::None;
    for (const PieceType type : captured) {
        if (previous != PieceType::None && type != previous) x += 6.0f;
        drawPiece(drawList, assets_, core::Piece{type, core::opposite(color)}, ImVec2(x, cursor.y), iconSize);
        x += step;
        previous = type;
    }
    if (!captured.empty()) x += iconSize - step + 6.0f;

    const int balance = materialBalance(game_);
    const int lead = color == Color::White ? balance : -balance;
    if (lead > 0) {
        char text[16];
        std::snprintf(text, sizeof(text), "+%d", lead);
        drawList->AddText(ImVec2(x, ImGui::GetWindowPos().y + textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    }

    if (timed()) drawClock(color, ImGui::GetWindowPos().x + width);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void PlayScreen::drawClock(Color color, float right) {
    const double ms = remainingMs_[color == Color::White ? 0 : 1];
    char text[32];
    const long seconds = static_cast<long>(std::ceil(ms / 1000.0));
    if (ms < 10000.0) {
        std::snprintf(text, sizeof(text), "0:%04.1f", std::max(ms, 0.0) / 1000.0);
    } else if (seconds >= 3600) {
        std::snprintf(text, sizeof(text), "%ld:%02ld:%02ld", seconds / 3600, seconds / 60 % 60, seconds % 60);
    } else {
        std::snprintf(text, sizeof(text), "%ld:%02ld", seconds / 60, seconds % 60);
    }
    const bool running = !game_.isOver() && !clocksPaused_ && game_.ply() >= 2 && game_.sideToMove() == color;
    const bool low = ms < std::min(20000.0, clockBaseMs_ * 0.1 + 1.0);

    const ImVec2 size = ImGui::CalcTextSize(text);
    const float padX = 10.0f;
    const ImVec2 min(right - size.x - 2.0f * padX, ImGui::GetWindowPos().y + 2.0f);
    const ImVec2 max(right, ImGui::GetWindowPos().y + kBarHeight - 2.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 background = low && running ? IM_COL32(190, 50, 40, 255)
                             : running      ? ImGui::GetColorU32(ImGuiCol_ButtonActive)
                                            : ImGui::GetColorU32(ImGuiCol_FrameBg);
    drawList->AddRectFilled(min, max, background, 4.0f);
    const ImU32 textColor = running ? IM_COL32(255, 255, 255, 255)
                            : low   ? IM_COL32(220, 80, 70, 255)
                                    : ImGui::GetColorU32(ImGuiCol_Text);
    drawList->AddText(ImVec2(min.x + padX, (min.y + max.y - size.y) * 0.5f), textColor, text);
}

void PlayScreen::drawCoach() {
    if (!settings_.coachMode || coach_.state != CoachState::Warned || game_.isOver()) return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, (qualityColor(coach_.quality) & ~IM_COL32_A_MASK) | (48u << IM_COL32_A_SHIFT));
    ImGui::BeginChild("##coach", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    const std::string& san = game_.history().back().san;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("%s was %s.", san.c_str(), coach_.quality == core::MoveQuality::Blunder ? "a blunder" : "a mistake");
    if (!coach_.betterSan.empty()) {
        ImGui::TextDisabled("%s was stronger (the green arrow). Take the move back and look again?", coach_.betterSan.c_str());
    } else {
        ImGui::TextDisabled("Take the move back and look again?");
    }
    ImGui::PopTextWrapPos();
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Take back", ImVec2(half, 0.0f))) undo();
    ImGui::SameLine();
    if (ImGui::Button("Play on", ImVec2(half, 0.0f))) coach_.state = CoachState::Cleared;
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

PlayScreen::Book PlayScreen::bookFromLine(const core::OpeningLine& line) {
    Book book;
    book.name = line.eco + "  " + line.name;
    core::Game replay;
    replay.setEnforceDraws(false);
    core::NodeId node = core::kRootNode;
    for (const std::string& uci : line.moves) {
        const std::optional<core::Move> move = core::parseUciMove(uci);
        if (!move) break;
        const core::NodeId next = core::playMove(book.tree, node, replay, *move);
        if (next == core::kNoNode) break;
        node = next;
    }
    return book;
}

bool PlayScreen::practiseOpening(std::string_view search) {
    if (openingBook_ == nullptr) return false;
    for (const core::OpeningLine& line : openingBook_->lines()) {
        if (line.name.find(search) == std::string::npos) continue;
        startNewGame(true, settings_.playAs, std::string(core::kStartFen));
        book_ = bookFromLine(line);
        return true;
    }
    return false;
}

void PlayScreen::setOpeningSources(const core::OpeningBook* book, storage::TrainingStore* repertoires) {
    openingBook_ = book;
    repertoires_ = repertoires;
}

void PlayScreen::drawSidePanel(float width, float height) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::BeginChild("##panel", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding);

    const bool over = game_.isOver();
    ImGui::PushStyleColor(ImGuiCol_Text, over ? kAccentColor : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::TextUnformatted(statusText().c_str());
    ImGui::PopStyleColor();
    if (game_.ply() > 0) {
        const std::string time = formatDuration(playSeconds_);
        ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(time.c_str()).x, ImGui::GetCursorPosX()));
        ImGui::TextDisabled("%s", time.c_str());
        ImGui::SetItemTooltip("Time spent on this game");
    }

    if (challenge_) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s  ·  goal: %s", challenge_->title.c_str(),
                            challenge_->goal == core::EndgameGoal::Win ? "win" : "hold the draw");
        ImGui::PopTextWrapPos();
    }
    if (!notice_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", notice_.c_str());
        ImGui::PopTextWrapPos();
    }

    if (book_) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Practising: %s", book_->name.c_str());
        if (!book_->note.empty()) ImGui::TextDisabled("%s", book_->note.c_str());
        ImGui::PopTextWrapPos();
    }
    drawCoach();

    if (!hintError_.empty() && hintLevel_ > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        ImGui::TextWrapped("%s", hintError_.c_str());
        ImGui::PopStyleColor();
    }

    if (againstEngine_ && !engineError_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        ImGui::TextWrapped("%s", engineError_.c_str());
        ImGui::PopStyleColor();
        if (ImGui::Button("Retry engine")) startEngine();
    }

    ImGui::Separator();

    const float rowHeight = ImGui::GetFrameHeightWithSpacing();
    const float buttonsHeight = rowHeight * 5.0f;
    const float listHeight = std::max(ImGui::GetContentRegionAvail().y - buttonsHeight - style.ItemSpacing.y, 60.0f);
    drawMoveList(listHeight);

    const float full = ImGui::GetContentRegionAvail().x;
    const float third = (full - 2.0f * style.ItemSpacing.x) / 3.0f;
    const ImVec2 size(third, 0.0f);

    ImGui::BeginDisabled(!game_.canUndo());
    if (ImGui::Button("Undo", size)) undo();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Take back a move (Left arrow)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!game_.canRedo());
    if (ImGui::Button("Redo", size)) redo();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Replay a move taken back (Right arrow)");
    ImGui::SameLine();
    if (ImGui::Button("Flip", size)) flipped_ = !flipped_;
    ImGui::SetItemTooltip("Flip the board (F)");

    if (ImGui::Button("New game", size)) openNewGameDialog();
    ImGui::SetItemTooltip("Start a new game (Ctrl+N)");
    ImGui::SameLine();
    ImGui::BeginDisabled(over);
    if (ImGui::Button("Resign", size)) wantResignDialog_ = true;
    ImGui::SameLine();
    // In endgame training a draw has to be earned on the board.
    ImGui::BeginDisabled(challenge_.has_value());
    if (ImGui::Button("Draw", size)) markDraw();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Mark the game as a draw (Undo takes it back)");

    const ImVec2 half = size;
    if (ImGui::Button("Copy FEN", half)) {
        ImGui::SetClipboardText(game_.fen().c_str());
        notice_ = "Position copied to the clipboard.";
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(game_.ply() == 0);
    if (ImGui::Button("Analyse", half)) analysisRequest_ = currentRecord();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Open this game in Analysis");
    ImGui::SameLine();
    if (ImGui::Button(settings_.blindfold ? "Show pieces" : "Blindfold", size)) {
        settings_.blindfold = !settings_.blindfold;
        settingsChanged_ = true;
    }
    ImGui::SetItemTooltip("Hide the pieces and play from memory (B). Hold Space to peek.");

    if (ImGui::Checkbox("Move badges", &settings_.playMoveBadges)) settingsChanged_ = true;
    ImGui::SetItemTooltip("Judge every move as it is played (Best, Mistake, Blunder, ...)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!againstEngine_);
    if (ImGui::Checkbox("Coach", &settings_.coachMode)) {
        settingsChanged_ = true;
        coach_ = {};
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("After a mistake or blunder, the computer waits and offers to take your move back");
    if (settings_.playMoveBadges) {
        if (!badgeAnalyzer_.error().empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kErrorColor, "Engine unavailable");
            ImGui::SetItemTooltip("%s", badgeAnalyzer_.error().c_str());
        } else if (badgeAnalyzer_.running() && badgeAnalyzer_.done() < badgeAnalyzer_.total()) {
            ImGui::SameLine();
            ImGui::TextDisabled("Judging%s", std::string(static_cast<std::size_t>(ImGui::GetTime() * 3.0) % 4, '.').c_str());
        }
    }

    if (!over) {
        const bool waiting = hintLevel_ > 0 && !hintMove_ && hintError_.empty();
        ImGui::BeginDisabled(!humanCanMove() || hintLevel_ >= 2 || waiting);
        if (ImGui::Button((hintButtonLabel() + "###hint").c_str(), ImVec2(full, 0.0f))) requestHint();
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("First press: the piece to move. Second press: the move itself (H)");
    }

    if (over && game_.ply() > 0) {
        if (ImGui::Button("Review game", ImVec2(full, 0.0f))) {
            // Only a game that is stored in this very state counts in the statistics.
            const bool stored = savedGameId_ && savedPly_ == game_.ply() && savedResult_ == game_.result();
            reviewRequest_ = ReviewRequest{currentRecord(), stored ? savedGameId_ : std::nullopt};
        }
        ImGui::SetItemTooltip("Let the engine check every move: accuracy, mistakes and key moments");
    }

    ImGui::EndChild();
}

void PlayScreen::drawMoveList(float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##moves", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

    const std::vector<core::MoveRecord>& history = game_.history();
    if (history.empty()) {
        ImGui::TextDisabled("No moves yet.");
    } else if (ImGui::BeginTable("##movetable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("White");
        ImGui::TableSetupColumn("Black");

        const bool blackStarts = game_.startSideToMove() == Color::Black;
        // Index into the list as if White always moved first; a leading gap stands for the missing white move.
        const std::size_t offset = blackStarts ? 1 : 0;
        const std::size_t cells = history.size() + offset;
        const std::size_t last = history.size() - 1;

        for (std::size_t cell = 0; cell < cells; ++cell) {
            if (cell % 2 == 0) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%d.", game_.startFullMoveNumber() + static_cast<int>(cell / 2));
            }
            ImGui::TableSetColumnIndex(1 + static_cast<int>(cell % 2));
            if (cell < offset) {
                ImGui::TextDisabled("...");
                continue;
            }
            const std::size_t index = cell - offset;
            if (index == last) {
                ImGui::TextColored(kAccentColor, "%s", history[index].san.c_str());
            } else {
                ImGui::TextUnformatted(history[index].san.c_str());
            }
            if (const core::MoveQuality quality = badgeQuality(index); quality != core::MoveQuality::None) {
                ImGui::SameLine(0.0f, 6.0f);
                const float size = ImGui::GetTextLineHeight();
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                drawQualityBadge(ImGui::GetWindowDrawList(), ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f), size * 0.5f, quality);
                ImGui::Dummy(ImVec2(size, size));
                ImGui::SetItemTooltip("%s", core::qualityName(quality));
            }
        }
        ImGui::EndTable();
    }

    if (lastListedPly_ != game_.ply()) {
        lastListedPly_ = game_.ply();
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void PlayScreen::openNewGameDialog() {
    draft_.againstEngine = settings_.playAgainstEngine;
    draft_.playAs = settings_.playAs;
    draft_.strengthMode = settings_.strengthMode;
    draft_.level = settings_.level;
    draft_.elo = settings_.elo;
    draft_.thinkTimeMs = settings_.thinkTimeMs;
    draft_.depthLimit = settings_.depthLimit;
    draft_.clockBaseSeconds = settings_.clockBaseSeconds;
    draft_.clockIncrementSeconds = settings_.clockIncrementSeconds;
    draft_.repertoires.clear();
    draft_.repertoireColors.clear();
    if (repertoires_ != nullptr) {
        for (const storage::RepertoireInfo& info : repertoires_->repertoires()) {
            draft_.repertoires.emplace_back(info.id, info.name);
            draft_.repertoireColors.push_back(info.color);
        }
    }
    draft_.error.clear();
    wantNewGameDialog_ = true;
}

void PlayScreen::drawNewGameDialog() {
    if (wantNewGameDialog_) {
        ImGui::OpenPopup("New game");
        wantNewGameDialog_ = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(460.0f, 0.0f), ImVec2(460.0f, viewport->WorkSize.y * 0.92f));
    if (!ImGui::BeginPopupModal("New game", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) return;

    ImGui::SeparatorText("Opponent");
    if (ImGui::RadioButton("Computer", draft_.againstEngine)) draft_.againstEngine = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Two players on this board", !draft_.againstEngine)) draft_.againstEngine = false;

    ImGui::BeginDisabled(!draft_.againstEngine);
    ImGui::SeparatorText("Play as");
    const auto colorRadio = [&](const char* label, app::ColorChoice choice) {
        if (ImGui::RadioButton(label, draft_.playAs == choice)) draft_.playAs = choice;
    };
    colorRadio("White", app::ColorChoice::White);
    ImGui::SameLine();
    colorRadio("Black", app::ColorChoice::Black);
    ImGui::SameLine();
    colorRadio("Random", app::ColorChoice::Random);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Time control");
    {
        struct Preset {
            const char* label;
            int base;
            int increment;
        };
        static constexpr Preset kPresets[] = {
            {"Untimed", 0, 0},          {"Bullet  1 + 0", 60, 0},    {"Bullet  2 + 1", 120, 1},
            {"Blitz  3 + 2", 180, 2},   {"Blitz  5 + 0", 300, 0},    {"Blitz  5 + 3", 300, 3},
            {"Rapid  10 + 0", 600, 0},  {"Rapid  10 + 5", 600, 5},   {"Rapid  15 + 10", 900, 10},
            {"Classical  30 + 0", 1800, 0}, {"Classical  30 + 20", 1800, 20},
        };
        const char* current = "Custom";
        for (const Preset& preset : kPresets) {
            if (preset.base == draft_.clockBaseSeconds && preset.increment == draft_.clockIncrementSeconds) current = preset.label;
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##timecontrol", current)) {
            for (const Preset& preset : kPresets) {
                if (ImGui::Selectable(preset.label, current == preset.label)) {
                    draft_.clockBaseSeconds = preset.base;
                    draft_.clockIncrementSeconds = preset.increment;
                }
            }
            ImGui::EndCombo();
        }
        if (draft_.clockBaseSeconds > 0) {
            int minutes = std::max(1, draft_.clockBaseSeconds / 60);
            const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::SetNextItemWidth(half);
            if (ImGui::SliderInt("##minutes", &minutes, 1, 90, "%d min", ImGuiSliderFlags_AlwaysClamp)) {
                draft_.clockBaseSeconds = minutes * 60;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(half);
            ImGui::SliderInt("##increment", &draft_.clockIncrementSeconds, 0, 60, "+ %d s per move", ImGuiSliderFlags_AlwaysClamp);
        }
    }

    ImGui::BeginDisabled(!draft_.againstEngine);
    ImGui::SeparatorText("Strength");
    if (ImGui::RadioButton("Level", draft_.strengthMode == app::StrengthMode::Level)) draft_.strengthMode = app::StrengthMode::Level;
    ImGui::SameLine();
    if (ImGui::RadioButton("Elo rating", draft_.strengthMode == app::StrengthMode::Elo)) draft_.strengthMode = app::StrengthMode::Elo;
    ImGui::SetNextItemWidth(-1.0f);
    if (draft_.strengthMode == app::StrengthMode::Level) {
        char format[48];
        std::snprintf(format, sizeof(format), "Level %%d  \xC2\xB7  %s", app::levelPreset(draft_.level).name);
        ImGui::SliderInt("##level", &draft_.level, 1, 8, format, ImGuiSliderFlags_AlwaysClamp);
    } else {
        ImGui::SliderInt("##elo", &draft_.elo, 1320, 3190, "%d Elo", ImGuiSliderFlags_AlwaysClamp);
    }

    ImGui::SeparatorText("Thinking");
    float seconds = static_cast<float>(draft_.thinkTimeMs) / 1000.0f;
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::BeginDisabled(draft_.clockBaseSeconds > 0);
    if (ImGui::SliderFloat("##time", &seconds, 0.1f, 10.0f,
                           draft_.clockBaseSeconds > 0 ? "Uses its clock" : "Up to %.1f s per move", ImGuiSliderFlags_AlwaysClamp)) {
        draft_.thinkTimeMs = static_cast<int>(std::lround(seconds * 10.0f)) * 100;
    }
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##depth", &draft_.depthLimit, 0, 30, draft_.depthLimit == 0 ? "No depth limit" : "Depth limit %d",
                     ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();

    if (draft_.againstEngine) drawOpeningChoice();

    ImGui::SeparatorText("Start position");
    if (draft_.againstEngine && draft_.openingMode != 0) draft_.customPosition = false;
    ImGui::BeginDisabled(draft_.againstEngine && draft_.openingMode != 0);
    ImGui::Checkbox("Custom position (FEN)", &draft_.customPosition);
    ImGui::EndDisabled();
    if (draft_.customPosition) {
        const float pasteWidth = 70.0f;
        ImGui::SetNextItemWidth(-(pasteWidth + ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::InputTextWithHint("##fen", "Paste a FEN string", draft_.fen.data(), draft_.fen.size())) draft_.error.clear();
        ImGui::SameLine();
        if (ImGui::Button("Paste", ImVec2(pasteWidth, 0.0f))) {
            if (const char* clipboard = ImGui::GetClipboardText()) {
                std::string text(clipboard);
                // Keep the first line only.
                text = text.substr(0, text.find_first_of("\r\n"));
                copyToBuffer(draft_.fen, text);
                draft_.error.clear();
            }
        }
    }
    if (!draft_.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        ImGui::TextWrapped("%s", draft_.error.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Start game", ImVec2(buttonWidth, 0.0f))) {
        const std::string fen = draft_.customPosition ? std::string(draft_.fen.data()) : std::string(core::kStartFen);
        std::string error;
        const int openingMode = draft_.againstEngine ? draft_.openingMode : 0;
        const bool hasLines = openingBook_ != nullptr && draft_.openingIndex >= 0 &&
                              draft_.openingIndex < static_cast<int>(openingBook_->lines().size());
        std::size_t repertoire = draft_.repertoires.size();
        for (std::size_t i = 0; i < draft_.repertoires.size(); ++i) {
            if (draft_.repertoires[i].first == draft_.repertoireId) repertoire = i;
        }
        if (!core::validateFen(fen, &error)) {
            draft_.error = "Invalid position: " + error + ".";
        } else if (openingMode == 1 && !hasLines) {
            draft_.error = "Choose an opening to practise.";
        } else if (openingMode == 2 && repertoire == draft_.repertoires.size()) {
            draft_.error = "Choose a repertoire to practise.";
        } else {
            settings_.playAgainstEngine = draft_.againstEngine;
            settings_.playAs = draft_.playAs;
            settings_.strengthMode = draft_.strengthMode;
            settings_.level = draft_.level;
            settings_.elo = draft_.elo;
            settings_.thinkTimeMs = draft_.thinkTimeMs;
            settings_.depthLimit = draft_.depthLimit;
            settings_.clockBaseSeconds = draft_.clockBaseSeconds;
            settings_.clockIncrementSeconds = draft_.clockIncrementSeconds;
            settings_.sanitize();
            settingsChanged_ = true;

            app::ColorChoice playAs = draft_.playAs;
            Book book;
            if (openingMode == 1) {
                book = bookFromLine(openingBook_->lines()[static_cast<std::size_t>(draft_.openingIndex)]);
            } else if (openingMode == 2) {
                book.name = draft_.repertoires[repertoire].second;
                book.tree = repertoires_->loadTree(draft_.repertoireId);
                book.fromRepertoire = true;
                // The user plays the repertoire's side.
                playAs = draft_.repertoireColors[repertoire] == Color::White ? app::ColorChoice::White : app::ColorChoice::Black;
            }
            startNewGame(draft_.againstEngine, playAs, fen);
            if (openingMode != 0) book_ = std::move(book);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void PlayScreen::drawOpeningChoice() {
    ImGui::SeparatorText("Opening");
    if (ImGui::RadioButton("Any", draft_.openingMode == 0)) draft_.openingMode = 0;
    ImGui::SameLine();
    ImGui::BeginDisabled(openingBook_ == nullptr || openingBook_->lines().empty());
    if (ImGui::RadioButton("Practise an opening", draft_.openingMode == 1)) draft_.openingMode = 1;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(draft_.repertoires.empty());
    if (ImGui::RadioButton("My repertoire", draft_.openingMode == 2)) draft_.openingMode = 2;
    ImGui::EndDisabled();
    if (draft_.repertoires.empty()) ImGui::SetItemTooltip("Build a repertoire in Openings first");

    if (draft_.openingMode == 1 && openingBook_ != nullptr) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##openingfilter", "Search, e.g. Italian, Najdorf or C50", draft_.openingFilter.data(),
                                 draft_.openingFilter.size());
        std::string filter(draft_.openingFilter.data());
        std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::vector<core::OpeningLine>& lines = openingBook_->lines();
        if (ImGui::BeginListBox("##openings", ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 6.5f))) {
            int shown = 0;
            for (std::size_t i = 0; i < lines.size() && shown < 300; ++i) {
                std::string label = lines[i].eco + "  " + lines[i].name;
                if (!filter.empty()) {
                    std::string lower = label;
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (lower.find(filter) == std::string::npos) continue;
                }
                ++shown;
                label += "##" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), draft_.openingIndex == static_cast<int>(i))) {
                    draft_.openingIndex = static_cast<int>(i);
                    draft_.error.clear();
                }
            }
            ImGui::EndListBox();
        }
        if (draft_.openingIndex >= 0 && draft_.openingIndex < static_cast<int>(lines.size())) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", lines[static_cast<std::size_t>(draft_.openingIndex)].pgn.c_str());
            ImGui::PopTextWrapPos();
        }
    } else if (draft_.openingMode == 2) {
        const char* preview = "Choose a repertoire";
        for (const auto& [id, name] : draft_.repertoires) {
            if (id == draft_.repertoireId) preview = name.c_str();
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##repertoire", preview)) {
            for (std::size_t i = 0; i < draft_.repertoires.size(); ++i) {
                const std::string label = draft_.repertoires[i].second + "  (" +
                                          (draft_.repertoireColors[i] == Color::White ? "White" : "Black") + ")##" +
                                          std::to_string(draft_.repertoires[i].first);
                if (ImGui::Selectable(label.c_str(), draft_.repertoires[i].first == draft_.repertoireId)) {
                    draft_.repertoireId = draft_.repertoires[i].first;
                    draft_.error.clear();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("You play the repertoire's side; the computer answers from it until it runs out.");
    }
}

void PlayScreen::drawResignDialog() {
    if (wantResignDialog_) {
        ImGui::OpenPopup("Resign");
        wantResignDialog_ = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Resign", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;

    const Color loser = againstEngine_ ? humanColor_ : game_.sideToMove();
    if (againstEngine_) {
        ImGui::TextUnformatted("Resign this game?");
    } else {
        ImGui::Text("%s resigns this game?", colorName(loser));
    }
    ImGui::Spacing();
    if (ImGui::Button("Resign", ImVec2(120.0f, 0.0f))) {
        cancelEngineMove();
        game_.resign(loser);
        notice_.clear();
        if (settings_.soundEnabled) assets_.play(app::SoundId::GameEnd, settings_.soundVolume);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void PlayScreen::handleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;

    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) undo();
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) redo();
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl) flipped_ = !flipped_;
    if (ImGui::IsKeyPressed(ImGuiKey_H, false) && !io.KeyCtrl) requestHint();
    if (ImGui::IsKeyPressed(ImGuiKey_N, false) && io.KeyCtrl) openNewGameDialog();
    if (ImGui::IsKeyPressed(ImGuiKey_B, false) && !io.KeyCtrl) {
        settings_.blindfold = !settings_.blindfold;
        settingsChanged_ = true;
    }
}

}  // namespace ui

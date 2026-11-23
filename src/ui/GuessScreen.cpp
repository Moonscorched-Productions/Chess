#include "ui/GuessScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <utility>

#include <imgui.h>

#include "app/Assets.h"
#include "app/GameAnalyzer.h"
#include "app/Paths.h"
#include "chess/Guess.h"
#include "chess/Pgn.h"
#include "app/Theme.h"
#include "ui/Common.h"

namespace ui {

namespace {

using core::Color;
using core::NodeId;

constexpr auto kOpponentDelay = std::chrono::milliseconds(700);
constexpr ImU32 kGuessArrowColor = IM_COL32(40, 120, 210, 170);
constexpr ImU32 kGameArrowColor = IM_COL32(96, 160, 48, 215);

std::string nameOr(const core::GameRecord& record, const char* tag, const char* fallback) {
    std::string name = record.tag(tag);
    if (name.empty() || name == "?") name = fallback;
    return name;
}

std::string yearOf(const core::GameRecord& record) {
    const std::string date = record.tag("Date");
    return date.size() >= 4 && date.substr(0, 4) != "????" ? date.substr(0, 4) : std::string();
}

}  // namespace

GuessScreen::GuessScreen(app::Settings& settings, app::Assets& assets, storage::GameStore* games,
                         const std::filesystem::path& classicsFile)
    : settings_(settings), assets_(assets), games_(games) {
    position_.setEnforceDraws(false);
    std::ifstream input(classicsFile, std::ios::binary);
    if (!input) return;
    core::PgnSplitter splitter(input);
    std::string text;
    while (splitter.next(text)) {
        if (std::optional<core::GameRecord> record = core::parsePgn(text); record && record->tree.moveCount() > 0) {
            classics_.push_back(std::move(*record));
        }
    }
}

// --- Choosing a game -------------------------------------------------------------

void GuessScreen::reloadList() {
    listDirty_ = false;
    choices_.clear();
    for (std::size_t i = 0; i < classics_.size(); ++i) {
        const core::GameRecord& record = classics_[i];
        Choice choice;
        choice.title = nameOr(record, "White", "White") + "  -  " + nameOr(record, "Black", "Black");
        choice.detail = yearOf(record);
        if (const std::string note = record.tag("Annotator"); !note.empty()) {
            choice.detail += (choice.detail.empty() ? "" : "  \xC2\xB7  ") + note;
        }
        choice.classic = i;
        choices_.push_back(std::move(choice));
    }
    if (games_ != nullptr) {
        storage::GameFilter filter;
        filter.limit = 300;
        for (const storage::GameSummary& game : games_->list(filter)) {
            if (game.moves < 10) continue;  // too short to be worth guessing
            Choice choice;
            choice.title = (game.white.empty() ? "?" : game.white) + "  -  " + (game.black.empty() ? "?" : game.black);
            choice.detail = game.date + "  \xC2\xB7  " + game.result + "  \xC2\xB7  " + std::to_string(game.moves) + " moves";
            if (!game.event.empty()) choice.detail += "  \xC2\xB7  " + game.event;
            choice.libraryId = game.id;
            choices_.push_back(std::move(choice));
        }
    }
    if (selected_ >= static_cast<int>(choices_.size())) selected_ = -1;
}

void GuessScreen::startClassic(std::size_t index, Color side) {
    if (index >= classics_.size()) return;
    const core::GameRecord& record = classics_[index];
    start(record, side, nameOr(record, "White", "White") + "  -  " + nameOr(record, "Black", "Black"));
}

void GuessScreen::start(core::GameRecord record, Color side, std::string title) {
    record_ = std::move(record);
    title_ = std::move(title);
    side_ = side;
    line_ = record_.tree.mainLine();
    index_ = 0;
    if (!position_.reset(record_.tree.startFen())) position_.reset();
    attempts_.clear();
    points_ = 0;
    feedback_.clear();
    lastGuess_.reset();
    pendingGuess_.reset();
    jobs_.clear();
    results_.clear();
    searchId_ = 0;
    opponentAt_ = {};
    board_.clearInteraction();
    phase_ = line_.empty() ? Phase::Finished : Phase::Guessing;

    // Start the engine now, so the first verdict does not wait for it.
    engineError_.clear();
    using State = engine::UciEngine::State;
    if (engine_.state() == State::Stopped || engine_.state() == State::Failed) {
        const std::filesystem::path path = app::resolveEnginePath(settings_.enginePath);
        if (path.empty()) {
            engineError_ = "No chess engine found: only the move from the game earns points.";
        } else if (!engine_.start(path, &engineError_) && engineError_.empty()) {
            engineError_ = "The engine could not be started.";
        }
    }
}

// --- Guessing --------------------------------------------------------------------

bool GuessScreen::guessersTurn() const {
    return phase_ == Phase::Guessing && index_ < line_.size() && position_.sideToMove() == side_;
}

int GuessScreen::maxPoints() const { return static_cast<int>(attempts_.size()) * core::kGuessMaxPoints; }

void GuessScreen::advance() {
    if (index_ >= line_.size()) return;
    const core::MoveRecord& played = record_.tree.node(line_[index_]).record;
    if (!position_.makeUciMove(played.uci)) {
        // The stored game holds an illegal move; nothing more to guess.
        phase_ = Phase::Finished;
        return;
    }
    ++index_;
    board_.clearInteraction();
    if (settings_.soundEnabled) {
        const app::SoundId id = position_.isOver()         ? app::SoundId::GameEnd
                                : !played.captured.empty() ? app::SoundId::Capture
                                                           : app::SoundId::Move;
        assets_.play(id, settings_.soundVolume);
    }
    if (index_ >= line_.size()) phase_ = Phase::Finished;
}

void GuessScreen::onGuess(core::Move move) {
    if (!guessersTurn()) return;
    const core::MoveRecord& played = record_.tree.node(line_[index_]).record;
    const std::string label =
        std::to_string(record_.tree.moveNumber(line_[index_])) + (side_ == Color::White ? ". " : "... ") + played.san;

    core::Game scratch;
    scratch.setEnforceDraws(false);
    scratch.reset(position_.fen());
    if (!scratch.makeMove(move)) return;
    const std::string guessSan = scratch.history().back().san;
    const std::string guessUci = scratch.history().back().uci;

    if (guessUci == played.uci) {
        attempts_.push_back(Attempt{label, guessSan, played.san, core::kGuessMaxPoints, true});
        points_ += core::kGuessMaxPoints;
        feedback_ = "Found it: " + played.san + " is the move from the game.  +" + std::to_string(core::kGuessMaxPoints);
        lastGuess_.reset();
        advance();
        return;
    }

    // Ask the engine about both moves, from the position after each.
    pendingGuess_ = move;
    pendingGuessSan_ = guessSan;
    std::string before = record_.tree.uciMoves(index_ == 0 ? core::kRootNode : line_[index_ - 1]);
    if (!before.empty()) before += ' ';
    jobs_ = {before + guessUci, before + played.uci};
    results_.assign(jobs_.size(), std::nullopt);
    job_ = 0;
    searchId_ = 0;
    phase_ = Phase::Judging;
    feedback_.clear();
    if (!engineError_.empty()) finishJudging();  // no engine: no verdict
}

void GuessScreen::startNextSearch() {
    engine::SearchLimits limits;
    limits.moveTimeMs = std::clamp(settings_.analysisMoveTimeMs, 300, 1000);
    limits.depth = std::max(settings_.analysisDepth, 12);
    searchDepth_ = 0;
    searchId_ = engine_.search(record_.tree.startFen(), jobs_[job_], limits);
}

void GuessScreen::finishJudging() {
    const core::MoveRecord& played = record_.tree.node(line_[index_]).record;
    const std::string label =
        std::to_string(record_.tree.moveNumber(line_[index_])) + (side_ == Color::White ? ". " : "... ") + played.san;
    Attempt attempt{label, pendingGuessSan_, played.san, 0, false};
    if (results_.size() == 2 && results_[0] && results_[1]) {
        // Expected scores from the guesser's side.
        const auto score = [&](const core::Eval& eval) {
            const double white = eval.whiteWinPercent();
            return side_ == Color::White ? white : 100.0 - white;
        };
        const double guessScore = score(*results_[0]);
        const double gameScore = score(*results_[1]);
        attempt.points = core::guessPoints(false, guessScore, gameScore);
        attempt.judged = true;
        char text[256];
        std::snprintf(text, sizeof(text), "You chose %s; the game went %s.  +%d  (your move keeps %.0f%%, the game move %.0f%%)",
                      pendingGuessSan_.c_str(), played.san.c_str(), attempt.points, guessScore, gameScore);
        feedback_ = text;
        if (attempt.points == core::kGuessMaxPoints) feedback_ += "  The engine likes yours just as much.";
    } else {
        feedback_ = "You chose " + pendingGuessSan_ + "; the game went " + played.san + ".";
    }
    points_ += attempt.points;
    attempts_.push_back(std::move(attempt));
    lastGuess_ = pendingGuess_;
    pendingGuess_.reset();
    jobs_.clear();
    searchId_ = 0;
    phase_ = Phase::Guessing;
    advance();
}

void GuessScreen::update() {
    for (const engine::EngineEvent& event : engine_.poll()) {
        switch (event.type) {
            case engine::EngineEvent::Type::Ready:
                app::configureForAnalysis(engine_, settings_);
                if (engine_.option("MultiPV")) engine_.setOption("MultiPV", "1");
                break;
            case engine::EngineEvent::Type::Info: {
                const engine::SearchInfo& info = event.info;
                if (event.searchId != searchId_ || searchId_ == 0 || info.isBound || info.multiPv != 1) break;
                if ((!info.scoreCp && !info.mateIn) || info.depth < searchDepth_) break;
                searchDepth_ = info.depth;
                // After either move it is the other side's turn.
                results_[job_] = app::evalFromInfo(info, side_ == Color::Black);
                break;
            }
            case engine::EngineEvent::Type::BestMove:
                if (event.searchId == searchId_ && searchId_ != 0) {
                    searchId_ = 0;
                    if (++job_ >= jobs_.size()) finishJudging();
                }
                break;
            case engine::EngineEvent::Type::Terminated:
                engineError_ = event.message.empty() ? "The engine stopped: only the move from the game earns points."
                                                     : event.message;
                if (phase_ == Phase::Judging) finishJudging();
                break;
        }
    }
    if (phase_ == Phase::Judging && searchId_ == 0 && job_ < jobs_.size() &&
        engine_.state() == engine::UciEngine::State::Ready) {
        startNextSearch();
    }

    // The other side's moves come from the game, after a short pause.
    if (phase_ == Phase::Guessing && index_ < line_.size() && !guessersTurn()) {
        const Clock::time_point now = Clock::now();
        if (opponentAt_ == Clock::time_point{}) {
            opponentAt_ = now + kOpponentDelay;
        } else if (now >= opponentAt_) {
            opponentAt_ = {};
            advance();
        }
    }

    if (autoGuess_ && guessersTurn()) {
        const Clock::time_point now = Clock::now();
        if (autoGuessAt_ == Clock::time_point{}) {
            autoGuessAt_ = now + std::chrono::milliseconds(150);
        } else if (now >= autoGuessAt_) {
            autoGuessAt_ = {};
            const std::vector<core::Move> moves = position_.legalMoves();
            static std::mt19937 rng{7};
            if (!moves.empty()) onGuess(moves[std::uniform_int_distribution<std::size_t>(0, moves.size() - 1)(rng)]);
        }
    }
}

app::PresenceText GuessScreen::presence() const {
    app::PresenceText text;
    text.details = "Guessing the moves";
    if (phase_ != Phase::Choosing) text.state = title_;
    return text;
}

// --- Drawing ---------------------------------------------------------------------

void GuessScreen::draw() {
    if (phase_ == Phase::Choosing) {
        drawChooser();
    } else {
        drawGame();
    }
}

void GuessScreen::drawChooser() {
    if (listDirty_) reloadList();
    ImGui::PushFont(app::titleFont());
    ImGui::TextUnformatted("Guess the Move");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("Play through a game from one side and find each move.");
    ImGui::Spacing();

    ImGui::TextDisabled("The move from the game earns %d points. Any other move earns up to %d, by how close the "
                        "engine rates it.",
                        core::kGuessMaxPoints, core::kGuessMaxPoints);
    ImGui::Spacing();

    bool startRequested = false;
    const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##guess-card", ImVec2(0.0f, std::max(ImGui::GetContentRegionAvail().y - footer, 120.0f)),
                      ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    if (ImGui::BeginTable("##guess-games", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        tableHeaders();
        bool libraryHeader = false;
        for (std::size_t i = 0; i < choices_.size(); ++i) {
            const Choice& choice = choices_[i];
            if (i == 0 && !choice.libraryId) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(kAccentColor, "Classics");
            }
            if (choice.libraryId && !libraryHeader) {
                libraryHeader = true;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(kAccentColor, "From your library");
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(i));
            const RowClick click = tableRow(choice.title.c_str(), selected_ == static_cast<int>(i));
            if (click.clicked) selected_ = static_cast<int>(i);
            const bool doubleClicked = click.doubleClicked;
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", choice.detail.c_str());
            if (doubleClicked) {
                selected_ = static_cast<int>(i);
                startRequested = true;
            }
        }
        if (choices_.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("No games yet. Import master games in the Game Library.");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Guess the moves of");
    ImGui::SameLine();
    ImGui::RadioButton("the winner", &sideChoice_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("White", &sideChoice_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Black", &sideChoice_, 2);
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::BeginDisabled(selected_ < 0);
    if ((ImGui::Button("Start", ImVec2(120.0f, 0.0f)) || startRequested) && selected_ >= 0) {
        const Choice& choice = choices_[static_cast<std::size_t>(selected_)];
        std::optional<core::GameRecord> record;
        if (choice.libraryId) {
            if (games_ != nullptr) {
                if (const std::optional<std::string> pgn = games_->pgn(*choice.libraryId)) record = core::parsePgn(*pgn);
            }
        } else {
            record = classics_[choice.classic];
        }
        if (record) {
            Color side = Color::White;
            if (sideChoice_ == 2 || (sideChoice_ == 0 && record->tag("Result") == "0-1")) side = Color::Black;
            start(std::move(*record), side, choice.title);
        }
    }
    ImGui::EndDisabled();
}

void GuessScreen::drawGame() {
    const BoardLayout layout = boardLayout(0.34f, 300.0f, 460.0f);
    const float panelWidth = layout.panelWidth;
    const float boardSize = layout.boardSize;
    ImGui::SetCursorPosX(layout.boardX);

    BoardOptions options = boardOptionsFrom(settings_);
    options.flipped = side_ == Color::Black;
    options.interactive = guessersTurn();
    if (lastGuess_ && phase_ != Phase::Judging) {
        options.arrows.push_back(BoardArrow{lastGuess_->from, lastGuess_->to, kGuessArrowColor});
        if (const std::optional<core::Move> game = position_.lastMove()) {
            options.arrows.push_back(BoardArrow{game->from, game->to, kGameArrowColor});
        }
    }
    if (phase_ == Phase::Judging && pendingGuess_) {
        options.arrows.push_back(BoardArrow{pendingGuess_->from, pendingGuess_->to, kGuessArrowColor});
    }

    const std::optional<core::Move> move = board_.draw(position_, assets_, options, boardSize);
    ImGui::SameLine(layout.panelX);
    drawPanel(panelWidth, boardSize);
    if (move) onGuess(*move);
}

void GuessScreen::drawPanel(float width, float height) {
    ImGui::BeginChild("##guess-panel", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(title_.c_str());
    ImGui::TextDisabled("You guess %s's moves", side_ == Color::White ? "White" : "Black");
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    if (phase_ == Phase::Finished) {
        ImGui::TextColored(kAccentColor, "Game over");
    } else if (phase_ == Phase::Judging) {
        ImGui::TextUnformatted(("The engine compares your move" + std::string(static_cast<std::size_t>(ImGui::GetTime() * 3.0) % 4, '.')).c_str());
    } else if (guessersTurn()) {
        ImGui::TextColored(kAccentColor, "Your guess: what was played here?");
    } else {
        ImGui::TextDisabled("The game continues...");
    }

    const int max = maxPoints();
    const int found = static_cast<int>(std::count_if(attempts_.begin(), attempts_.end(), [](const Attempt& a) {
        return a.guessSan == a.gameSan;
    }));
    ImGui::Text("%d / %d points", points_, max);
    if (max > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%d%%  \xC2\xB7  %d of %zu moves found)", points_ * 100 / max, found, attempts_.size());
    }
    if (!feedback_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", feedback_.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!engineError_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kErrorColor, "%s", engineError_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();

    const float buttons = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##attempts", ImVec2(0.0f, std::max(ImGui::GetContentRegionAvail().y - buttons, 60.0f)),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (ImGui::BeginTable("##attempt-table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Game move");
        ImGui::TableSetupColumn("Your guess");
        ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        tableHeaders();
        for (const Attempt& attempt : attempts_) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(attempt.label.c_str());
            ImGui::TableSetColumnIndex(1);
            if (attempt.guessSan == attempt.gameSan) {
                ImGui::TextColored(kAccentColor, "%s", attempt.guessSan.c_str());
            } else {
                ImGui::TextUnformatted(attempt.guessSan.c_str());
            }
            ImGui::TableSetColumnIndex(2);
            if (attempt.judged) {
                ImGui::Text("%d", attempt.points);
            } else {
                ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }
    if (attempts_.empty()) ImGui::TextDisabled("Your guesses appear here.");
    ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!guessersTurn());
    if (ImGui::Button("Show the move", ImVec2(half, 0.0f))) {
        const core::MoveRecord& played = record_.tree.node(line_[index_]).record;
        attempts_.push_back(Attempt{std::to_string(record_.tree.moveNumber(line_[index_])) +
                                        (side_ == Color::White ? ". " : "... ") + played.san,
                                    "-", played.san, 0, true});
        feedback_ = "The game went " + played.san + ".";
        lastGuess_.reset();
        advance();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Give up on this move (no points)");
    ImGui::SameLine();
    if (ImGui::Button(phase_ == Phase::Finished ? "Choose another game" : "Stop", ImVec2(half, 0.0f))) {
        engine_.stopSearch();
        searchId_ = 0;
        phase_ = Phase::Choosing;
    }
    ImGui::EndChild();
}

}  // namespace ui

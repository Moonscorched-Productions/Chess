#include "ui/ReviewScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include <imgui.h>

#include "app/Assets.h"
#include "app/Theme.h"
#include "chess/Explain.h"
#include "chess/Pgn.h"
#include "chess/Position.h"
#include "chess/PuzzleBuilder.h"
#include "chess/Statistics.h"
#include "ui/Common.h"
#include "ui/EvalGraph.h"
#include "ui/QualityBadge.h"

namespace ui {

namespace {

using core::Color;
using core::kNoNode;
using core::kRootNode;
using core::MoveQuality;
using core::NodeId;

constexpr float kGraphHeight = 96.0f;
constexpr float kEvalBarWidth = 20.0f;
constexpr int kListLimit = 500;
constexpr ImU32 kBestArrowColor = IM_COL32(96, 160, 48, 215);

constexpr MoveQuality kQualities[] = {MoveQuality::Best,       MoveQuality::Excellent, MoveQuality::Good,
                                      MoveQuality::Inaccuracy, MoveQuality::Mistake,   MoveQuality::Blunder};

bool isError(MoveQuality quality) {
    return quality == MoveQuality::Inaccuracy || quality == MoveQuality::Mistake || quality == MoveQuality::Blunder;
}

// Draws a badge at the start of the current line and moves the cursor past it.
void badgeInline(MoveQuality quality) {
    const float height = ImGui::GetTextLineHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    drawQualityBadge(ImGui::GetWindowDrawList(), ImVec2(pos.x + height * 0.5f, pos.y + height * 0.5f), height * 0.5f, quality);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + height + 6.0f);
}

std::string playerName(const core::GameRecord& record, const char* tag, const char* fallback) {
    std::string name = record.tag(tag);
    if (name.empty() || name == "?") name = fallback;
    return name;
}

}  // namespace

ReviewScreen::ReviewScreen(app::Settings& settings, app::Assets& assets, storage::GameStore* games,
                           storage::ReviewStore* reviews, const core::OpeningBook* book)
    : settings_(settings), assets_(assets), games_(games), reviews_(reviews), book_(book), analyzer_(settings), generator_(settings) {
    position_.setEnforceDraws(false);
    treeView_.quality = [this](NodeId id) { return qualityOf(id); };
}

// --- Review work ---------------------------------------------------------------

void ReviewScreen::load(core::GameRecord record, std::optional<storage::GameId> libraryId) {
    analyzer_.stop();
    generator_.stop();
    record_ = std::move(record);
    libraryId_ = libraryId;
    review_ = {};
    notice_.clear();
    lastDone_ = 0;

    opening_.clear();
    openingName_.clear();
    core::GameTree& tree = record_->tree;
    if (book_ != nullptr) {
        if (const core::OpeningName* opening = book_->identify(tree, tree.lineEnd(kRootNode))) {
            opening_ = opening->eco + "  " + opening->name;
            openingName_ = opening->name;
        }
    }
    // Show the board from the user's side when the game names them as Black.
    flipped_ = core::matchesPlayer(record_->tag("Black"), settings_.playerNames) &&
               !core::matchesPlayer(record_->tag("White"), settings_.playerNames);
    goTo(kRootNode, false);

    if (tree.moveCount() == 0) {
        notice_ = "This game has no moves to review.";
        return;
    }
    if (analyzer_.start(tree)) return;
    if (analyzer_.error().empty()) {
        finishReview();  // every position was analysed before
    } else {
        notice_ = analyzer_.error();
        pending_.clear();
    }
}

void ReviewScreen::reviewFromLibrary(storage::GameId id) {
    if (games_ == nullptr) return;
    const std::optional<std::string> text = games_->pgn(id);
    std::string error;
    std::optional<core::GameRecord> record = text ? core::parsePgn(*text, &error) : std::nullopt;
    if (!record) {
        listNotice_ = "Could not open the game" + (error.empty() ? std::string(".") : ": " + error + ".");
        return;
    }
    listNotice_.clear();
    load(std::move(*record), id);
}

void ReviewScreen::reviewAll() {
    if (reviews_ == nullptr) return;
    pending_ = reviews_->unreviewed();
    batchActive_ = !pending_.empty();
    batchReviewed_ = 0;
    if (!batchActive_) listNotice_ = "Every game in the library has been reviewed.";
}

void ReviewScreen::finishReview() {
    if (!record_) return;
    review_ = core::reviewGame(record_->tree);
    refreshCurrent();
    if (review_.judgedMoves == 0) return;

    if (!libraryId_) {
        notice_ = "This game is not in the Game Library, so it does not count in the statistics.";
        return;
    }
    if (games_ == nullptr || reviews_ == nullptr) return;
    // The evaluations go back into the stored game, so opening it again needs no engine.
    games_->update(*libraryId_, *record_);
    if (reviews_->save(*libraryId_, review_, openingName_)) {
        reviewsChanged_ = true;
        listDirty_ = true;
        if (batchActive_) ++batchReviewed_;
    } else {
        notice_ = "The review could not be stored.";
    }
}

void ReviewScreen::closeGame() {
    analyzer_.stop();
    generator_.stop();
    pending_.clear();
    batchActive_ = false;
    record_.reset();
    libraryId_.reset();
    listDirty_ = true;
}

void ReviewScreen::update() {
    analyzer_.update();
    if (analyzer_.takeFinished()) {
        finishReview();
    } else if (record_ && !analyzer_.running() && !analyzer_.error().empty() && notice_.empty()) {
        notice_ = analyzer_.error();
        pending_.clear();
    } else if (record_ && analyzer_.running() && analyzer_.done() != lastDone_) {
        // Keep the summary growing while the engine works through the game.
        lastDone_ = analyzer_.done();
        review_ = core::reviewGame(record_->tree);
        refreshCurrent();
    }

    // Puzzle generation for the game on screen.
    generator_.update();
    if (generator_.takeFinished()) {
        generated_ = generator_.takePuzzles();
    } else if (!generator_.running() && !generator_.error().empty() && notice_.empty()) {
        notice_ = generator_.error();
    }
    if (wantPuzzles_ && record_ && !analyzer_.running() && review_.complete()) {
        wantPuzzles_ = false;
        startPuzzles();
    }

    if (!batchActive_ || analyzer_.running()) return;
    if (!pending_.empty()) {
        const storage::GameId next = pending_.front();
        pending_.erase(pending_.begin());
        reviewFromLibrary(next);
        return;
    }
    // The last game of the batch is done: back to the list.
    batchActive_ = false;
    const int reviewed = batchReviewed_;
    closeGame();
    listNotice_ = "Reviewed " + std::to_string(reviewed) + (reviewed == 1 ? " game." : " games.");
}

void ReviewScreen::startPuzzles() {
    if (!record_ || generator_.running()) return;
    const std::vector<NodeId> candidates = core::puzzleCandidates(record_->tree, review_);
    if (candidates.empty()) {
        notice_ = "No moment in this game where a mistake could be punished, so there is nothing to make a puzzle from.";
        return;
    }
    if (generator_.start(record_->tree, candidates)) {
        notice_ = "Looking for puzzles in this game...";
    } else {
        notice_ = generator_.error();
    }
}

std::optional<std::vector<core::Puzzle>> ReviewScreen::takeGeneratedPuzzles() { return std::exchange(generated_, std::nullopt); }

void ReviewScreen::onPuzzlesStored(int found, int added) {
    if (found == 0) {
        notice_ = "No position in this game has a single clear winning move, so no puzzle was made.";
    } else if (added == 0) {
        notice_ = "The puzzles from this game are already in your collection.";
    } else {
        notice_ = "Added " + std::to_string(added) + (added == 1 ? " puzzle" : " puzzles") +
                  " from this game. In Puzzles, pick the theme \"From my games\".";
    }
}

std::optional<ReviewScreen::AnalysisRequest> ReviewScreen::takeAnalysisRequest() {
    return std::exchange(analysisRequest_, std::nullopt);
}

bool ReviewScreen::consumeReviewsChanged() { return std::exchange(reviewsChanged_, false); }

app::PresenceText ReviewScreen::presence() const {
    app::PresenceText text;
    if (!record_) {
        text.details = "Choosing a game to review";
        return text;
    }
    text.details = "Reviewing a game";
    const std::string white = record_->tag("White");
    const std::string black = record_->tag("Black");
    if (!white.empty() && !black.empty() && white != "?" && black != "?") text.state = white + " vs " + black;
    return text;
}

// --- Navigation ----------------------------------------------------------------

core::MoveQuality ReviewScreen::qualityOf(NodeId id) const {
    if (!record_ || !record_->tree.contains(id) || id == kRootNode) return MoveQuality::None;
    return core::classifyMove(record_->tree, id);
}

std::string ReviewScreen::moveLabel(NodeId id) const {
    const core::GameTree& tree = record_->tree;
    return std::to_string(tree.moveNumber(id)) + (tree.mover(id) == Color::White ? ". " : "... ") + tree.node(id).record.san;
}

void ReviewScreen::goTo(NodeId id, bool sound) {
    if (!record_) return;
    if (!record_->tree.contains(id)) id = kRootNode;
    const bool moved = id != current_;
    current_ = id;
    if (!core::loadPosition(record_->tree, current_, position_)) {
        current_ = kRootNode;
        core::loadPosition(record_->tree, current_, position_);
    }
    board_.clearInteraction();
    board_.clearAnnotations();
    treeView_.scrollToCurrent();
    refreshCurrent();

    // Like a board being replayed: the sound of the move that led to the new position.
    if (sound && moved && settings_.soundEnabled && current_ != kRootNode) {
        const core::MoveRecord& record = record_->tree.node(current_).record;
        const app::SoundId soundId = position_.isOver()          ? app::SoundId::GameEnd
                                     : !record.captured.empty() ? app::SoundId::Capture
                                                                : app::SoundId::Move;
        assets_.play(soundId, settings_.soundVolume);
    }
}

void ReviewScreen::goToPly(int ply) {
    if (!record_) return;
    const std::vector<NodeId> line = record_->tree.mainLine();
    if (ply <= 0 || line.empty()) {
        goTo(kRootNode);
    } else {
        goTo(line[std::min(static_cast<std::size_t>(ply), line.size()) - 1]);
    }
}

// Works out the move the engine preferred over the current one, for the arrow and the text.
void ReviewScreen::refreshCurrent() {
    bestSan_.clear();
    bestMove_.reset();
    explanation_.clear();
    if (!record_ || current_ == kRootNode) return;
    const core::GameTree& tree = record_->tree;
    for (const std::string& sentence : core::explainMove(tree, current_, qualityOf(current_))) {
        if (!explanation_.empty()) explanation_ += ' ';
        explanation_ += sentence;
    }
    const core::TreeNode& before = tree.node(tree.node(current_).parent);
    if (before.bestMove.empty()) return;

    core::Game scratch;
    scratch.setEnforceDraws(false);
    if (!scratch.reset(before.fen) || !scratch.makeUciMove(before.bestMove)) return;
    bestSan_ = scratch.history().back().san;
    bestMove_ = scratch.history().back().move;
}

void ReviewScreen::stepMoment(int direction) {
    if (!record_ || review_.moments.empty()) return;
    const int currentPly = record_->tree.node(current_).ply;
    NodeId target = kNoNode;
    if (direction > 0) {
        for (const core::CriticalMoment& moment : review_.moments) {
            if (record_->tree.node(moment.node).ply > currentPly) {
                target = moment.node;
                break;
            }
        }
    } else {
        for (auto it = review_.moments.rbegin(); it != review_.moments.rend(); ++it) {
            if (record_->tree.node(it->node).ply < currentPly) {
                target = it->node;
                break;
            }
        }
    }
    if (target != kNoNode) goTo(target);
}

// --- Game list -----------------------------------------------------------------

void ReviewScreen::reloadList() {
    listDirty_ = false;
    list_.clear();
    accuracies_.clear();
    unreviewed_ = 0;
    if (games_ == nullptr) return;
    storage::GameFilter filter;
    filter.limit = kListLimit;
    list_ = games_->list(filter);
    if (reviews_ != nullptr) {
        accuracies_ = reviews_->accuracies();
        unreviewed_ = static_cast<int>(reviews_->unreviewed().size());
    }
}

void ReviewScreen::drawList() {
    ImGui::TextUnformatted("Game Review");
    ImGui::Spacing();
    if (games_ == nullptr) {
        ImGui::TextColored(kErrorColor, "The game database is not available.");
        return;
    }
    if (listDirty_) reloadList();

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Pick a game and the engine checks every move: accuracy for both players, each move "
                        "classified from best to blunder, and the moments that decided the game.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    ImGui::BeginDisabled(selected_ == 0);
    if (ImGui::Button("Review game")) reviewFromLibrary(selected_);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(unreviewed_ == 0);
    char label[64];
    std::snprintf(label, sizeof(label), "Review all new games (%d)###reviewall", unreviewed_);
    if (ImGui::Button(label)) reviewAll();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Review every library game that has no review yet, one after the other");

    if (!listNotice_.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", listNotice_.c_str());
    }
    ImGui::Spacing();

    if (list_.empty()) {
        ImGui::TextDisabled("The Game Library is empty. Finish a game in Play or import PGN files in the Game Library.");
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##review-games", 6, flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 96.0f);
    ImGui::TableSetupColumn("White", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Black", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Moves", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Accuracy", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableHeadersRow();

    storage::GameId toOpen = 0;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(list_.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const storage::GameSummary& game = list_[static_cast<std::size_t>(row)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(game.id));
            if (ImGui::Selectable(game.date.empty() ? "-" : game.date.c_str(), selected_ == game.id,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                selected_ = game.id;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) toOpen = game.id;
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(game.white.empty() ? "?" : game.white.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(game.black.empty() ? "?" : game.black.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(game.result == "1/2-1/2" ? "\xC2\xBD-\xC2\xBD" : game.result.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", game.moves);
            ImGui::TableSetColumnIndex(5);
            if (const auto found = accuracies_.find(game.id); found != accuracies_.end()) {
                ImGui::Text("%.1f  /  %.1f", found->second.white, found->second.black);
            } else {
                ImGui::TextDisabled("not reviewed");
            }
        }
    }
    ImGui::EndTable();
    if (toOpen != 0) reviewFromLibrary(toOpen);
}

// --- Game view -----------------------------------------------------------------

void ReviewScreen::draw() {
    if (!record_) {
        drawList();
    } else {
        drawGame();
    }
}

void ReviewScreen::drawGame() {
    handleShortcuts();
    const core::GameTree& tree = record_->tree;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float panelWidth = std::clamp(avail.x * 0.36f, 300.0f, 520.0f);
    float boardSize = std::min(avail.x - panelWidth - kEvalBarWidth - 2.0f * style.ItemSpacing.x,
                               avail.y - kGraphHeight - style.ItemSpacing.y);
    boardSize = std::floor(std::max(boardSize, 240.0f) / 8.0f) * 8.0f;
    const float leftWidth = kEvalBarWidth + style.ItemSpacing.x + boardSize;
    const float columnHeight = boardSize + style.ItemSpacing.y + kGraphHeight;

    const float used = leftWidth + style.ItemSpacing.x + panelWidth;
    if (avail.x > used) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((avail.x - used) * 0.5f));

    BoardOptions options = boardOptionsFrom(settings_);
    options.flipped = flipped_;
    options.interactive = false;
    options.showLegalMoves = false;
    if (const MoveQuality quality = qualityOf(current_); quality != MoveQuality::None) {
        const core::Move played = tree.node(current_).record.move;
        // A weak move is shown together with what the engine wanted instead.
        if (isError(quality)) {
            const ImU32 color = (qualityColor(quality) & ~IM_COL32_A_MASK) | (215u << IM_COL32_A_SHIFT);
            options.arrows.push_back(BoardArrow{played.from, played.to, color});
            if (bestMove_) options.arrows.push_back(BoardArrow{bestMove_->from, bestMove_->to, kBestArrowColor});
        }
        options.badges.push_back(BoardBadge{played.to, quality});
    }

    ImGui::BeginGroup();
    drawEvalBar(ImVec2(kEvalBarWidth, boardSize), tree.node(current_).eval, flipped_, settings_.animations);
    ImGui::SameLine();
    board_.draw(position_, assets_, options, boardSize);
    const NodeId clicked = drawEvalGraph(tree, current_, ImVec2(leftWidth, kGraphHeight),
                                         [this](NodeId id) { return qualityOf(id); }, "Waiting for the engine...");
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginChild("##review-panel", ImVec2(panelWidth, std::max(columnHeight, avail.y)),
                      ImGuiChildFlags_AlwaysUseWindowPadding);

    // Header: players, result, opening.
    const std::string result = record_->tag("Result");
    ImGui::Text("%s  -  %s", playerName(*record_, "White", "White").c_str(), playerName(*record_, "Black", "Black").c_str());
    if (!result.empty() && result != "*") {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", result.c_str());
    }
    if (!opening_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", opening_.c_str());
        ImGui::PopTextWrapPos();
    }

    if (analyzer_.running()) {
        const float fraction = analyzer_.total() > 0 ? static_cast<float>(analyzer_.done()) / static_cast<float>(analyzer_.total()) : 0.0f;
        char progress[64];
        std::snprintf(progress, sizeof(progress), "Checking move %zu of %zu", std::min(analyzer_.done() + 1, analyzer_.total()),
                      analyzer_.total());
        const float stopWidth = ImGui::CalcTextSize("Stop").x + 2.0f * style.FramePadding.x;
        ImGui::ProgressBar(fraction, ImVec2(ImGui::GetContentRegionAvail().x - stopWidth - style.ItemSpacing.x, 0.0f), progress);
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            analyzer_.stop();
            pending_.clear();
            batchActive_ = false;
            review_ = core::reviewGame(tree);
            notice_ = "Review stopped. The numbers cover the moves checked so far.";
        }
        if (batchActive_ && !pending_.empty()) {
            ImGui::TextDisabled("%zu more %s waiting", pending_.size(), pending_.size() == 1 ? "game" : "games");
        }
    }

    drawSummary();
    ImGui::Separator();
    drawExplanation();

    const float rowHeight = ImGui::GetFrameHeightWithSpacing();
    const float noticeHeight = notice_.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 2.0f;
    const float below = rowHeight * 2.0f + noticeHeight + style.ItemSpacing.y;
    const float tabsHeight = std::max(ImGui::GetContentRegionAvail().y - below, 90.0f);
    NodeId treeClicked = kNoNode;
    if (ImGui::BeginChild("##review-tabs", ImVec2(0.0f, tabsHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
        if (ImGui::BeginTabBar("##tabs")) {
            char label[48];
            std::snprintf(label, sizeof(label), "Key moments (%zu)###moments", review_.moments.size());
            if (ImGui::BeginTabItem(label)) {
                drawMoments(ImGui::GetContentRegionAvail().y);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Position")) {
                drawPositionNotes(ImGui::GetContentRegionAvail().y);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Moves")) {
                treeClicked = treeView_.draw(tree, current_, ImGui::GetContentRegionAvail().y, false).clicked;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();

    drawButtons();
    if (!notice_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", notice_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    // Navigation last: closing the game or moving on invalidates what was drawn above.
    if (!record_) return;
    if (clicked != kNoNode) goTo(clicked);
    if (treeClicked != kNoNode) goTo(treeClicked);
}

void ReviewScreen::drawSummary() {
    const auto accuracyCell = [&](const core::SideReview& side) {
        ImGui::PushFont(app::headlineFont());
        if (side.moves > 0) {
            ImGui::Text("%.1f", side.accuracy);
        } else {
            ImGui::TextDisabled("-");
        }
        ImGui::PopFont();
    };

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 2.0f));
    if (ImGui::BeginTable("##summary", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##what", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn("##white", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("##black", ImGuiTableColumnFlags_WidthStretch, 1.0f);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("White");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled("Black");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
        ImGui::TextUnformatted("Accuracy");
        ImGui::TableSetColumnIndex(1);
        accuracyCell(review_.white);
        ImGui::TableSetColumnIndex(2);
        accuracyCell(review_.black);

        for (const MoveQuality quality : kQualities) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            badgeInline(quality);
            ImGui::TextUnformatted(core::qualityName(quality));
            for (int column = 1; column <= 2; ++column) {
                ImGui::TableSetColumnIndex(column);
                const int count = (column == 1 ? review_.white : review_.black).count(quality);
                if (count > 0) {
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(qualityColor(quality)), "%d", count);
                } else {
                    ImGui::TextDisabled("0");
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

// Two lines about the move on the board: what it was and what was better.
void ReviewScreen::drawExplanation() {
    const core::GameTree& tree = record_->tree;
    const float height = ImGui::GetTextLineHeightWithSpacing() * 4.0f;
    ImGui::BeginChild("##explain", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PushTextWrapPos(0.0f);

    const MoveQuality quality = qualityOf(current_);
    if (current_ == kRootNode) {
        ImGui::TextDisabled("Step through the game with the arrow keys. Up and Down jump between the key moments.");
    } else if (quality == MoveQuality::None) {
        ImGui::TextUnformatted(moveLabel(current_).c_str());
        ImGui::TextDisabled("%s", analyzer_.running() ? "Not checked yet." : "This move was not checked.");
    } else {
        const core::TreeNode& node = tree.node(current_);
        const core::TreeNode& before = tree.node(node.parent);
        badgeInline(quality);
        ImGui::TextUnformatted(moveLabel(current_).c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(qualityColor(quality)), "%s", core::qualityName(quality));

        std::string text;
        if (isError(quality)) {
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer), "Gave up %.0f%% of the winning chances (%s to %s). ",
                          core::expectedScoreLoss(tree, current_).value_or(0.0), formatEval(*before.eval).c_str(),
                          formatEval(*node.eval).c_str());
            text = buffer;
        }
        text += explanation_;
        if (!text.empty()) ImGui::TextDisabled("%s", text.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

void ReviewScreen::drawMoments(float height) {
    ImGui::BeginChild("##moments", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (review_.moments.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", analyzer_.running()       ? "Mistakes and blunders are listed here as they are found."
                                  : review_.judgedMoves > 0 ? "No mistakes or blunders in this game."
                                                            : "Nothing to show yet.");
        ImGui::PopTextWrapPos();
    }
    NodeId clicked = kNoNode;
    for (const core::CriticalMoment& moment : review_.moments) {
        ImGui::PushID(moment.node);
        const ImVec2 start = ImGui::GetCursorPos();
        if (ImGui::Selectable("##moment", moment.node == current_, ImGuiSelectableFlags_None,
                              ImVec2(0.0f, ImGui::GetTextLineHeight()))) {
            clicked = moment.node;
        }
        ImGui::SetCursorPos(start);
        badgeInline(moment.quality);
        ImGui::TextUnformatted(moveLabel(moment.node).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", core::momentText(moment.kind));
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (clicked != kNoNode) goTo(clicked);
}

// What stands out in the position on the board, for each side.
void ReviewScreen::drawPositionNotes(float height) {
    ImGui::BeginChild("##position-notes", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImGui::PushTextWrapPos(0.0f);
    const std::string fen = position_.fen();
    for (const Color color : {Color::White, Color::Black}) {
        ImGui::TextUnformatted(color == Color::White ? "White" : "Black");
        const std::vector<std::string> notes = core::positionNotes(fen, color);
        if (notes.empty()) ImGui::TextDisabled("Nothing stands out.");
        for (const std::string& note : notes) ImGui::TextDisabled("%s", note.c_str());
        ImGui::Spacing();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

void ReviewScreen::drawButtons() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const core::GameTree& tree = record_->tree;
    const float full = ImGui::GetContentRegionAvail().x;
    const auto width = [&](int perRow) {
        return ImVec2((full - static_cast<float>(perRow - 1) * style.ItemSpacing.x) / static_cast<float>(perRow), 0.0f);
    };
    const bool atStart = current_ == kRootNode;
    const bool atEnd = tree.node(current_).children.empty();

    ImGui::BeginDisabled(atStart);
    if (ImGui::Button("|<", width(5))) goTo(kRootNode);
    ImGui::SetItemTooltip("Start of the game (Home)");
    ImGui::SameLine();
    if (ImGui::Button("<", width(5))) goTo(tree.node(current_).parent);
    ImGui::SetItemTooltip("Previous move (Left arrow)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(atEnd);
    if (ImGui::Button(">", width(5))) goTo(tree.node(current_).children.front());
    ImGui::SetItemTooltip("Next move (Right arrow)");
    ImGui::SameLine();
    if (ImGui::Button(">|", width(5))) goTo(tree.lineEnd(current_));
    ImGui::SetItemTooltip("End of the game (End)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Flip", width(5))) flipped_ = !flipped_;
    ImGui::SetItemTooltip("Flip the board (F)");

    ImGui::BeginDisabled(review_.moments.empty());
    if (ImGui::Button("< Key", width(5))) stepMoment(-1);
    ImGui::SetItemTooltip("Previous key moment (Up arrow)");
    ImGui::SameLine();
    if (ImGui::Button("Key >", width(5))) stepMoment(1);
    ImGui::SetItemTooltip("Next key moment (Down arrow)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(analyzer_.running() || generator_.running() || review_.judgedMoves == 0);
    if (ImGui::Button("Puzzles", width(5))) startPuzzles();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Make puzzles from the moments of this game where a mistake could be punished");
    ImGui::SameLine();
    if (ImGui::Button("Analysis", width(5))) analysisRequest_ = AnalysisRequest{*record_, libraryId_};
    ImGui::SetItemTooltip("Open this game in Analysis to try other moves");
    ImGui::SameLine();
    if (ImGui::Button("Games", width(5))) closeGame();
    ImGui::SetItemTooltip("Back to the list of games");
}

void ReviewScreen::handleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
    const core::GameTree& tree = record_->tree;
    const core::TreeNode& node = tree.node(current_);

    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && current_ != kRootNode) goTo(node.parent);
    else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !node.children.empty()) goTo(node.children.front());
    else if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) goTo(kRootNode);
    else if (ImGui::IsKeyPressed(ImGuiKey_End, false)) goTo(tree.lineEnd(current_));
    else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) stepMoment(-1);
    else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) stepMoment(1);
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl) flipped_ = !flipped_;
}

}  // namespace ui

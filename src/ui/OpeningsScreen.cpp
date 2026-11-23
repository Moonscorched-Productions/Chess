#include "ui/OpeningsScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>

#include <imgui.h>

#include "app/Assets.h"
#include "app/FileDialog.h"
#include "chess/Pgn.h"
#include "ui/Common.h"

namespace ui {

namespace {

using core::Color;
using core::kNoNode;
using core::kRootNode;
using core::NodeId;

constexpr auto kReplyDelay = std::chrono::milliseconds(450);

template <std::size_t N>
void copyToBuffer(std::array<char, N>& buffer, const std::string& text) {
    const std::size_t length = std::min(text.size(), N - 1);
    std::memcpy(buffer.data(), text.data(), length);
    buffer[length] = '\0';
}

std::string repertoireLabel(const storage::RepertoireInfo& info) {
    std::string label = info.folder.empty() ? info.name : info.folder + " / " + info.name;
    label += info.color == Color::White ? "  (White)" : "  (Black)";
    return label;
}

// Adds every move of `source` below `sourceNode` to `target` below `targetNode`.
void mergeTree(const core::GameTree& source, NodeId sourceNode, core::GameTree& target, NodeId targetNode) {
    core::Game game;
    game.setEnforceDraws(false);
    for (const NodeId child : source.node(sourceNode).children) {
        if (!core::loadPosition(target, targetNode, game)) return;
        const NodeId added = core::playMove(target, targetNode, game, source.node(child).record.move);
        if (added == kNoNode) continue;
        if (target.node(added).comment.empty() && !source.node(child).comment.empty()) {
            target.setComment(added, source.node(child).comment);
        }
        mergeTree(source, child, target, added);
    }
}

}  // namespace

OpeningsScreen::OpeningsScreen(app::Settings& settings, app::Assets& assets, storage::TrainingStore* store,
                               const core::OpeningBook* book)
    : settings_(settings), assets_(assets), store_(store), book_(book) {
    position_.setEnforceDraws(false);
    treeView_.emptyHint = "Play moves on the board to build this repertoire, or import a PGN file.";
    reloadList();
    if (!repertoires_.empty()) select(repertoires_.front().id);
}

std::optional<core::GameRecord> OpeningsScreen::takeAnalysisRequest() { return std::exchange(analysisRequest_, std::nullopt); }

app::PresenceText OpeningsScreen::presence() const {
    app::PresenceText text;
    text.details = training_ ? "Drilling an opening repertoire" : "Preparing openings";
    if (const storage::RepertoireInfo* info = selectedInfo()) text.state = info->name;
    return text;
}

// --- Repertoires -----------------------------------------------------------------

void OpeningsScreen::reloadList() {
    repertoires_ = store_ != nullptr ? store_->repertoires() : std::vector<storage::RepertoireInfo>{};
}

const storage::RepertoireInfo* OpeningsScreen::selectedInfo() const {
    for (const storage::RepertoireInfo& info : repertoires_)
        if (info.id == selected_) return &info;
    return nullptr;
}

void OpeningsScreen::select(storage::RepertoireId id) {
    stopTraining();
    selected_ = id;
    tree_ = store_ != nullptr && id != 0 ? store_->loadTree(id) : core::GameTree();
    cards_ = store_ != nullptr && id != 0 ? store_->cards(id) : std::unordered_map<std::string, core::CardState>{};
    const storage::RepertoireInfo* info = selectedInfo();
    flipped_ = info != nullptr && info->color == Color::Black;
    goTo(kRootNode);
}

void OpeningsScreen::saveTree() {
    if (store_ != nullptr && selected_ != 0) store_->saveTree(selected_, tree_);
}

OpeningsScreen::Counts OpeningsScreen::counts() const {
    Counts result;
    const storage::RepertoireInfo* info = selectedInfo();
    if (info == nullptr) return result;
    const int today = core::todayNumber();
    std::unordered_set<std::string> seen;
    for (const NodeId id : core::decisionNodes(tree_, info->color)) {
        const std::string key = core::positionKey(tree_.node(id).fen);
        if (!seen.insert(key).second) continue;  // a transposition counts once
        ++result.positions;
        const auto card = cards_.find(key);
        if (card == cards_.end() || card->second.isDue(today)) ++result.due;
        if (card != cards_.end() && card->second.streak > 0) ++result.learned;
    }
    return result;
}

void OpeningsScreen::importPgn() {
    const std::optional<std::filesystem::path> path = app::openPgnDialog();
    if (!path) return;
    std::ifstream input(*path, std::ios::binary);
    if (!input) {
        notice_ = "Could not open the file.";
        return;
    }
    // Every game of the file is merged in as lines of the repertoire.
    core::PgnSplitter splitter(input);
    std::string text;
    int merged = 0;
    const int before = tree_.moveCount();
    while (splitter.next(text)) {
        const std::optional<core::GameRecord> record = core::parsePgn(text);
        if (!record || record->tree.startFen() != tree_.startFen()) continue;
        mergeTree(record->tree, kRootNode, tree_, kRootNode);
        ++merged;
    }
    saveTree();
    goTo(kRootNode);
    notice_ = merged == 0 ? "No usable games found in the file."
                          : "Added " + std::to_string(tree_.moveCount() - before) + " moves from " + std::to_string(merged) +
                                (merged == 1 ? " game." : " games.");
}

void OpeningsScreen::exportPgn() {
    const storage::RepertoireInfo* info = selectedInfo();
    if (info == nullptr) return;
    const std::optional<std::filesystem::path> path = app::savePgnDialog("repertoire.pgn");
    if (!path) return;
    core::GameRecord record{{}, tree_};
    record.setTag("Event", info->name);
    record.setTag(info->color == Color::White ? "White" : "Black", "Repertoire");
    std::ofstream output(*path, std::ios::binary | std::ios::trunc);
    output << core::writePgn(record);
    notice_ = output.good() ? "Repertoire exported." : "Could not write the file.";
}

// --- Editing ---------------------------------------------------------------------

void OpeningsScreen::goTo(NodeId id) {
    if (!tree_.contains(id)) id = kRootNode;
    current_ = id;
    if (!core::loadPosition(tree_, current_, position_)) {
        current_ = kRootNode;
        core::loadPosition(tree_, current_, position_);
    }
    board_.clearInteraction();
    board_.clearAnnotations();
    treeView_.scrollToCurrent();
}

void OpeningsScreen::playMove(core::Move move) {
    if (selected_ == 0) return;
    const NodeId next = core::playMove(tree_, current_, position_, move);
    if (next == kNoNode) return;
    current_ = next;
    board_.clearInteraction();
    treeView_.scrollToCurrent();
    notice_.clear();
    saveTree();
    if (settings_.soundEnabled) {
        assets_.play(tree_.node(next).record.captured.empty() ? app::SoundId::Move : app::SoundId::Capture, settings_.soundVolume);
    }
}

void OpeningsScreen::removeNode(NodeId id) {
    if (!tree_.contains(id) || id == kRootNode) return;
    const std::vector<NodeId> path = tree_.pathTo(current_);
    const bool inside = std::find(path.begin(), path.end(), id) != path.end();
    const NodeId parent = tree_.node(id).parent;
    tree_.remove(id);
    saveTree();
    if (inside) goTo(parent);
}

// --- Training --------------------------------------------------------------------

void OpeningsScreen::startTraining(bool dueOnly) {
    const storage::RepertoireInfo* info = selectedInfo();
    if (info == nullptr) return;
    const int today = core::todayNumber();

    // A line is worth playing if it passes a position that is due (or any position, when practising all).
    queue_.clear();
    for (std::vector<NodeId>& line : core::allLines(tree_)) {
        bool wanted = false;
        NodeId position = kRootNode;
        for (const NodeId next : line) {
            if (core::sideToMoveAt(tree_, position) == info->color) {
                const auto card = cards_.find(core::positionKey(tree_.node(position).fen));
                if (!dueOnly || card == cards_.end() || card->second.isDue(today)) wanted = true;
            }
            position = next;
        }
        if (wanted) queue_.push_back(std::move(line));
    }
    if (queue_.empty()) {
        notice_ = tree_.moveCount() == 0 ? "This repertoire has no moves yet." : "Nothing is due today. Use \"Practise all\" to drill anyway.";
        return;
    }
    std::shuffle(queue_.begin(), queue_.end(), rng_);

    training_ = true;
    reviewed_.clear();
    sessionCorrect_ = 0;
    sessionWrong_ = 0;
    feedback_.clear();
    notice_.clear();
    nextLine();
}

void OpeningsScreen::stopTraining() {
    training_ = false;
    queue_.clear();
    line_.clear();
    lineIndex_ = 0;
    feedback_.clear();
    missedHere_ = false;
}

bool OpeningsScreen::nextLine() {
    const storage::RepertoireInfo* info = selectedInfo();
    while (!queue_.empty() && info != nullptr) {
        std::vector<NodeId> line = std::move(queue_.back());
        queue_.pop_back();
        // Skip lines whose questions were all asked already through another line.
        bool fresh = false;
        NodeId position = kRootNode;
        for (const NodeId next : line) {
            if (core::sideToMoveAt(tree_, position) == info->color &&
                reviewed_.count(core::positionKey(tree_.node(position).fen)) == 0) {
                fresh = true;
            }
            position = next;
        }
        if (!fresh) continue;

        line_ = std::move(line);
        lineIndex_ = 0;
        missedHere_ = false;
        goTo(kRootNode);
        replyAt_ = Clock::now() + kReplyDelay;
        return true;
    }
    line_.clear();
    lineIndex_ = 0;
    return false;
}

void OpeningsScreen::reviewCurrent(bool correct) {
    const std::string key = core::positionKey(tree_.node(current_).fen);
    if (!reviewed_.insert(key).second) return;  // graded once per session
    core::CardState& card = cards_[key];
    card = core::reviewCard(card, correct, core::todayNumber());
    if (store_ != nullptr) store_->saveCard(selected_, key, card);
    if (correct) {
        ++sessionCorrect_;
    } else {
        ++sessionWrong_;
    }
}

void OpeningsScreen::answer(core::Move move) {
    if (!training_ || lineIndex_ >= line_.size()) return;

    // Any move of the repertoire in this position is right, not only the one of this line.
    NodeId chosen = kNoNode;
    for (const NodeId child : tree_.node(current_).children) {
        if (tree_.node(child).record.move == move) chosen = child;
    }
    if (chosen == kNoNode) {
        if (!missedHere_) reviewCurrent(false);
        missedHere_ = true;
        feedback_ = "Not in your repertoire. The move here is " + tree_.node(line_[lineIndex_]).record.san + ".";
        board_.clearInteraction();
        return;
    }

    if (!missedHere_) reviewCurrent(true);
    if (chosen != line_[lineIndex_]) {
        // The user picked another branch: follow that one to its end instead.
        line_ = tree_.pathTo(tree_.lineEnd(chosen));
    }
    lineIndex_ = static_cast<std::size_t>(tree_.node(chosen).ply);
    missedHere_ = false;
    feedback_ = "Correct.";
    goTo(chosen);
    replyAt_ = Clock::now() + kReplyDelay;
    if (settings_.soundEnabled) assets_.play(app::SoundId::Move, settings_.soundVolume);
}

void OpeningsScreen::update() {
    if (wantTraining_) {
        wantTraining_ = false;
        startTraining(false);
    }
    if (!training_) return;
    const storage::RepertoireInfo* info = selectedInfo();
    if (info == nullptr) {
        stopTraining();
        return;
    }
    const Clock::time_point now = Clock::now();
    if (now < replyAt_) return;

    if (line_.empty()) return;  // session finished; the panel shows the summary
    if (lineIndex_ >= line_.size()) {
        nextLine();
        return;
    }

    const bool usersTurn = core::sideToMoveAt(tree_, current_) == info->color;
    const bool alreadyAsked = reviewed_.count(core::positionKey(tree_.node(current_).fen)) != 0 && !missedHere_;
    if (!usersTurn || alreadyAsked) {
        // The opponent's move, or a position answered earlier in this session: just play it.
        goTo(line_[lineIndex_++]);
        replyAt_ = now + kReplyDelay;
        if (settings_.soundEnabled) assets_.play(app::SoundId::Move, settings_.soundVolume);
    } else if (autoAnswer_) {
        if (autoAt_ == Clock::time_point{}) {
            autoAt_ = now + std::chrono::milliseconds(500);
        } else if (now >= autoAt_) {
            autoAt_ = {};
            answer(tree_.node(line_[lineIndex_]).record.move);
        }
    }
}

// --- Drawing ---------------------------------------------------------------------

void OpeningsScreen::draw() {
    if (store_ == nullptr) {
        ImGui::TextUnformatted("Openings");
        ImGui::TextColored(kErrorColor, "The training database is not available.");
        return;
    }

    // Tree edits requested from the context menu during the previous frame.
    if (pendingRemove_ != kNoNode) removeNode(std::exchange(pendingRemove_, kNoNode));
    if (pendingPromote_ != kNoNode) {
        tree_.promote(std::exchange(pendingPromote_, kNoNode));
        saveTree();
    }
    if (pendingMainLine_ != kNoNode) {
        tree_.makeMainLine(std::exchange(pendingMainLine_, kNoNode));
        saveTree();
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float panelWidth = std::clamp(avail.x * 0.36f, 320.0f, 480.0f);
    float boardSize = std::min(avail.x - panelWidth - style.ItemSpacing.x, avail.y);
    boardSize = std::floor(std::max(boardSize, 240.0f) / 8.0f) * 8.0f;
    const float used = boardSize + style.ItemSpacing.x + panelWidth;
    if (avail.x > used) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((avail.x - used) * 0.5f));

    const storage::RepertoireInfo* info = selectedInfo();
    const bool asking = training_ && info != nullptr && lineIndex_ < line_.size() &&
                        core::sideToMoveAt(tree_, current_) == info->color;

    BoardOptions options = boardOptionsFrom(settings_);
    options.flipped = flipped_;
    options.interactive = info != nullptr && (training_ ? asking : true);
    if (training_ && missedHere_ && lineIndex_ < line_.size()) {
        const core::Move expected = tree_.node(line_[lineIndex_]).record.move;
        options.arrows.push_back(BoardArrow{expected.from, expected.to, IM_COL32(40, 120, 210, 200)});
    }

    const std::optional<core::Move> move = board_.draw(position_, assets_, options, boardSize);
    ImGui::SameLine();
    drawSidePanel(panelWidth, std::max(boardSize, avail.y));
    drawRepertoireDialog();

    if (move) {
        if (training_) {
            answer(*move);
        } else {
            playMove(*move);
        }
    }
}

void OpeningsScreen::drawSidePanel(float width, float height) {
    ImGui::BeginChild("##openings-panel", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (training_) {
        drawTrainPanel();
    } else {
        drawEditPanel();
    }
    ImGui::EndChild();
}

void OpeningsScreen::drawEditPanel() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float full = ImGui::GetContentRegionAvail().x;
    const auto width = [&](int perRow) {
        return ImVec2((full - static_cast<float>(perRow - 1) * style.ItemSpacing.x) / static_cast<float>(perRow), 0.0f);
    };
    const storage::RepertoireInfo* info = selectedInfo();

    // --- Repertoire picker -------------------------------------------------------
    ImGui::SetNextItemWidth(-1.0f);
    const std::string preview = info != nullptr ? repertoireLabel(*info) : "No repertoire";
    if (ImGui::BeginCombo("##repertoire", preview.c_str())) {
        storage::RepertoireId chosen = 0;
        for (const storage::RepertoireInfo& item : repertoires_) {
            ImGui::PushID(static_cast<int>(item.id));
            if (ImGui::Selectable(repertoireLabel(item).c_str(), item.id == selected_)) chosen = item.id;
            ImGui::PopID();
        }
        ImGui::EndCombo();
        if (chosen != 0 && chosen != selected_) {
            select(chosen);
            info = selectedInfo();
        }
    }
    if (ImGui::Button("New...", width(3))) {
        dialogIsNew_ = true;
        dialogName_[0] = '\0';
        dialogFolder_[0] = '\0';
        dialogColor_ = 0;
        wantDialog_ = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(info == nullptr);
    if (ImGui::Button("Edit...", width(3)) && info != nullptr) {
        dialogIsNew_ = false;
        copyToBuffer(dialogName_, info->name);
        copyToBuffer(dialogFolder_, info->folder);
        dialogColor_ = info->color == Color::Black ? 1 : 0;
        wantDialog_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete", width(3))) wantDeleteDialog_ = true;
    ImGui::EndDisabled();

    if (info == nullptr) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("A repertoire is the set of opening lines you want to play with one colour. "
                            "Create one, add its moves on the board, then train it.");
        ImGui::PopTextWrapPos();
        return;
    }

    // --- Training entry ----------------------------------------------------------
    const Counts stats = counts();
    ImGui::Separator();
    ImGui::TextDisabled("%d positions  \xC2\xB7  %d learned  \xC2\xB7  %d due", stats.positions, stats.learned, stats.due);
    char trainLabel[48];
    std::snprintf(trainLabel, sizeof(trainLabel), "Train (%d due)###train", stats.due);
    ImGui::BeginDisabled(stats.positions == 0);
    if (ImGui::Button(trainLabel, width(2))) startTraining(true);
    ImGui::SetItemTooltip("Drill the positions that are new or scheduled for today");
    ImGui::SameLine();
    if (ImGui::Button("Practise all", width(2))) startTraining(false);
    ImGui::SetItemTooltip("Go through every line, whether it is due or not");
    ImGui::EndDisabled();
    ImGui::Separator();

    // --- Tree --------------------------------------------------------------------
    if (book_ != nullptr) {
        if (const core::OpeningName* opening = book_->identify(tree_, current_)) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s  %s", opening->eco.c_str(), opening->name.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    const float rowHeight = ImGui::GetFrameHeightWithSpacing();
    const float noticeHeight = notice_.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 2.0f;
    const float treeHeight = std::max(ImGui::GetContentRegionAvail().y - rowHeight * 2.0f - noticeHeight - style.ItemSpacing.y, 80.0f);
    const MoveTreeActions actions = treeView_.draw(tree_, current_, treeHeight);
    if (actions.clicked != kNoNode) goTo(actions.clicked);
    if (actions.remove != kNoNode) pendingRemove_ = actions.remove;
    if (actions.promote != kNoNode) pendingPromote_ = actions.promote;
    if (actions.mainLine != kNoNode) pendingMainLine_ = actions.mainLine;

    // --- Buttons -----------------------------------------------------------------
    const bool atStart = current_ == kRootNode;
    const bool atEnd = tree_.node(current_).children.empty();
    ImGui::BeginDisabled(atStart);
    if (ImGui::Button("|<", width(5))) goTo(kRootNode);
    ImGui::SameLine();
    if (ImGui::Button("<", width(5))) goTo(tree_.node(current_).parent);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(atEnd);
    if (ImGui::Button(">", width(5))) goTo(tree_.node(current_).children.front());
    ImGui::SameLine();
    if (ImGui::Button(">|", width(5))) goTo(tree_.lineEnd(current_));
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Flip", width(5))) flipped_ = !flipped_;

    if (ImGui::Button("Import PGN...", width(3))) importPgn();
    ImGui::SetItemTooltip("Add the lines of a PGN file to this repertoire");
    ImGui::SameLine();
    ImGui::BeginDisabled(tree_.moveCount() == 0);
    if (ImGui::Button("Export PGN...", width(3))) exportPgn();
    ImGui::SameLine();
    if (ImGui::Button("Analyse", width(3))) {
        core::GameRecord record{{}, tree_};
        record.setTag("Event", info->name);
        analysisRequest_ = std::move(record);
    }
    ImGui::SetItemTooltip("Open a copy of this repertoire in Analysis");
    ImGui::EndDisabled();

    if (!notice_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", notice_.c_str());
        ImGui::PopTextWrapPos();
    }

    // Keyboard navigation while editing.
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && !atStart) goTo(tree_.node(current_).parent);
        else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !atEnd) goTo(tree_.node(current_).children.front());
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl) flipped_ = !flipped_;
    }
}

void OpeningsScreen::drawTrainPanel() {
    const storage::RepertoireInfo* info = selectedInfo();
    ImGui::Text("Training  \xC2\xB7  %s", info != nullptr ? info->name.c_str() : "");
    ImGui::TextDisabled("%d correct  \xC2\xB7  %d missed  \xC2\xB7  %d lines to go", sessionCorrect_, sessionWrong_,
                        static_cast<int>(queue_.size()));
    ImGui::Separator();

    const bool finished = line_.empty();
    if (finished) {
        const int total = sessionCorrect_ + sessionWrong_;
        ImGui::TextColored(kAccentColor, "Session finished.");
        if (total > 0) {
            ImGui::Text("You knew %d of %d positions (%d%%).", sessionCorrect_, total, sessionCorrect_ * 100 / total);
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Positions you knew come back after a few days, the ones you missed come back sooner.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Done", ImVec2(-1.0f, 0.0f))) stopTraining();
        return;
    }

    const bool usersTurn = info != nullptr && lineIndex_ < line_.size() && core::sideToMoveAt(tree_, current_) == info->color;
    ImGui::PushTextWrapPos(0.0f);
    if (usersTurn) {
        ImGui::TextUnformatted("Play your repertoire move.");
    } else {
        ImGui::TextDisabled("The opponent is moving...");
    }
    if (!feedback_.empty()) {
        ImGui::TextColored(missedHere_ ? kErrorColor : kAccentColor, "%s", feedback_.c_str());
    }
    if (book_ != nullptr && current_ != kRootNode) {
        if (const core::OpeningName* opening = book_->identify(tree_, current_)) ImGui::TextDisabled("%s", opening->name.c_str());
    }
    ImGui::PopTextWrapPos();

    ImGui::Spacing();
    if (ImGui::Button("Stop training", ImVec2(-1.0f, 0.0f))) stopTraining();
}

void OpeningsScreen::drawRepertoireDialog() {
    if (wantDialog_) {
        ImGui::OpenPopup("Repertoire");
        wantDialog_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f));
    if (beginCenteredModal("Repertoire", ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted("Name");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##name", "e.g. Italian Game", dialogName_.data(), dialogName_.size());
        ImGui::TextUnformatted("You play");
        ImGui::RadioButton("White", &dialogColor_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Black", &dialogColor_, 1);
        ImGui::TextUnformatted("Folder (optional)");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##folder", "Groups repertoires in the list", dialogFolder_.data(), dialogFolder_.size());
        ImGui::Spacing();

        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::BeginDisabled(dialogName_[0] == '\0');
        if (ImGui::Button(dialogIsNew_ ? "Create" : "Save", ImVec2(half, 0.0f))) {
            const Color color = dialogColor_ == 1 ? Color::Black : Color::White;
            if (dialogIsNew_) {
                if (const std::optional<storage::RepertoireId> id = store_->createRepertoire(dialogName_.data(), color, dialogFolder_.data())) {
                    reloadList();
                    select(*id);
                }
            } else if (const storage::RepertoireInfo* info = selectedInfo()) {
                storage::RepertoireInfo updated = *info;
                updated.name = dialogName_.data();
                updated.folder = dialogFolder_.data();
                updated.color = color;
                store_->updateRepertoire(updated);
                reloadList();
                flipped_ = color == Color::Black;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(half, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (wantDeleteDialog_) {
        ImGui::OpenPopup("Delete repertoire");
        wantDeleteDialog_ = false;
    }
    if (beginCenteredModal("Delete repertoire", ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Delete this repertoire and its training progress? This cannot be undone.");
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
            store_->removeRepertoire(selected_);
            reloadList();
            select(repertoires_.empty() ? 0 : repertoires_.front().id);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace ui

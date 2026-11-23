#include "ui/AnalysisScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>

#include <imgui.h>

#include "app/Assets.h"
#include "app/FileDialog.h"
#include "app/GameAnalyzer.h"
#include "app/Paths.h"
#include "chess/Pgn.h"
#include "chess/Position.h"
#include "ui/Common.h"
#include "ui/EvalGraph.h"
#include "ui/QualityBadge.h"

namespace ui {

namespace {

using core::Color;
using core::kNoNode;
using core::kRootNode;
using core::NodeId;

constexpr float kGraphHeight = 96.0f;
constexpr float kEvalBarWidth = 20.0f;
constexpr int kPvMoves = 12;
constexpr ImU32 kEngineArrowColor = IM_COL32(0, 110, 190, 170);

void copyToBuffer(std::array<char, 512>& buffer, const std::string& text) {
    const std::size_t length = std::min(text.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), text.data(), length);
    buffer[length] = '\0';
}

std::string trimmed(std::string text) {
    const auto isSpace = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.back()))) text.pop_back();
    std::size_t start = 0;
    while (start < text.size() && isSpace(static_cast<unsigned char>(text[start]))) ++start;
    return text.substr(start);
}

// An engine line (UCI moves) played out from a position and written in SAN.
struct SanLine {
    std::string text;
    core::Move first;
    bool valid = false;
};

SanLine sanLine(const std::string& fen, const std::string& pv) {
    SanLine result;
    core::Game scratch;
    scratch.setEnforceDraws(false);
    if (!scratch.reset(fen)) return result;
    int moveNumber = scratch.fullMoveNumber();
    bool white = scratch.sideToMove() == Color::White;
    std::size_t pos = 0;
    for (int played = 0; played < kPvMoves && pos < pv.size(); ++played) {
        const std::size_t end = std::min(pv.find(' ', pos), pv.size());
        if (!scratch.makeUciMove(std::string_view(pv).substr(pos, end - pos))) break;
        if (white) {
            result.text += std::to_string(moveNumber) + ". ";
        } else if (played == 0) {
            result.text += std::to_string(moveNumber) + "... ";
        }
        result.text += scratch.history().back().san + " ";
        if (!white) ++moveNumber;
        white = !white;
        pos = end + 1;
    }
    if (scratch.history().empty()) return result;
    result.first = scratch.history().front().move;
    result.valid = true;
    return result;
}

core::GameRecord emptyRecord(std::string_view fen = core::kStartFen) { return core::GameRecord{{}, core::GameTree(fen)}; }

}  // namespace

AnalysisScreen::AnalysisScreen(app::Settings& settings, app::Assets& assets, const core::OpeningBook* book)
    : settings_(settings), assets_(assets), book_(book), record_(emptyRecord()), compare_(settings) {
    position_.setEnforceDraws(false);
    treeView_.quality = [this](core::NodeId id) { return qualityOf(id); };
    treeView_.emptyHint = "Move a piece on the board to start a line, or paste a FEN or PGN.";
    goTo(kRootNode);
}

// --- Engine ------------------------------------------------------------------

void AnalysisScreen::startEngine() {
    engineStarted_ = true;
    engineError_.clear();
    searchId_ = 0;
    searchNode_ = kNoNode;
    appliedMultiPv_ = 0;

    const std::filesystem::path path = app::resolveEnginePath(settings_.enginePath);
    if (path.empty()) {
        engine_.shutdown();
        engineError_ = "No chess engine found. Set the engine path in Settings.";
        return;
    }
    std::string error;
    if (!engine_.start(path, &error)) engineError_ = error;
}

void AnalysisScreen::configureEngine() {
    // Options must not be changed while a search is running.
    engine_.stopSearch();
    searchId_ = 0;
    searchNode_ = kNoNode;
    appliedMultiPv_ = 0;

    app::configureForAnalysis(engine_, settings_);
}

void AnalysisScreen::update(bool visible) {
    if (visible && !engineStarted_) startEngine();

    for (const engine::EngineEvent& event : engine_.poll()) {
        switch (event.type) {
            case engine::EngineEvent::Type::Ready:
                engineError_.clear();
                configureEngine();
                break;
            case engine::EngineEvent::Type::Info:
                if (event.searchId == searchId_) handleInfo(event.info);
                break;
            case engine::EngineEvent::Type::BestMove:
                if (event.searchId == searchId_) {
                    searchId_ = 0;
                    if (searchIsBatch_) {
                        ++batchIndex_;
                        searchNode_ = kNoNode;
                    }
                }
                break;
            case engine::EngineEvent::Type::Terminated:
                searchId_ = 0;
                searchNode_ = kNoNode;
                batchQueue_.clear();
                engineError_ = event.message.empty() ? "The engine stopped unexpectedly." : event.message;
                break;
        }
    }

    if (wantBatch_ && engine_.state() == engine::UciEngine::State::Ready) {
        wantBatch_ = false;
        startBatch();
    }
    syncSearch(visible);

    // The comparison engine follows the board while live analysis is on.
    if (comparePath_ != settings_.compareEnginePath || compareOptions_ != settings_.compareEngineOptions) {
        comparePath_ = settings_.compareEnginePath;
        compareOptions_ = settings_.compareEngineOptions;
        compare_.restart();
    }
    if (visible && engineEnabled_ && settings_.compareEngines && batchQueue_.empty() && !position_.isOver()) {
        compare_.analyse(record_.tree.startFen(), record_.tree.uciMoves(current_), sideToMoveAt(current_) == Color::White);
    } else {
        compare_.rest();
    }
    compare_.update();
}

void AnalysisScreen::syncSearch(bool visible) {
    if (engine_.state() != engine::UciEngine::State::Ready) return;
    const core::GameTree& tree = record_.tree;

    const auto setMultiPv = [&](int count) {
        if (appliedMultiPv_ == count) return;
        engine_.stopSearch();
        if (engine_.option("MultiPV")) engine_.setOption("MultiPV", std::to_string(count));
        appliedMultiPv_ = count;
    };

    // Whole-game analysis takes priority over the live search.
    if (!batchQueue_.empty()) {
        while (batchIndex_ < batchQueue_.size() && !tree.contains(batchQueue_[batchIndex_])) ++batchIndex_;
        if (batchIndex_ >= batchQueue_.size()) {
            stopBatch();
            notice_ = "Game analysis finished.";
            return;
        }
        const NodeId target = batchQueue_[batchIndex_];
        if (searchId_ != 0 && searchIsBatch_ && searchNode_ == target) return;
        setMultiPv(1);
        engine::SearchLimits limits;
        // Depth alone is unpredictable (sharp positions can take many seconds), so time caps it.
        limits.depth = settings_.analysisDepth;
        limits.moveTimeMs = settings_.analysisMoveTimeMs;
        searchNode_ = target;
        searchIsBatch_ = true;
        searchId_ = engine_.search(tree.startFen(), tree.uciMoves(target), limits);
        return;
    }

    const bool want = visible && engineEnabled_ && !position_.isOver();
    if (!want) {
        if (searchId_ != 0) engine_.stopSearch();
        searchId_ = 0;
        searchNode_ = kNoNode;
        return;
    }
    if (searchNode_ == current_ && !searchIsBatch_ && appliedMultiPv_ == settings_.analysisLines) return;

    setMultiPv(settings_.analysisLines);
    lines_ = {};
    depth_ = 0;
    nodesPerSecond_ = 0;
    tablebaseHits_ = 0;
    searchNode_ = current_;
    searchIsBatch_ = false;
    searchId_ = engine_.search(tree.startFen(), tree.uciMoves(current_), engine::SearchLimits{});
}

void AnalysisScreen::handleInfo(const engine::SearchInfo& info) {
    if (info.isBound || !record_.tree.contains(searchNode_)) return;
    if (!info.scoreCp && !info.mateIn) return;

    const bool whiteToMove = sideToMoveAt(searchNode_) == Color::White;
    const core::Eval eval = app::evalFromInfo(info, whiteToMove);

    if (info.multiPv == 1) {
        const std::optional<core::Eval>& known = record_.tree.node(searchNode_).eval;
        if (!known || known->depth <= eval.depth) {
            record_.tree.setEval(searchNode_, eval);
            // The first move of the main line is the engine's choice here; it decides
            // whether the move actually played counts as the best one.
            if (!info.pv.empty()) record_.tree.setBestMove(searchNode_, info.pv.substr(0, info.pv.find(' ')));
        }
    }
    if (searchIsBatch_) return;

    depth_ = std::max(depth_, info.depth);
    if (info.nodesPerSecond > 0) nodesPerSecond_ = info.nodesPerSecond;
    tablebaseHits_ = std::max(tablebaseHits_, info.tbHits);
    if (info.multiPv < 1 || info.multiPv > static_cast<int>(lines_.size()) || info.pv.empty()) return;

    const SanLine san = sanLine(record_.tree.node(searchNode_).fen, info.pv);
    if (!san.valid) return;

    Line& line = lines_[static_cast<std::size_t>(info.multiPv - 1)];
    line.depth = info.depth;
    line.eval = eval;
    line.san = san.text;
    line.firstMove = san.first;
    line.valid = true;
}

void AnalysisScreen::startBatch() {
    stopBatch();
    const core::GameTree& tree = record_.tree;
    std::vector<NodeId> nodes{kRootNode};
    const std::vector<NodeId> mainLine = tree.mainLine();
    nodes.insert(nodes.end(), mainLine.begin(), mainLine.end());

    for (const NodeId id : nodes) {
        const std::optional<core::Eval>& eval = tree.node(id).eval;
        const bool done = eval && (eval->depth >= settings_.analysisDepth || eval->mate.has_value());
        if (!done) batchQueue_.push_back(id);
    }
    batchIndex_ = 0;
    batchTotal_ = batchQueue_.size();
    if (batchQueue_.empty()) notice_ = "This game is already analysed.";
}

void AnalysisScreen::stopBatch() {
    if (batchQueue_.empty()) return;
    batchQueue_.clear();
    if (searchIsBatch_) {
        if (searchId_ != 0) engine_.stopSearch();
        searchId_ = 0;
        searchNode_ = kNoNode;
        searchIsBatch_ = false;
    }
}

void AnalysisScreen::onSettingsChanged(bool enginePathChanged) {
    if (!engineStarted_) return;
    if (enginePathChanged) {
        stopBatch();
        startEngine();
    } else if (engine_.state() == engine::UciEngine::State::Ready) {
        const bool wasBatch = !batchQueue_.empty();
        configureEngine();
        (void)wasBatch;  // the queue is kept; syncSearch() restarts the right search
    }
}

bool AnalysisScreen::consumeSettingsChanged() { return std::exchange(settingsChanged_, false); }

app::PresenceText AnalysisScreen::presence() const {
    app::PresenceText text;
    const core::GameTree& tree = record_.tree;
    text.details = tree.moveCount() == 0 ? "Analysing a position" : "Analysing a game";

    const std::string white = record_.tag("White");
    const std::string black = record_.tag("Black");
    if (!white.empty() && !black.empty() && white != "?" && black != "?") {
        text.state = white + " vs " + black;
    } else if (current_ != kRootNode) {
        text.state = "Move " + std::to_string(tree.moveNumber(current_));
    } else {
        text.state = "Exploring variations";
    }
    return text;
}

void AnalysisScreen::goToPly(int ply) {
    const std::vector<NodeId> line = record_.tree.mainLine();
    if (ply <= 0 || line.empty()) {
        goTo(kRootNode);
    } else {
        goTo(line[std::min(static_cast<std::size_t>(ply), line.size()) - 1]);
    }
}

std::optional<AnalysisScreen::SaveRequest> AnalysisScreen::takeSaveRequest() {
    return std::exchange(saveRequest_, std::nullopt);
}

void AnalysisScreen::onSaved(std::optional<std::int64_t> libraryId) {
    if (libraryId) {
        libraryId_ = libraryId;
        notice_ = "Saved to the Game Library.";
    } else {
        notice_ = "Could not save the game to the library.";
    }
}

// --- Navigation and editing ----------------------------------------------------

Color AnalysisScreen::sideToMoveAt(NodeId id) const {
    if (id == kRootNode) return record_.tree.startSideToMove();
    return core::opposite(record_.tree.mover(id));
}

std::optional<core::Eval> AnalysisScreen::currentEval() const {
    if (position_.status() == core::Status::Checkmate) {
        core::Eval eval;
        eval.mate = 0;
        eval.cp = position_.sideToMove() == Color::White ? -1 : 1;
        return eval;
    }
    if (searchNode_ == current_ && !searchIsBatch_ && lines_[0].valid) return lines_[0].eval;
    return record_.tree.node(current_).eval;
}

core::MoveQuality AnalysisScreen::qualityOf(NodeId id) const {
    const core::GameTree& tree = record_.tree;
    if (!tree.contains(id) || id == kRootNode) return core::MoveQuality::None;
    // Shallow evaluations from the first moments of a live search are too noisy to judge a
    // move by. Depth 0 means "unknown" (evaluations read from a PGN) and is accepted.
    const auto reliable = [](const std::optional<core::Eval>& eval) {
        return eval && (eval->mate.has_value() || eval->depth == 0 || eval->depth >= 10);
    };
    const core::TreeNode& node = tree.node(id);
    if (!reliable(node.eval) || !reliable(tree.node(node.parent).eval)) return core::MoveQuality::None;
    return core::classifyMove(tree, id);
}

void AnalysisScreen::goTo(NodeId id) {
    if (!record_.tree.contains(id)) id = kRootNode;
    current_ = id;
    if (!core::loadPosition(record_.tree, current_, position_)) {
        current_ = kRootNode;
        core::loadPosition(record_.tree, current_, position_);
    }
    board_.clearInteraction();
    board_.clearAnnotations();
    treeView_.scrollToCurrent();
    commentNode_ = kNoNode;  // refresh the comment box
}

void AnalysisScreen::playMove(core::Move move) {
    // `position_` is at `current_`; playMove advances both together.
    const NodeId next = core::playMove(record_.tree, current_, position_, move);
    if (next == kNoNode) return;
    current_ = next;
    board_.clearInteraction();
    board_.clearAnnotations();
    treeView_.scrollToCurrent();
    commentNode_ = kNoNode;
    notice_.clear();

    if (settings_.soundEnabled) {
        const core::MoveRecord& record = record_.tree.node(next).record;
        const app::SoundId id = position_.isOver() ? app::SoundId::GameEnd
                                : !record.captured.empty() ? app::SoundId::Capture
                                                           : app::SoundId::Move;
        assets_.play(id, settings_.soundVolume);
    }
}

void AnalysisScreen::removeNode(NodeId id) {
    if (!record_.tree.contains(id) || id == kRootNode) return;
    // Step out of the branch first if the current position is inside it.
    const std::vector<NodeId> path = record_.tree.pathTo(current_);
    const bool inside = std::find(path.begin(), path.end(), id) != path.end();
    const NodeId parent = record_.tree.node(id).parent;
    record_.tree.remove(id);
    if (inside) goTo(parent);
}

void AnalysisScreen::load(core::GameRecord record, std::optional<std::int64_t> libraryId) {
    libraryId_ = libraryId;
    stopBatch();
    if (searchId_ != 0) engine_.stopSearch();
    searchId_ = 0;
    searchNode_ = kNoNode;
    lines_ = {};
    depth_ = 0;

    record_ = std::move(record);
    flipped_ = false;
    goTo(kRootNode);
}

void AnalysisScreen::pasteFromClipboard() {
    const char* clipboard = ImGui::GetClipboardText();
    const std::string text = trimmed(clipboard != nullptr ? clipboard : "");
    if (text.empty()) {
        notice_ = "The clipboard is empty.";
        return;
    }
    if (text.find('\n') == std::string::npos && core::validateFen(text)) {
        load(emptyRecord(text));
        notice_ = "Position loaded.";
        return;
    }
    std::string error;
    if (std::optional<core::GameRecord> record = core::parsePgn(text, &error)) {
        load(std::move(*record));
        notice_ = "Game loaded.";
    } else {
        notice_ = "Could not read the clipboard as FEN or PGN: " + error + ".";
    }
}

void AnalysisScreen::openFile() {
    const std::optional<std::filesystem::path> path = app::openPgnDialog();
    if (!path) return;
    std::ifstream input(*path, std::ios::binary);
    if (!input) {
        notice_ = "Could not open the file.";
        return;
    }
    core::PgnSplitter splitter(input);
    std::string text;
    std::string error;
    if (!splitter.next(text)) {
        notice_ = "The file contains no games.";
        return;
    }
    if (std::optional<core::GameRecord> record = core::parsePgn(text, &error)) {
        load(std::move(*record));
        std::string more;
        notice_ = splitter.next(more) ? "Loaded the first game. Use the Game Library to import all games of a file."
                                      : "Game loaded.";
    } else {
        notice_ = "Could not read the game: " + error + ".";
    }
}

void AnalysisScreen::saveFile() {
    const std::optional<std::filesystem::path> path = app::savePgnDialog("game.pgn");
    if (!path) return;
    std::ofstream output(*path, std::ios::binary | std::ios::trunc);
    output << core::writePgn(record_);
    notice_ = output.good() ? "Game saved." : "Could not write the file.";
}

// --- Drawing -------------------------------------------------------------------

void AnalysisScreen::draw() {
    handleShortcuts();

    // Tree edits requested from a context menu during the previous frame.
    if (pendingRemove_ != kNoNode) removeNode(std::exchange(pendingRemove_, kNoNode));
    if (pendingPromote_ != kNoNode) record_.tree.promote(std::exchange(pendingPromote_, kNoNode));
    if (pendingMainLine_ != kNoNode) record_.tree.makeMainLine(std::exchange(pendingMainLine_, kNoNode));

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
    options.interactive = true;
    options.tints = overlayTints();
    const bool liveLines = searchNode_ == current_ && !searchIsBatch_;
    if (settings_.showBestMoveArrow && engineEnabled_ && liveLines && lines_[0].valid) {
        options.arrows.push_back(BoardArrow{lines_[0].firstMove.from, lines_[0].firstMove.to, kEngineArrowColor});
    }
    if (const core::MoveQuality quality = qualityOf(current_); quality != core::MoveQuality::None) {
        options.badges.push_back(BoardBadge{record_.tree.node(current_).record.move.to, quality});
    }

    ImGui::BeginGroup();
    drawEvalBar(ImVec2(kEvalBarWidth, boardSize), currentEval(), flipped_, settings_.animations);
    ImGui::SameLine();
    const std::optional<core::Move> move = board_.draw(position_, assets_, options, boardSize);
    drawEvalGraph(ImVec2(leftWidth, kGraphHeight));
    ImGui::EndGroup();

    ImGui::SameLine();
    // The panel uses the full height even when a narrow window keeps the board small.
    ImGui::BeginChild("##analysis-panel", ImVec2(panelWidth, std::max(columnHeight, avail.y)),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    drawEnginePanel();
    ImGui::Separator();

    const std::string white = record_.tag("White");
    const std::string black = record_.tag("Black");
    if (!white.empty() || !black.empty()) {
        const std::string result = record_.tag("Result");
        ImGui::Text("%s  -  %s", white.empty() ? "?" : white.c_str(), black.empty() ? "?" : black.c_str());
        if (!result.empty() && result != "*") {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", result.c_str());
        }
    }

    if (book_ != nullptr) {
        if (const core::OpeningName* opening = book_->identify(record_.tree, current_)) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s  %s", opening->eco.c_str(), opening->name.c_str());
            ImGui::PopTextWrapPos();
        }
    }

    const float rowHeight = ImGui::GetFrameHeightWithSpacing();
    const float noticeHeight = notice_.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 2.0f;
    const float below = rowHeight * 5.0f + noticeHeight + style.ItemSpacing.y;
    {
        const MoveTreeActions actions = treeView_.draw(record_.tree, current_, std::max(ImGui::GetContentRegionAvail().y - below, 80.0f));
        if (actions.clicked != kNoNode) goTo(actions.clicked);
        // Edits are applied at the start of the next frame, outside the tree drawing.
        if (actions.remove != kNoNode) pendingRemove_ = actions.remove;
        if (actions.promote != kNoNode) pendingPromote_ = actions.promote;
        if (actions.mainLine != kNoNode) pendingMainLine_ = actions.mainLine;
    }

    // Comment on the current move.
    if (commentNode_ != current_) {
        commentNode_ = current_;
        copyToBuffer(comment_, record_.tree.node(current_).comment);
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##comment", current_ == kRootNode ? "Comment on the game" : "Comment on this move",
                                 comment_.data(), comment_.size())) {
        record_.tree.setComment(current_, comment_.data());
    }

    drawButtons();
    if (!notice_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", notice_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    if (move) playMove(*move);
}

void AnalysisScreen::drawEnginePanel() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool batch = !batchQueue_.empty();

    ImGui::Checkbox("##engine-on", &engineEnabled_);
    ImGui::SetItemTooltip("Turn live engine analysis on or off");
    ImGui::SameLine();
    std::string name = engine_.name();
    if (name.empty()) name = "Engine";
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name.c_str());

    // Right side of the header: depth, speed and the number of lines.
    char status[64] = "";
    if (batch) {
        std::snprintf(status, sizeof(status), "analysing game %zu/%zu", std::min(batchIndex_ + 1, batchTotal_), batchTotal_);
    } else if (engineEnabled_ && depth_ > 0) {
        std::snprintf(status, sizeof(status), "depth %d  \xC2\xB7  %.1f Mn/s", depth_,
                      static_cast<double>(nodesPerSecond_) / 1.0e6);
    }
    const float buttonWidth = ImGui::GetFrameHeight();
    const float rightWidth = ImGui::CalcTextSize(status).x + 2.0f * (buttonWidth + style.ItemSpacing.x);
    ImGui::SameLine();
    const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightEdge - rightWidth));
    ImGui::TextDisabled("%s", status);
    ImGui::SameLine();
    ImGui::BeginDisabled(settings_.analysisLines <= 1);
    if (ImGui::Button("-", ImVec2(buttonWidth, 0.0f))) {
        --settings_.analysisLines;
        settingsChanged_ = true;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Show fewer engine lines");
    ImGui::SameLine();
    ImGui::BeginDisabled(settings_.analysisLines >= 5);
    if (ImGui::Button("+", ImVec2(buttonWidth, 0.0f))) {
        ++settings_.analysisLines;
        settingsChanged_ = true;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Show more engine lines");

    if (!engineError_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        ImGui::TextWrapped("%s", engineError_.c_str());
        ImGui::PopStyleColor();
        if (ImGui::Button("Retry engine")) startEngine();
        return;
    }

    // One row per engine line; rows keep their place while the engine is thinking.
    const bool live = searchNode_ == current_ && !searchIsBatch_;
    std::optional<core::Move> clicked;
    for (int i = 0; i < settings_.analysisLines; ++i) {
        const Line& line = lines_[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        if (position_.isOver() && i == 0) {
            ImGui::TextDisabled("%s", position_.status() == core::Status::Checkmate ? "Checkmate" : "Stalemate");
        } else if (engineEnabled_ && live && line.valid) {
            const std::string label = formatEval(line.eval) + "   " + line.san;
            if (ImGui::Selectable(label.c_str(), false)) clicked = line.firstMove;
            ImGui::SetItemTooltip("Play the first move of this line");
        } else {
            ImGui::TextDisabled("%s", !engineEnabled_ ? "-" : batch ? "-" : "...");
        }
        ImGui::PopID();
    }
    // What the endgame tablebases say, when the engine has them and the position is small enough.
    if (live && lines_[0].valid && tablebaseHits_ > 0 && !settings_.syzygyPath.empty() &&
        core::Position(position_.fen()).pieceCount() <= 7) {
        const core::Eval& eval = lines_[0].eval;
        const bool decided = eval.mate.has_value() || std::abs(eval.cp) >= kTablebaseScore;
        if (decided) {
            ImGui::TextDisabled("Tablebase: %s", eval.whiteIsAhead() ? "White wins" : "Black wins");
        } else if (eval.cp == 0) {
            ImGui::TextDisabled("Tablebase: draw");
        }
    }
    drawComparison();
    if (clicked) playMove(*clicked);
}

// One row for the second engine: its evaluation, depth and line, and whether it wants
// the same move as the main engine.
void AnalysisScreen::drawComparison() {
    if (!compare_.configured()) return;
    if (ImGui::Checkbox("##compare-on", &settings_.compareEngines)) settingsChanged_ = true;
    ImGui::SetItemTooltip("Show what the comparison engine thinks of this position");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    std::string name = compare_.name();
    if (name.empty()) name = "Comparison engine";
    if (!compare_.error().empty()) {
        ImGui::TextColored(kErrorColor, "%s", compare_.error().c_str());
        return;
    }
    const app::ComparisonEngine::Result& result = compare_.result();
    const SanLine san = result.valid ? sanLine(position_.fen(), result.pv) : SanLine{};
    if (!settings_.compareEngines || !engineEnabled_ || !san.valid) {
        ImGui::TextDisabled("%s%s", name.c_str(), settings_.compareEngines && engineEnabled_ && !position_.isOver() ? "  ..." : "");
        return;
    }
    std::string label = name + "  " + formatEval(result.eval) + "  depth " + std::to_string(result.depth);
    const bool live = searchNode_ == current_ && !searchIsBatch_ && lines_[0].valid;
    if (live) label += san.first == lines_[0].firstMove ? "  \xC2\xB7  same move" : "  \xC2\xB7  other move";
    ImGui::TextDisabled("%s", label.c_str());
    if (ImGui::Selectable((san.text + "##compare-line").c_str(), false)) playMove(san.first);
    ImGui::SetItemTooltip("Play the first move of this line");
}

const std::vector<SquareTint>& AnalysisScreen::overlayTints() {
    const std::string key = std::to_string(overlay_) + position_.fen();
    if (key == overlayKey_) return overlayTints_;
    overlayKey_ = key;
    overlayTints_.clear();
    if (overlay_ == 0) return overlayTints_;

    const core::Position position(position_.fen());
    if (overlay_ == 1) {
        // Square control: who attacks each square more often. Blue for White, red for Black.
        for (core::Square square = 0; square < 64; ++square) {
            const int white = position.attackerCount(square, Color::White);
            const int black = position.attackerCount(square, Color::Black);
            if (white == 0 && black == 0) continue;
            const int difference = white - black;
            if (difference == 0) {
                overlayTints_.push_back(SquareTint{square, IM_COL32(120, 120, 120, 70)});
                continue;
            }
            const int alpha = std::min(60 + 40 * std::abs(difference), 190);
            overlayTints_.push_back(SquareTint{square, difference > 0 ? IM_COL32(40, 110, 220, alpha) : IM_COL32(215, 55, 45, alpha)});
        }
    } else if (overlay_ == 2) {
        // Pieces that can be won: attacked by something cheaper, or attacked and not defended.
        for (const Color color : {Color::White, Color::Black}) {
            for (const core::Square square : position.hangingPieces(color)) {
                overlayTints_.push_back(SquareTint{square, IM_COL32(220, 40, 40, 170)});
            }
        }
    } else {
        // Piece activity: from red (no moves) over yellow to green (eight or more).
        for (core::Square square = 0; square < 64; ++square) {
            const core::Piece piece = position.at(square);
            if (piece.empty() || piece.type == core::PieceType::Pawn || piece.type == core::PieceType::King) continue;
            const float activity = std::min(static_cast<float>(position.mobility(square)) / 8.0f, 1.0f);
            const int red = activity < 0.5f ? 220 : static_cast<int>(220.0f * (1.0f - activity) * 2.0f);
            const int green = activity < 0.5f ? static_cast<int>(190.0f * activity * 2.0f) : 190;
            overlayTints_.push_back(SquareTint{square, IM_COL32(red, green, 40, 165)});
        }
    }
    return overlayTints_;
}

void AnalysisScreen::drawEvalGraph(ImVec2 size) {
    const NodeId clicked = ui::drawEvalGraph(record_.tree, current_, size, [this](NodeId id) { return qualityOf(id); },
                                             "Not analysed yet. Use \"Analyse\" to fill the graph.");
    if (clicked != kNoNode) goTo(clicked);
}

void AnalysisScreen::drawButtons() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const core::GameTree& tree = record_.tree;
    const float full = ImGui::GetContentRegionAvail().x;
    const auto width = [&](int perRow) {
        return ImVec2((full - static_cast<float>(perRow - 1) * style.ItemSpacing.x) / static_cast<float>(perRow), 0.0f);
    };

    const bool atStart = current_ == kRootNode;
    const bool atEnd = tree.node(current_).children.empty();

    ImGui::SetNextItemWidth(full);
    ImGui::Combo("##overlay", &overlay_,
                 "Board overlay: none\0Board overlay: square control\0Board overlay: pieces that can be won\0"
                 "Board overlay: piece activity\0");
    ImGui::SetItemTooltip("Colour the squares: who controls them (blue White, red Black), which pieces can be won,\n"
                          "or how many moves each piece has (red none, green many)");

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
    ImGui::SetItemTooltip("End of the line (End)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Flip", width(5))) flipped_ = !flipped_;
    ImGui::SetItemTooltip("Flip the board (F)");

    if (ImGui::Button("New", width(4))) {
        load(emptyRecord());
        notice_.clear();
    }
    ImGui::SetItemTooltip("Start from an empty game");
    ImGui::SameLine();
    if (ImGui::Button("Paste", width(4))) pasteFromClipboard();
    ImGui::SetItemTooltip("Load a FEN or PGN from the clipboard (Ctrl+V)");
    ImGui::SameLine();
    const bool narrow = full < 330.0f;
    if (ImGui::Button(narrow ? "FEN###copyfen" : "Copy FEN###copyfen", width(4))) {
        ImGui::SetClipboardText(position_.fen().c_str());
        notice_ = "Position copied to the clipboard.";
    }
    ImGui::SameLine();
    if (ImGui::Button(narrow ? "PGN###copypgn" : "Copy PGN###copypgn", width(4))) {
        ImGui::SetClipboardText(core::writePgn(record_).c_str());
        notice_ = "Game copied to the clipboard.";
    }

    if (ImGui::Button("Open...", width(4))) openFile();
    ImGui::SetItemTooltip("Open a PGN file");
    ImGui::SameLine();
    if (ImGui::Button("Save...", width(4))) saveFile();
    ImGui::SetItemTooltip("Save this game as a PGN file");
    ImGui::SameLine();
    ImGui::BeginDisabled(tree.moveCount() == 0);
    if (ImGui::Button(narrow ? "Library###tolibrary" : "To library###tolibrary", width(4))) saveRequest_ = SaveRequest{record_, libraryId_};
    ImGui::SetItemTooltip("Store this game in the Game Library");
    ImGui::SameLine();
    const bool batch = !batchQueue_.empty();
    if (ImGui::Button(batch ? "Stop" : "Analyse", width(4))) {
        if (batch) {
            stopBatch();
            notice_ = "Game analysis stopped.";
        } else {
            startBatch();
        }
    }
    ImGui::SetItemTooltip("Evaluate every move of the main line to fill the graph");
    ImGui::EndDisabled();
}

void AnalysisScreen::handleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
    const core::GameTree& tree = record_.tree;
    const core::TreeNode& node = tree.node(current_);

    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && current_ != kRootNode) goTo(node.parent);
    else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !node.children.empty()) goTo(node.children.front());
    else if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) goTo(kRootNode);
    else if (ImGui::IsKeyPressed(ImGuiKey_End, false)) goTo(tree.lineEnd(current_));
    else if ((ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) && current_ != kRootNode) {
        // Step between the alternatives at this point.
        const std::vector<NodeId>& siblings = tree.node(node.parent).children;
        const auto it = std::find(siblings.begin(), siblings.end(), current_);
        if (it != siblings.end() && siblings.size() > 1) {
            const std::ptrdiff_t index = it - siblings.begin();
            const std::ptrdiff_t step = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : -1;
            const std::ptrdiff_t next = std::clamp<std::ptrdiff_t>(index + step, 0, static_cast<std::ptrdiff_t>(siblings.size()) - 1);
            if (next != index) goTo(siblings[static_cast<std::size_t>(next)]);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl) flipped_ = !flipped_;
    if (ImGui::IsKeyPressed(ImGuiKey_V, false) && io.KeyCtrl) pasteFromClipboard();
}

}  // namespace ui

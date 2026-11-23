#include "ui/PuzzleScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include <imgui.h>

#include "app/Assets.h"
#include "app/FileDialog.h"
#include "chess/Repertoire.h"
#include "ui/Common.h"

namespace ui {

namespace {

struct ThemeChoice {
    const char* label;
    const char* tag;  // Lichess theme name
};

constexpr ThemeChoice kThemes[] = {
    {"Any theme", ""},
    {"From my games", "fromMyGames"},
    {"Fork", "fork"},
    {"Pin", "pin"},
    {"Skewer", "skewer"},
    {"Discovered attack", "discoveredAttack"},
    {"Double check", "doubleCheck"},
    {"Deflection", "deflection"},
    {"Attraction", "attraction"},
    {"Sacrifice", "sacrifice"},
    {"Hanging piece", "hangingPiece"},
    {"Trapped piece", "trappedPiece"},
    {"Defensive move", "defensiveMove"},
    {"Quiet move", "quietMove"},
    {"Zugzwang", "zugzwang"},
    {"Promotion", "promotion"},
    {"Mate in 1", "mateIn1"},
    {"Mate in 2", "mateIn2"},
    {"Mate in 3", "mateIn3"},
    {"Back-rank mate", "backRankMate"},
    {"Opening", "opening"},
    {"Middlegame", "middlegame"},
    {"Endgame", "endgame"},
    {"Pawn endgame", "pawnEndgame"},
    {"Rook endgame", "rookEndgame"},
};

constexpr float kBarHeight = 34.0f;
constexpr auto kReplyDelay = std::chrono::milliseconds(450);
constexpr auto kRevealDelay = std::chrono::milliseconds(700);

std::string formatTime(double seconds) {
    const int total = static_cast<int>(seconds);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
    return buffer;
}

}  // namespace

PuzzleScreen::PuzzleScreen(app::Settings& settings, app::Assets& assets, storage::PuzzleStore* store,
                           std::filesystem::path databaseFile, const std::filesystem::path& starterCsv)
    : settings_(settings), assets_(assets), store_(store), databaseFile_(std::move(databaseFile)) {
    if (store_ == nullptr) return;
    // First start: fill the empty database with the bundled starter set.
    if (store_->count() == 0 && !starterCsv.empty()) store_->importCsvFile(starterCsv);
    refreshStats();
}

PuzzleScreen::~PuzzleScreen() {
    importCancel_ = true;
    if (importThread_.joinable()) importThread_.join();
}

bool PuzzleScreen::consumeSettingsChanged() { return std::exchange(settingsChanged_, false); }

std::optional<core::GameRecord> PuzzleScreen::takeAnalysisRequest() { return std::exchange(analysisRequest_, std::nullopt); }

app::PresenceText PuzzleScreen::presence() const {
    app::PresenceText text;
    text.details = "Solving puzzles";
    text.state = "Puzzle rating " + std::to_string(stats_.rating);
    return text;
}

void PuzzleScreen::refreshStats() {
    if (store_ == nullptr) return;
    stats_ = store_->stats();
    history_ = store_->history(60);
    ratingHistory_ = store_->ratingHistory(120);
    puzzleCount_ = store_->count();
    reviewQueue_ = store_->reviewQueue(core::todayNumber());
}

double PuzzleScreen::elapsedSeconds() const {
    if (phase_ == Phase::Solving) return std::chrono::duration<double>(Clock::now() - startedAt_).count();
    return finalSeconds_;
}

// --- Loading puzzles -------------------------------------------------------------

bool PuzzleScreen::begin(const core::Puzzle& puzzle, bool rated) {
    if (!session_.start(puzzle)) return false;
    puzzle_ = puzzle;
    phase_ = Phase::Solving;
    rated_ = rated;
    recorded_ = false;
    unaided_ = true;
    hintLevel_ = 0;
    feedback_.clear();
    notice_.clear();
    startedAt_ = Clock::now();
    finalSeconds_ = 0.0;
    board_.clearInteraction();
    board_.clearAnnotations();
    return true;
}

void PuzzleScreen::loadNext() {
    if (store_ == nullptr) return;
    static constexpr int kOffsets[] = {-300, 0, 300};
    storage::PuzzleFilter filter;
    const int center = stats_.rating + kOffsets[std::clamp(difficulty_, 0, 2)];
    filter.minRating = center - 150;
    filter.maxRating = center + 150;
    filter.theme = kThemes[theme_].tag;

    // Widen the rating window step by step rather than coming back empty-handed.
    for (int attempt = 0; attempt < 12; ++attempt) {
        if (attempt == 4) {
            filter.minRating = center - 400;
            filter.maxRating = center + 400;
        } else if (attempt == 8) {
            filter.minRating = 0;
            filter.maxRating = 4000;
        }
        const std::optional<core::Puzzle> puzzle = store_->random(filter, rng_());
        if (!puzzle) {
            if (attempt < 8) attempt = attempt < 4 ? 3 : 7;  // nothing in this window: jump to the next one
            continue;
        }
        // A puzzle whose data does not hold up is skipped silently.
        if (begin(*puzzle, true)) return;
    }
    puzzle_.reset();
    phase_ = Phase::Idle;
    notice_ = puzzleCount_ == 0 ? "There are no puzzles yet. Import a puzzle file to begin."
                                : "No unsolved puzzles match this selection.";
}

void PuzzleScreen::loadDaily() {
    if (store_ == nullptr) return;
    const std::optional<core::Puzzle> puzzle = store_->daily(core::todayNumber());
    if (!puzzle || !begin(*puzzle, false)) {
        notice_ = "There is no daily puzzle available.";
        return;
    }
    notice_ = "Today's puzzle. It does not change your rating.";
}

void PuzzleScreen::loadDue() {
    if (store_ == nullptr) return;
    const std::optional<core::Puzzle> puzzle = store_->nextDue(core::todayNumber());
    if (!puzzle || !begin(*puzzle, false)) {
        notice_ = "Nothing is due for review today.";
        return;
    }
    const std::optional<core::CardState> card = store_->card(puzzle->id);
    notice_ = "Review: a puzzle you missed before";
    if (card && card->streak > 0) notice_ += " (solved " + std::to_string(card->streak) + "x in a row so far)";
    notice_ += ". It does not change your rating.";
}

void PuzzleScreen::practiseTheme(std::string_view theme) {
    for (int i = 0; i < static_cast<int>(std::size(kThemes)); ++i) {
        if (theme == kThemes[i].tag) theme_ = i;
    }
    loadNext();
}

// --- Solving ---------------------------------------------------------------------

void PuzzleScreen::recordOnce(bool solved) {
    if (recorded_ || store_ == nullptr || !puzzle_) return;
    recorded_ = true;
    store_->recordAttempt(*puzzle_, solved, elapsedSeconds(), rated_);
    refreshStats();
}

void PuzzleScreen::onMove(core::Move move) {
    if (phase_ != Phase::Solving) return;
    switch (session_.play(move)) {
        case core::PuzzleSession::Outcome::Wrong:
            unaided_ = false;
            feedback_ = "That is not the move. Try again.";
            // The first mistake decides the result; further tries are for learning.
            recordOnce(false);
            break;
        case core::PuzzleSession::Outcome::Correct:
            feedback_ = "Correct. Keep going.";
            hintLevel_ = 0;
            replyAt_ = Clock::now() + kReplyDelay;
            if (settings_.soundEnabled) assets_.play(app::SoundId::Move, settings_.soundVolume);
            break;
        case core::PuzzleSession::Outcome::Solved:
            finalSeconds_ = elapsedSeconds();
            phase_ = Phase::Solved;
            hintLevel_ = 0;
            feedback_ = unaided_ ? "Solved!" : "Solved, with help.";
            recordOnce(unaided_);
            if (settings_.soundEnabled) assets_.play(app::SoundId::GameEnd, settings_.soundVolume);
            break;
        case core::PuzzleSession::Outcome::Ignored:
            break;
    }
    board_.clearInteraction();
}

void PuzzleScreen::useHint() {
    if (phase_ != Phase::Solving || !session_.expectedMove()) return;
    unaided_ = false;
    recordOnce(false);
    hintLevel_ = std::min(hintLevel_ + 1, 2);
    feedback_ = hintLevel_ == 1 ? "Hint: this piece moves." : "Hint: this is the move.";
}

void PuzzleScreen::showSolution() {
    if (phase_ != Phase::Solving) return;
    unaided_ = false;
    finalSeconds_ = elapsedSeconds();
    recordOnce(false);
    phase_ = Phase::Revealed;
    hintLevel_ = 0;
    feedback_ = "Solution: " + session_.solutionSan();
    revealAt_ = Clock::now();
    board_.clearInteraction();
}

void PuzzleScreen::update() {
    if (importDone_.load()) finishImport();

    const Clock::time_point now = Clock::now();
    if (phase_ == Phase::Solving && session_.replyPending() && now >= replyAt_) {
        session_.playReply();
        board_.clearInteraction();
        if (settings_.soundEnabled) assets_.play(app::SoundId::Move, settings_.soundVolume);
    }
    if (phase_ == Phase::Revealed && !session_.solved() && now >= revealAt_) {
        session_.revealNext();
        revealAt_ = now + kRevealDelay;
        if (settings_.soundEnabled) assets_.play(app::SoundId::Move, settings_.soundVolume);
    }

    if (autoSolve_) {
        if (autoAt_ == Clock::time_point{}) autoAt_ = now + std::chrono::milliseconds(600);
        if (now >= autoAt_) {
            autoAt_ = {};
            if (phase_ == Phase::Idle || phase_ == Phase::Solved) {
                loadNext();
            } else if (const std::optional<core::Move> expected = session_.expectedMove()) {
                onMove(*expected);
            }
        }
    }
}

// --- Import ----------------------------------------------------------------------

void PuzzleScreen::startImport(const std::filesystem::path& file) {
    if (importRunning_.load()) return;
    if (importThread_.joinable()) importThread_.join();
    importRunning_ = true;
    importDone_ = false;
    importCancel_ = false;
    importProgress_ = 0;
    importResult_ = {};
    // The worker has its own connection, so puzzles can be solved while it runs.
    importThread_ = std::thread([this, file] {
        std::string error;
        const std::unique_ptr<storage::PuzzleStore> store = storage::PuzzleStore::open(databaseFile_, &error);
        if (store) {
            importResult_ = store->importCsvFile(file, &importProgress_, &importCancel_);
        } else {
            importResult_.error = error;
        }
        importDone_ = true;
    });
}

void PuzzleScreen::finishImport() {
    if (importThread_.joinable()) importThread_.join();
    importDone_ = false;
    importRunning_ = false;
    if (!importResult_.error.empty()) {
        notice_ = "Import failed: " + importResult_.error;
    } else {
        notice_ = "Imported " + std::to_string(importResult_.imported) + " puzzles";
        if (importResult_.skipped > 0) notice_ += " (" + std::to_string(importResult_.skipped) + " skipped or already present)";
        notice_ += ".";
    }
    refreshStats();
}

// --- Drawing ---------------------------------------------------------------------

void PuzzleScreen::draw() {
    if (store_ == nullptr) {
        ImGui::TextUnformatted("Puzzles");
        ImGui::TextColored(kErrorColor, "The puzzle database is not available.");
        return;
    }
    if (phase_ == Phase::Idle && !puzzle_ && notice_.empty()) loadNext();

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float barBlock = kBarHeight + style.ItemSpacing.y;
    const BoardLayout layout = boardLayout(0.32f, 300.0f, 400.0f, 0.0f, barBlock);
    const float panelWidth = layout.panelWidth;
    const float boardSize = layout.boardSize;
    ImGui::SetCursorPosX(layout.boardX);

    const bool solving = phase_ == Phase::Solving;
    const core::Color solver = session_.solverColor();

    BoardOptions options = boardOptionsFrom(settings_);
    options.flipped = session_.active() && solver == core::Color::Black;
    options.interactive = solving && !session_.replyPending() && session_.game().sideToMove() == solver;
    // Blindfold: the pieces stay hidden until the puzzle is over (or Space is held).
    options.hidePieces = solving && blindfoldHidden(settings_);
    if (options.hidePieces) options.showLegalMoves = false;
    if (ImGui::IsKeyPressed(ImGuiKey_B, false) && !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl) {
        settings_.blindfold = !settings_.blindfold;
        settingsChanged_ = true;
    }
    if (solving && hintLevel_ > 0) {
        if (const std::optional<core::Move> expected = session_.expectedMove()) {
            options.highlights.push_back(expected->from);
            if (hintLevel_ >= 2) options.arrows.push_back(BoardArrow{expected->from, expected->to, IM_COL32(40, 120, 210, 200)});
        }
    }

    ImGui::BeginGroup();
    // Prompt line above the board.
    ImGui::BeginChild("##prompt", ImVec2(boardSize, kBarHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY((kBarHeight - ImGui::GetTextLineHeight()) * 0.5f);
    if (!session_.active()) {
        ImGui::TextDisabled("No puzzle loaded.");
    } else if (solving) {
        ImGui::Text("Find the best move for %s.", solver == core::Color::White ? "White" : "Black");
        ImGui::SameLine();
        ImGui::TextDisabled("Move %d of %d", std::min(session_.solverMovesDone() + 1, session_.solverMovesTotal()),
                            session_.solverMovesTotal());
    } else {
        ImGui::TextColored(phase_ == Phase::Solved && unaided_ ? kAccentColor : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s",
                           phase_ == Phase::Solved ? (unaided_ ? "Solved!" : "Solved, with help.") : "Solution shown.");
    }
    ImGui::EndChild();
    const std::optional<core::Move> move = board_.draw(session_.game(), assets_, options, boardSize);
    ImGui::EndGroup();

    ImGui::SameLine(layout.panelX);
    drawSidePanel(panelWidth, std::max(boardSize + barBlock, avail.y));

    if (move) onMove(*move);
}

void PuzzleScreen::drawSidePanel(float width, float height) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::BeginChild("##puzzle-panel", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding);

    // --- Rating and statistics ---------------------------------------------------
    ImGui::Text("Puzzle rating  %d", stats_.rating);
    ImGui::TextDisabled("%d solved of %d  \xC2\xB7  streak %d  \xC2\xB7  best %d", stats_.solved, stats_.attempts, stats_.streak,
                        stats_.bestStreak);
    drawRatingGraph(ImVec2(ImGui::GetContentRegionAvail().x, 54.0f));
    ImGui::Separator();

    // --- Current puzzle ----------------------------------------------------------
    const bool finished = phase_ == Phase::Solved || phase_ == Phase::Revealed;
    if (puzzle_) {
        ImGui::Text("Time  %s", formatTime(elapsedSeconds()).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(rated_ ? "rated" : "not rated");
        if (finished) {
            // Rating and themes would give the answer away, so they appear afterwards.
            ImGui::TextDisabled("Puzzle %s  \xC2\xB7  rated %d", puzzle_->id.c_str(), puzzle_->rating);
            if (!puzzle_->themes.empty()) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("Themes: %s", puzzle_->themes.c_str());
                ImGui::PopTextWrapPos();
            }
        }
    }
    if (!feedback_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(feedback_.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!notice_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", notice_.c_str());
        ImGui::PopTextWrapPos();
    }

    const float full = ImGui::GetContentRegionAvail().x;
    const ImVec2 half((full - style.ItemSpacing.x) * 0.5f, 0.0f);
    const ImVec2 third((full - 2.0f * style.ItemSpacing.x) / 3.0f, 0.0f);

    const bool solving = phase_ == Phase::Solving;
    ImGui::BeginDisabled(!solving);
    if (ImGui::Button("Hint", third)) useHint();
    ImGui::SetItemTooltip("Show which piece moves; press again for the move. Counts as not solved.");
    ImGui::SameLine();
    if (ImGui::Button("Solution", third)) showSolution();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!puzzle_);
    if (ImGui::Button("Retry", third) && puzzle_) {
        const core::Puzzle again = *puzzle_;
        begin(again, false);
    }
    ImGui::SetItemTooltip("Start this puzzle again (not rated)");
    ImGui::EndDisabled();

    if (ImGui::Button("Next puzzle", half)) loadNext();
    ImGui::SameLine();
    ImGui::BeginDisabled(!puzzle_);
    if (ImGui::Button("Analyse", half) && puzzle_) {
        // Hand the position and its solution to Analysis as a small game.
        core::GameRecord record{{}, core::GameTree(puzzle_->fen)};
        core::Game game;
        game.setEnforceDraws(false);
        if (game.reset(puzzle_->fen)) {
            core::NodeId node = core::kRootNode;
            for (const std::string& uci : puzzle_->moves) {
                const std::optional<core::Move> parsed = core::parseUciMove(uci);
                if (!parsed || !game.makeUciMove(uci)) break;
                node = record.tree.addMove(node, game.history().back(), game.fen());
            }
            record.setTag("Event", "Puzzle " + puzzle_->id);
            analysisRequest_ = std::move(record);
        }
    }
    ImGui::EndDisabled();

    if (ImGui::Button("Daily puzzle", half)) loadDaily();
    ImGui::SameLine();
    ImGui::BeginDisabled(reviewQueue_.due == 0);
    char retryLabel[48];
    std::snprintf(retryLabel, sizeof(retryLabel), "Review due (%d)###reviewdue", reviewQueue_.due);
    if (ImGui::Button(retryLabel, half)) loadDue();
    ImGui::EndDisabled();
    if (reviewQueue_.due > 0) {
        ImGui::SetItemTooltip("Puzzles you missed come back on a schedule: the next day, then further apart each time "
                              "you solve them.");
    } else if (reviewQueue_.nextDueDay) {
        const int days = *reviewQueue_.nextDueDay - core::todayNumber();
        ImGui::SetItemTooltip("%d %s scheduled. The next one is due in %d %s.", reviewQueue_.scheduled,
                              reviewQueue_.scheduled == 1 ? "puzzle is" : "puzzles are", days, days == 1 ? "day" : "days");
    } else {
        ImGui::SetItemTooltip("Puzzles you miss come back here for review on a schedule.");
    }

    // --- Selection ---------------------------------------------------------------
    ImGui::Separator();
    ImGui::SetNextItemWidth(half.x);
    ImGui::Combo("##difficulty", &difficulty_, "Easier\0Normal\0Harder\0");
    ImGui::SetItemTooltip("How the next puzzles compare to your rating");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half.x);
    if (ImGui::BeginCombo("##theme", kThemes[theme_].label)) {
        for (int i = 0; i < static_cast<int>(std::size(kThemes)); ++i) {
            if (ImGui::Selectable(kThemes[i].label, theme_ == i)) theme_ = i;
        }
        ImGui::EndCombo();
    }
    if (ImGui::Checkbox("Blindfold", &settings_.blindfold)) settingsChanged_ = true;
    ImGui::SetItemTooltip("Solve without seeing the pieces (B). Hold Space to peek.");

    if (importRunning_.load()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Importing... %d", importProgress_.load());
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) importCancel_ = true;
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%d puzzles", puzzleCount_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Import...")) {
            if (const std::optional<std::filesystem::path> path = app::openCsvDialog()) startImport(*path);
        }
        ImGui::SetItemTooltip("Import puzzles in the Lichess CSV format (lichess_db_puzzle.csv, unpacked)");
    }

    // --- History -----------------------------------------------------------------
    ImGui::Separator();
    drawHistory(std::max(ImGui::GetContentRegionAvail().y, 60.0f));
    ImGui::EndChild();
}

void PuzzleScreen::drawRatingGraph(ImVec2 size) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    if (ratingHistory_.size() < 2) return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const auto [lowest, highest] = std::minmax_element(ratingHistory_.begin(), ratingHistory_.end());
    const float low = static_cast<float>(*lowest) - 10.0f;
    const float range = std::max(static_cast<float>(*highest) + 10.0f - low, 40.0f);
    const std::size_t count = ratingHistory_.size();
    ImVec2 previous;
    for (std::size_t i = 0; i < count; ++i) {
        const ImVec2 point(origin.x + size.x * static_cast<float>(i) / static_cast<float>(count - 1),
                           origin.y + size.y * (1.0f - (static_cast<float>(ratingHistory_[i]) - low) / range));
        if (i > 0) drawList->AddLine(previous, point, ImGui::ColorConvertFloat4ToU32(kAccentColor), 1.5f);
        previous = point;
    }
}

void PuzzleScreen::drawHistory(float height) {
    if (history_.empty()) {
        ImGui::TextDisabled("Solved and missed puzzles are listed here.");
        return;
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##puzzle-history", 4, flags, ImVec2(0.0f, height))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 62.0f);
    ImGui::TableSetupColumn("Puzzle");
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("You", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    tableHeaders();

    std::string toLoad;
    for (std::size_t i = 0; i < history_.size(); ++i) {
        const storage::PuzzleAttempt& attempt = history_[i];
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushStyleColor(ImGuiCol_Text, attempt.solved ? kAccentColor : kErrorColor);
        if (tableRow(attempt.solved ? "Solved" : "Missed", false).clicked) toLoad = attempt.puzzleId;
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s\nClick to try this puzzle again", attempt.when.c_str());
        ImGui::PopID();
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", attempt.puzzleRating);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(formatTime(attempt.seconds).c_str());
        ImGui::TableSetColumnIndex(3);
        if (attempt.rated) {
            ImGui::Text("%d", attempt.ratingAfter);
        } else {
            ImGui::TextDisabled("-");
        }
    }
    ImGui::EndTable();

    if (!toLoad.empty()) {
        if (const std::optional<core::Puzzle> puzzle = store_->get(toLoad)) {
            if (begin(*puzzle, false)) notice_ = "A puzzle from your history. It does not change your rating.";
        }
    }
}

}  // namespace ui

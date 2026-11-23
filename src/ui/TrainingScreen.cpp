#include "ui/TrainingScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <imgui.h>

#include "app/Assets.h"
#include "app/Theme.h"
#include "chess/Pgn.h"
#include "chess/Statistics.h"
#include "storage/GameStore.h"
#include "storage/ReviewStore.h"
#include "storage/TrainingStore.h"
#include "ui/Common.h"
#include "ui/QualityBadge.h"

namespace ui {

namespace {

using core::Color;
using core::Motif;

constexpr int kMaxGames = 300;           // most recent reviewed games looked at
constexpr double kAcceptableLoss = 4.0;  // drill: another move is fine if it keeps this close
constexpr float kTileHeight = 104.0f;
constexpr ImU32 kBestArrow = IM_COL32(96, 160, 48, 215);
constexpr ImU32 kPlayedArrow = IM_COL32(210, 70, 60, 200);
constexpr ImU32 kTriedArrow = IM_COL32(40, 120, 210, 190);

void tile(const char* id, float width, const char* caption, const char* value, const char* detail) {
    ImGui::BeginChild(id, ImVec2(width, kTileHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("%s", caption);
    ImGui::PushFont(app::headlineFont());
    ImGui::TextUnformatted(value);
    ImGui::PopFont();
    ImGui::TextDisabled("%s", detail);
    ImGui::EndChild();
}

void bar(ImVec2 size, float fraction, ImU32 color) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
    const float filled = size.x * std::clamp(fraction, 0.0f, 1.0f);
    if (filled >= 1.0f) drawList->AddRectFilled(origin, ImVec2(origin.x + filled, origin.y + size.y), color, 3.0f);
}

ImU32 accentU32() { return ImGui::ColorConvertFloat4ToU32(kAccentColor); }

// Orders "level 1".."level 8" first, then Elo values, each ascending.
int strengthOrder(const std::string& label) {
    const int number = std::atoi(label.c_str() + (label.rfind("level ", 0) == 0 ? 6 : 0));
    return label.rfind("level ", 0) == 0 ? number : 100 + number;
}

}  // namespace

TrainingScreen::TrainingScreen(app::Settings& settings, app::Assets& assets, std::filesystem::path databaseFile,
                               storage::TrainingStore* training)
    : settings_(settings), assets_(assets), databaseFile_(std::move(databaseFile)), training_(training), judge_(settings) {
    drillPosition_.setEnforceDraws(false);
}

TrainingScreen::~TrainingScreen() {
    if (worker_.joinable()) {
        cancel_ = true;
        worker_.join();
    }
}

// --- Building the numbers ---------------------------------------------------------

void TrainingScreen::startBuild() {
    dirty_ = false;
    if (worker_.joinable() || databaseFile_.empty()) return;
    worker_ = std::thread([this, file = databaseFile_, names = settings_.playerNames] {
        auto data = std::make_unique<Data>();
        const auto games = storage::GameStore::open(file);
        const auto reviews = games ? storage::ReviewStore::open(file) : nullptr;
        if (games && reviews) {
            std::vector<core::PlayerGame> played = reviews->playerGames(names);
            data->reviewedGames = static_cast<int>(played.size());
            if (played.size() > static_cast<std::size_t>(kMaxGames)) played.erase(played.begin(), played.end() - kMaxGames);
            for (const core::PlayerGame& game : played) {
                if (cancel_) return;
                const std::optional<std::string> pgn = games->pgn(game.gameId);
                if (!pgn) continue;
                const std::optional<core::GameRecord> record = core::parsePgn(*pgn);
                if (!record) continue;
                const std::size_t before = data->report.examples.size();
                core::addToReport(data->report, *record, game.color, game.gameId);
                for (std::size_t i = before; i < data->report.examples.size(); ++i) {
                    data->examplePlies.push_back(record->tree.node(data->report.examples[i].node).ply);
                }
            }

            // Results in Play against each engine level.
            storage::GameFilter filter;
            filter.limit = 1000000;
            std::vector<EngineResults> engine;
            for (const storage::GameSummary& game : games->list(filter)) {
                if (game.source != "play") continue;
                const bool white = core::matchesPlayer(game.white, names);
                const bool black = core::matchesPlayer(game.black, names);
                if (white == black) continue;
                const std::string label = core::engineStrengthLabel(white ? game.black : game.white);
                if (label.empty() || game.result == "*") continue;
                auto it = std::find_if(engine.begin(), engine.end(), [&](const EngineResults& r) { return r.label == label; });
                if (it == engine.end()) {
                    engine.push_back(EngineResults{label});
                    it = engine.end() - 1;
                }
                ++it->games;
                if (game.result == "1/2-1/2") {
                    ++it->draws;
                } else if ((game.result == "1-0") == white) {
                    ++it->wins;
                } else {
                    ++it->losses;
                }
            }
            std::sort(engine.begin(), engine.end(),
                      [](const EngineResults& a, const EngineResults& b) { return strengthOrder(a.label) < strengthOrder(b.label); });
            data->engine = std::move(engine);
        }
        if (const auto puzzles = storage::PuzzleStore::open(file)) {
            data->puzzles = puzzles->stats();
            data->puzzleQueue = puzzles->reviewQueue(core::todayNumber());
        }
        if (const auto training = storage::TrainingStore::open(file)) {
            for (const auto& [id, progress] : training->endgameProgress()) {
                data->endgameAttempts += progress.attempts;
                data->endgameSuccesses += progress.successes;
            }
        }
        result_ = std::move(data);
        ready_ = true;
    });
}

void TrainingScreen::collectResult() {
    if (!ready_) return;
    worker_.join();
    ready_ = false;
    data_ = std::move(result_);
    if (training_ != nullptr) cards_ = training_->mistakeCards();
    // A drill in progress keeps its position; the queue is rebuilt around it.
    if (drillPhase_ == DrillPhase::Solving || drillPhase_ == DrillPhase::Judging) {
        drillPhase_ = DrillPhase::Idle;
        current_.reset();
    }
    buildDrillQueue();
}

std::string TrainingScreen::cardKey(const core::MistakeExample& example) const {
    return core::positionKey(example.fenBefore) + " " + example.best;
}

void TrainingScreen::buildDrillQueue() {
    queue_.clear();
    dueCount_ = 0;
    newCount_ = 0;
    if (!data_) return;
    const int today = core::todayNumber();
    std::vector<std::pair<int, std::size_t>> due;
    std::vector<std::size_t> fresh;
    std::vector<std::string> seen;
    const std::vector<core::MistakeExample>& examples = data_->report.examples;
    for (std::size_t i = examples.size(); i-- > 0;) {  // newest games first
        const core::MistakeExample& example = examples[i];
        if (example.best.empty()) continue;
        if (drillMotif_ >= 0 && std::find(example.motifs.begin(), example.motifs.end(), static_cast<Motif>(drillMotif_)) ==
                                    example.motifs.end()) {
            continue;
        }
        const std::string key = cardKey(example);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;  // the same mistake twice
        seen.push_back(key);
        const auto card = cards_.find(key);
        if (card == cards_.end()) {
            fresh.push_back(i);
        } else if (card->second.isDue(today)) {
            due.emplace_back(card->second.dueDay, i);
        }
    }
    std::stable_sort(due.begin(), due.end());
    for (const auto& [day, index] : due) queue_.push_back(index);
    queue_.insert(queue_.end(), fresh.begin(), fresh.end());
    dueCount_ = static_cast<int>(due.size());
    newCount_ = static_cast<int>(fresh.size());
}

void TrainingScreen::update() {
    collectResult();
    judge_.update();
    if (drillPhase_ != DrillPhase::Judging || !judge_.takeDone() || !current_ || !data_) return;

    const core::MistakeExample& example = data_->report.examples[*current_];
    const auto& results = judge_.results();
    if (results.size() == 2 && results[0] && results[1]) {
        const Color user = drillPosition_.sideToMove();
        const auto score = [&](const core::Eval& eval) {
            const double white = eval.whiteWinPercent();
            return user == Color::White ? white : 100.0 - white;
        };
        const double tried = score(*results[0]);
        const double best = score(*results[1]);
        char text[200];
        if (best - tried <= kAcceptableLoss) {
            std::snprintf(text, sizeof(text), "Good: your move keeps %.0f%%, the engine's %s %.0f%%.", tried,
                          example.bestSan.c_str(), best);
            finishDrill(true, text);
        } else {
            std::snprintf(text, sizeof(text), "Not quite: your move keeps %.0f%%, the engine's %s %.0f%%.", tried,
                          example.bestSan.c_str(), best);
            finishDrill(false, text);
        }
    } else {
        // No verdict without an engine: show the answer, but leave the schedule alone.
        drillPhase_ = DrillPhase::Answered;
        drillCorrect_ = false;
        drillMessage_ = "The engine could not check that move. It preferred " + example.bestSan + ".";
        if (!judge_.error().empty()) drillMessage_ += " (" + judge_.error() + ")";
    }
}

// --- Drill -------------------------------------------------------------------------

void TrainingScreen::nextDrill() {
    current_.reset();
    drillPhase_ = DrillPhase::Idle;
    drillMessage_.clear();
    triedMove_.reset();
    if (queue_.empty() || !data_) return;
    current_ = queue_.front();
    queue_.erase(queue_.begin());
    if (!drillPosition_.reset(data_->report.examples[*current_].fenBefore)) {
        current_.reset();
        return;
    }
    board_.clearInteraction();
    drillPhase_ = DrillPhase::Solving;
}

void TrainingScreen::answerDrill(core::Move move) {
    if (drillPhase_ != DrillPhase::Solving || !current_) return;
    const core::MistakeExample& example = data_->report.examples[*current_];
    core::Game scratch;
    scratch.setEnforceDraws(false);
    if (!scratch.reset(example.fenBefore) || !scratch.makeMove(move)) return;
    tried_ = scratch.history().back().uci;
    triedMove_ = move;
    if (settings_.soundEnabled) {
        assets_.play(scratch.history().back().captured.empty() ? app::SoundId::Move : app::SoundId::Capture, settings_.soundVolume);
    }
    if (tried_ == example.best) {
        finishDrill(true, "Correct: " + example.bestSan + " is the engine's move.");
    } else if (tried_ == example.played) {
        finishDrill(false, "That is the move from your game. The engine wanted " + example.bestSan + ".");
    } else {
        judge_.judge(example.fenBefore, {tried_, example.best});
        drillPhase_ = DrillPhase::Judging;
    }
}

void TrainingScreen::finishDrill(bool correct, const std::string& message) {
    drillPhase_ = DrillPhase::Answered;
    drillCorrect_ = correct;
    drillMessage_ = message;
    ++sessionTotal_;
    if (correct) ++sessionRight_;
    if (!current_ || !data_) return;
    const int today = core::todayNumber();
    const std::string key = cardKey(data_->report.examples[*current_]);
    const auto existing = cards_.find(key);
    core::CardState card = core::reviewCard(existing == cards_.end() ? core::CardState{} : existing->second, correct, today);
    if (!correct) card.dueDay = today + 1;  // again tomorrow
    cards_[key] = card;
    if (training_ != nullptr) training_->saveMistakeCard(key, card);
    if (existing != cards_.end() && existing->second.isDue(today)) {
        dueCount_ = std::max(0, dueCount_ - 1);
    } else if (existing == cards_.end()) {
        newCount_ = std::max(0, newCount_ - 1);
    }
}

// --- Requests ----------------------------------------------------------------------

std::optional<std::string> TrainingScreen::takePuzzleThemeRequest() { return std::exchange(puzzleThemeRequest_, std::nullopt); }
std::optional<TrainingScreen::GameRequest> TrainingScreen::takeGameRequest() { return std::exchange(gameRequest_, std::nullopt); }
bool TrainingScreen::takeEndgameRequest() { return std::exchange(endgameRequest_, false); }

app::PresenceText TrainingScreen::presence() const {
    app::PresenceText text;
    text.details = drillPhase_ == DrillPhase::Idle ? "Looking at training progress" : "Drilling past mistakes";
    return text;
}

// --- Drawing -----------------------------------------------------------------------

void TrainingScreen::draw() {
    if (dirty_) startBuild();
    ImGui::PushFont(app::titleFont());
    ImGui::TextUnformatted("Training");
    ImGui::PopFont();
    ImGui::SameLine();
    if (worker_.joinable()) {
        ImGui::TextDisabled("Reading your games%s", std::string(static_cast<std::size_t>(ImGui::GetTime() * 3.0) % 4, '.').c_str());
    } else if (data_) {
        ImGui::TextDisabled("From %d reviewed %s of yours%s", data_->report.games, data_->report.games == 1 ? "game" : "games",
                            data_->reviewedGames > kMaxGames ? " (the most recent)" : "");
    }
    if (!data_) return;

    if (ImGui::BeginTabBar("##training-tabs")) {
        const auto flags = [&](int tab) {
            if (wantTab_ != tab) return 0;
            wantTab_ = -1;
            return static_cast<int>(ImGuiTabItemFlags_SetSelected);
        };
        if (ImGui::BeginTabItem("Dashboard", nullptr, flags(0))) {
            drawDashboard();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Weaknesses", nullptr, flags(1))) {
            drawWeaknesses();
            ImGui::EndTabItem();
        }
        char drillLabel[48];
        std::snprintf(drillLabel, sizeof(drillLabel), "Mistake drill (%d)###drill", dueCount_ + newCount_);
        if (ImGui::BeginTabItem(drillLabel, nullptr, flags(2))) {
            drawDrill();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void TrainingScreen::drawDashboard() {
    const Data& data = *data_;
    const core::TrainingReport& report = data.report;
    ImGui::BeginChild("##dashboard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

    if (report.games == 0) {
        ImGui::PushTextWrapPos(std::min(ImGui::GetContentRegionAvail().x, 760.0f));
        ImGui::TextDisabled("No reviewed games of yours yet. Play a game and press \"Review game\", or review library "
                            "games in Game Review: mistakes, patterns and drills appear here once games are reviewed. "
                            "Imported games count when your names are set in Statistics.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }

    // --- Today ---
    ImGui::SeparatorText("Today");
    const int puzzleDue = data.puzzleQueue.due;
    const std::vector<std::pair<Motif, int>> ranked = report.rankedMotifs();
    std::optional<Motif> weakest;
    for (const auto& [motif, count] : ranked) {
        if (*core::motifTheme(motif) != '\0') {
            weakest = motif;
            break;
        }
    }
    bool anything = false;
    if (dueCount_ + newCount_ > 0) {
        anything = true;
        char label[96];
        std::snprintf(label, sizeof(label), "Drill your mistakes  (%d due, %d new)", dueCount_, newCount_);
        if (ImGui::Button(label)) wantTab_ = 2;
    }
    if (puzzleDue > 0) {
        if (anything) ImGui::SameLine();
        anything = true;
        char label[64];
        std::snprintf(label, sizeof(label), "Review %d missed %s", puzzleDue, puzzleDue == 1 ? "puzzle" : "puzzles");
        if (ImGui::Button(label)) puzzleThemeRequest_ = "";
    }
    if (weakest) {
        if (anything) ImGui::SameLine();
        anything = true;
        const std::string label = std::string("Puzzles for your top pattern: ") + core::motifName(*weakest);
        if (ImGui::Button(label.c_str())) puzzleThemeRequest_ = core::motifTheme(*weakest);
        ImGui::SetItemTooltip("Your most common tactical mistake. %s", core::motifAdvice(*weakest));
    }
    if (!anything) ImGui::TextDisabled("Nothing due. Play or review a game to find new things to work on.");
    ImGui::Spacing();

    // --- Tiles ---
    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = (ImGui::GetContentRegionAvail().x - 3.0f * style.ItemSpacing.x) / 4.0f;
    char value[48];
    char detail[128];

    if (report.games > 0) {
        std::snprintf(value, sizeof(value), "%.2f", report.perGame(report.blunders));
        // The recent trend: the last ten games against the rest.
        const std::vector<double>& byGame = report.blundersByGame;
        const std::size_t recent = std::min<std::size_t>(10, byGame.size());
        double recentSum = 0.0;
        for (std::size_t i = byGame.size() - recent; i < byGame.size(); ++i) recentSum += byGame[i];
        std::snprintf(detail, sizeof(detail), "last %zu games: %.2f  \xC2\xB7  mistakes %.2f", recent,
                      recent > 0 ? recentSum / static_cast<double>(recent) : 0.0, report.perGame(report.mistakes));
    } else {
        std::snprintf(value, sizeof(value), "-");
        std::snprintf(detail, sizeof(detail), "no reviewed games");
    }
    tile("##t-blunders", width, "Blunders per game", value, detail);
    ImGui::SameLine();

    if (report.moves > 0) {
        std::snprintf(value, sizeof(value), "%.0f", report.centipawnLoss);
        std::snprintf(detail, sizeof(detail), "average centipawn loss, %d moves", report.moves);
    } else {
        std::snprintf(value, sizeof(value), "-");
        std::snprintf(detail, sizeof(detail), "no reviewed moves");
    }
    tile("##t-acpl", width, "Centipawn loss", value, detail);
    ImGui::SameLine();

    if (data.puzzles.attempts > 0) {
        std::snprintf(value, sizeof(value), "%.0f%%", 100.0 * data.puzzles.solved / data.puzzles.attempts);
        std::snprintf(detail, sizeof(detail), "%d of %d  \xC2\xB7  rating %d", data.puzzles.solved, data.puzzles.attempts,
                      data.puzzles.rating);
    } else {
        std::snprintf(value, sizeof(value), "-");
        std::snprintf(detail, sizeof(detail), "no puzzles tried yet");
    }
    tile("##t-puzzles", width, "Puzzle success", value, detail);
    ImGui::SameLine();

    if (data.endgameAttempts > 0) {
        std::snprintf(value, sizeof(value), "%.0f%%", 100.0 * data.endgameSuccesses / data.endgameAttempts);
        std::snprintf(detail, sizeof(detail), "%d of %d attempts reached the goal", data.endgameSuccesses, data.endgameAttempts);
    } else {
        std::snprintf(value, sizeof(value), "-");
        std::snprintf(detail, sizeof(detail), "no endgames practised yet");
    }
    tile("##t-endgames", width, "Endgame success", value, detail);
    if (data.endgameAttempts == 0 && ImGui::IsItemClicked()) endgameRequest_ = true;
    ImGui::Spacing();

    // --- Two columns: mistake patterns and engine results ---
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    const float rows = ImGui::GetTextLineHeightWithSpacing() * 9.0f + 2.0f * style.WindowPadding.y;

    ImGui::BeginChild("##patterns", ImVec2(half, rows), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted("Most common mistake patterns");
    const int errors = report.mistakes + report.blunders;
    if (ranked.empty()) {
        ImGui::TextDisabled("None yet.");
    } else if (ImGui::BeginTable("##pattern-table", 3, ImGuiTableFlags_SizingFixedFit)) {
        // The label column fits the longest name shown, so the bars never run into it.
        float labelWidth = 0.0f;
        for (std::size_t i = 0; i < ranked.size() && i < 7; ++i) {
            labelWidth = std::max(labelWidth, ImGui::CalcTextSize(core::motifName(ranked[i].first)).x);
        }
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("bar", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000").x);
        for (std::size_t i = 0; i < ranked.size() && i < 7; ++i) {
            const auto& [motif, count] = ranked[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(core::motifName(motif));
            ImGui::TableSetColumnIndex(1);
            bar(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight()),
                errors > 0 ? static_cast<float>(count) / errors : 0.0f,
                motif == Motif::Positional ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : accentU32());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", count);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##engine", ImVec2(half, rows), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::TextUnformatted("Results against the engine");
    if (data.engine.empty()) {
        ImGui::TextDisabled("Finished games from Play appear here.");
    } else if (ImGui::BeginTable("##engine-table", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Opponent");
        ImGui::TableSetupColumn("Games", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableSetupColumn("Won / drawn / lost", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Score");
        tableHeaders();
        for (const EngineResults& row : data.engine) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.label.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", row.games);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d / %d / %d", row.wins, row.draws, row.losses);
            ImGui::TableSetColumnIndex(3);
            const float score = row.games > 0 ? (row.wins + 0.5f * row.draws) / static_cast<float>(row.games) : 0.0f;
            bar(ImVec2(ImGui::GetContentRegionAvail().x - 44.0f, ImGui::GetTextLineHeight()), score, accentU32());
            ImGui::SameLine();
            ImGui::Text("%.0f%%", score * 100.0f);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    // --- Errors by phase ---
    if (errors > 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("Mistakes and blunders by phase:");
        for (int phase = 0; phase < core::kPhaseCount; ++phase) {
            ImGui::SameLine();
            ImGui::Text("%s %d", core::phaseName(static_cast<core::GamePhase>(phase)),
                        report.phaseErrors[static_cast<std::size_t>(phase)]);
        }
    }
    ImGui::EndChild();
}

void TrainingScreen::drawWeaknesses() {
    const core::TrainingReport& report = data_->report;
    const std::vector<std::pair<Motif, int>> ranked = report.rankedMotifs();
    if (ranked.empty()) {
        ImGui::TextDisabled("No mistakes or blunders found in your reviewed games yet.");
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Each mistake is checked for the tactic the engine's move had (missed) and the one your move "
                        "let the opponent play (allowed). One mistake can show several patterns. Click a pattern to "
                        "list its examples.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float left = std::min(ImGui::GetContentRegionAvail().x * 0.5f, 620.0f);
    const int errors = report.mistakes + report.blunders;

    ImGui::BeginChild("##motifs", ImVec2(left, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (ImGui::BeginTable("##motif-table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Pattern");
        ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        tableHeaders();
        for (const auto& [motif, count] : ranked) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(motif));
            if (ImGui::Selectable(core::motifName(motif), selectedMotif_ == static_cast<int>(motif))) {
                selectedMotif_ = selectedMotif_ == static_cast<int>(motif) ? -1 : static_cast<int>(motif);
            }
            ImGui::SetItemTooltip("%s", core::motifAdvice(motif));
            ImGui::TableSetColumnIndex(1);
            bar(ImVec2(90.0f, ImGui::GetTextLineHeight()), errors > 0 ? static_cast<float>(count) / errors : 0.0f,
                motif == Motif::Positional ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : accentU32());
            ImGui::SameLine();
            ImGui::Text("%d", count);
            ImGui::TableSetColumnIndex(2);
            const char* theme = core::motifTheme(motif);
            if (*theme != '\0') {
                if (ImGui::SmallButton("Puzzles")) puzzleThemeRequest_ = theme;
                ImGui::SetItemTooltip("Practise this pattern with puzzles");
                ImGui::SameLine();
            }
            if (ImGui::SmallButton("Drill")) {
                drillMotif_ = static_cast<int>(motif);
                buildDrillQueue();
                nextDrill();
                wantTab_ = 2;
            }
            ImGui::SetItemTooltip("Drill your own mistakes with this pattern");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##examples", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (selectedMotif_ >= 0) {
        ImGui::TextUnformatted(core::motifName(static_cast<Motif>(selectedMotif_)));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", core::motifAdvice(static_cast<Motif>(selectedMotif_)));
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextUnformatted("All mistakes and blunders, newest first");
    }
    ImGui::Separator();
    if (ImGui::BeginTable("##example-table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Move");
        ImGui::TableSetupColumn("Game");
        ImGui::TableSetupColumn("Better");
        tableHeaders();
        int shown = 0;
        for (std::size_t i = report.examples.size(); i-- > 0 && shown < 200;) {
            const core::MistakeExample& example = report.examples[i];
            if (selectedMotif_ >= 0 && std::find(example.motifs.begin(), example.motifs.end(), static_cast<Motif>(selectedMotif_)) ==
                                           example.motifs.end()) {
                continue;
            }
            ++shown;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(i));
            if (tableRow(example.label.c_str(), false).clicked) gameRequest_ = GameRequest{example.gameId, data_->examplePlies[i]};
            if (ImGui::IsItemHovered()) {
                std::string motifs;
                for (const Motif motif : example.motifs) motifs += std::string(motifs.empty() ? "" : ", ") + core::motifName(motif);
                ImGui::SetTooltip("%s\nOpen the game in Game Review at this move", motifs.c_str());
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("vs %s  \xC2\xB7  %s", example.opponent.c_str(), example.date.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(example.bestSan.c_str());
        }
        ImGui::EndTable();
    }
    (void)style;
    ImGui::EndChild();
}

void TrainingScreen::drawDrill() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const BoardLayout layout = boardLayout(0.34f, 300.0f, 460.0f);
    const float panelWidth = layout.panelWidth;
    const float boardSize = layout.boardSize;

    if (!current_ && drillPhase_ == DrillPhase::Idle && !queue_.empty()) nextDrill();
    const core::MistakeExample* example = current_ && data_ ? &data_->report.examples[*current_] : nullptr;
    BoardOptions options = boardOptionsFrom(settings_);
    options.flipped = drillPosition_.sideToMove() == Color::Black;
    options.interactive = drillPhase_ == DrillPhase::Solving;
    if (example && drillPhase_ == DrillPhase::Answered) {
        if (const auto best = core::parseUciMove(example->best)) options.arrows.push_back(BoardArrow{best->from, best->to, kBestArrow});
        if (const auto played = core::parseUciMove(example->played); played && example->played != tried_) {
            options.arrows.push_back(BoardArrow{played->from, played->to, kPlayedArrow});
        }
        if (triedMove_ && tried_ != example->best) options.arrows.push_back(BoardArrow{triedMove_->from, triedMove_->to, kTriedArrow});
    } else if (triedMove_ && drillPhase_ == DrillPhase::Judging) {
        options.arrows.push_back(BoardArrow{triedMove_->from, triedMove_->to, kTriedArrow});
    }
    ImGui::SetCursorPosX(layout.boardX);
    if (!example) {
        // An empty board would look broken; show the start position, not interactive.
        static const core::Game start;
        board_.draw(start, assets_, BoardOptions{boardOptionsFrom(settings_)}, boardSize);
    } else {
        const std::optional<core::Move> move = board_.draw(drillPosition_, assets_, options, boardSize);
        if (move) answerDrill(*move);
    }
    ImGui::SameLine(layout.panelX);

    ImGui::BeginChild("##drill-panel", ImVec2(panelWidth, boardSize), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::TextUnformatted("Mistake drill");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Positions from your reviewed games where you went wrong. Find a better move; the engine "
                        "accepts any move about as good as its own. Missed ones come back tomorrow, solved ones "
                        "later and later.");
    ImGui::PopTextWrapPos();
    ImGui::Text("%d due  \xC2\xB7  %d new", dueCount_, newCount_);
    if (drillMotif_ >= 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(only \"%s\")", core::motifName(static_cast<Motif>(drillMotif_)));
        ImGui::SameLine();
        if (ImGui::SmallButton("Show all")) {
            drillMotif_ = -1;
            buildDrillQueue();
        }
    }
    if (sessionTotal_ > 0) ImGui::TextDisabled("This session: %d of %d right", sessionRight_, sessionTotal_);
    ImGui::Separator();

    if (example) {
        ImGui::Text("Your game vs %s", example->opponent.c_str());
        ImGui::TextDisabled("%s  \xC2\xB7  %s", example->date.c_str(), core::phaseName(example->phase));
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        if (drillPhase_ == DrillPhase::Solving) {
            ImGui::TextColored(kAccentColor, "%s to move. You played %s in the game; find a better move.",
                               drillPosition_.sideToMove() == Color::White ? "White" : "Black", example->label.c_str());
        } else if (drillPhase_ == DrillPhase::Judging) {
            ImGui::TextUnformatted(("Checking your move" + std::string(static_cast<std::size_t>(ImGui::GetTime() * 3.0) % 4, '.')).c_str());
        } else if (drillPhase_ == DrillPhase::Answered) {
            ImGui::TextColored(drillCorrect_ ? kAccentColor : kErrorColor, "%s", drillMessage_.c_str());
            std::string motifs;
            for (const Motif motif : example->motifs) motifs += std::string(motifs.empty() ? "" : ", ") + core::motifName(motif);
            ImGui::TextDisabled("Pattern: %s", motifs.c_str());
            ImGui::TextDisabled("Green: the engine's move. Red: your move in the game.");
        }
        ImGui::PopTextWrapPos();
    } else if (queue_.empty()) {
        ImGui::TextDisabled(data_ && data_->report.examples.empty() ? "No mistakes to drill yet: review some of your games first."
                                                                     : "All done for today.");
    }

    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing() -
                                                                style.WindowPadding.y));
    const float half = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    if (drillPhase_ == DrillPhase::Solving) {
        if (ImGui::Button("Show the answer", ImVec2(half, 0.0f)) && example) {
            triedMove_.reset();
            tried_.clear();
            finishDrill(false, "The engine's move was " + example->bestSan + ".");
        }
    } else {
        ImGui::BeginDisabled(queue_.empty() || drillPhase_ == DrillPhase::Judging);
        if (ImGui::Button(current_ ? "Next" : "Start", ImVec2(half, 0.0f))) nextDrill();
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!example);
    if (ImGui::Button("Open the game", ImVec2(half, 0.0f)) && example) {
        gameRequest_ = GameRequest{example->gameId, data_->examplePlies[*current_]};
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
}

}  // namespace ui

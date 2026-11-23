#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "app/RichPresence.h"
#include "app/Settings.h"
#include "chess/Game.h"
#include "chess/GameTree.h"
#include "chess/OpeningBook.h"
#include "chess/Repertoire.h"
#include "storage/TrainingStore.h"
#include "ui/BoardView.h"
#include "ui/MoveTreeView.h"

namespace app {
class Assets;
}

namespace ui {

// The Openings section: build repertoires as move trees and drill them with spaced repetition.
class OpeningsScreen {
public:
    // `store` may be null when the database could not be opened.
    OpeningsScreen(app::Settings& settings, app::Assets& assets, storage::TrainingStore* store,
                   const core::OpeningBook* book);

    // Plays the opponent's moves during training. Call once per frame.
    void update();
    // Draws the section into the current ImGui window.
    void draw();

    // The repertoire the user asked to look at in Analysis, if any (cleared by the call).
    std::optional<core::GameRecord> takeAnalysisRequest();
    app::PresenceText presence() const;

    // Test aids: start a training session / answer every question correctly.
    void requestTraining() { wantTraining_ = true; }
    void setAutoAnswer(bool enabled) { autoAnswer_ = enabled; }

private:
    using Clock = std::chrono::steady_clock;

    struct Counts {
        int positions = 0;  // positions with a move to know
        int learned = 0;    // answered correctly last time
        int due = 0;        // new or scheduled for today
    };

    // Repertoire management.
    void reloadList();
    void select(storage::RepertoireId id);
    void saveTree();
    void importPgn();
    void exportPgn();
    Counts counts() const;
    const storage::RepertoireInfo* selectedInfo() const;

    // Editing.
    void goTo(core::NodeId id);
    void playMove(core::Move move);
    void removeNode(core::NodeId id);

    // Training.
    void startTraining(bool dueOnly);
    void stopTraining();
    bool nextLine();
    void answer(core::Move move);
    void reviewCurrent(bool correct);

    void drawSidePanel(float width, float height);
    void drawEditPanel();
    void drawTrainPanel();
    void drawRepertoireDialog();

    app::Settings& settings_;
    app::Assets& assets_;
    storage::TrainingStore* store_;
    const core::OpeningBook* book_;

    std::vector<storage::RepertoireInfo> repertoires_;
    storage::RepertoireId selected_ = 0;
    core::GameTree tree_;
    std::unordered_map<std::string, core::CardState> cards_;
    core::NodeId current_ = core::kRootNode;
    core::Game position_;
    BoardView board_;
    MoveTreeView treeView_;
    bool flipped_ = false;
    std::string notice_;
    core::NodeId pendingRemove_ = core::kNoNode;
    core::NodeId pendingPromote_ = core::kNoNode;
    core::NodeId pendingMainLine_ = core::kNoNode;

    // Training session.
    bool training_ = false;
    std::vector<std::vector<core::NodeId>> queue_;  // lines still to be played
    std::vector<core::NodeId> line_;                // the line being played
    std::size_t lineIndex_ = 0;                     // next node of `line_`
    std::unordered_set<std::string> reviewed_;      // positions already graded in this session
    bool missedHere_ = false;                       // a wrong answer was given at the current position
    int sessionCorrect_ = 0;
    int sessionWrong_ = 0;
    std::string feedback_;
    Clock::time_point replyAt_{};
    std::mt19937 rng_{std::random_device{}()};

    // "New" / "Edit" dialog.
    bool wantDialog_ = false;
    bool dialogIsNew_ = true;
    bool wantDeleteDialog_ = false;
    std::array<char, 96> dialogName_{};
    std::array<char, 64> dialogFolder_{};
    int dialogColor_ = 0;

    std::optional<core::GameRecord> analysisRequest_;
    bool wantTraining_ = false;
    bool autoAnswer_ = false;
    Clock::time_point autoAt_{};
};

}  // namespace ui

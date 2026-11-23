#pragma once

#include <string>
#include <unordered_map>

#include "chess/Endgames.h"
#include "storage/TrainingStore.h"

namespace ui {

// The Endgames section: a catalogue of training positions with the user's progress.
// Picking one hands it to the Play section, where it is played against the engine.
class EndgameScreen {
public:
    // `store` may be null; progress is then not shown.
    explicit EndgameScreen(storage::TrainingStore* store);

    // Draws the section into the current ImGui window.
    void draw();

    // Reloads the progress on the next frame (call after a result was recorded).
    void markDirty() { dirty_ = true; }

    // The position the user chose to practise, if any (cleared by the call).
    const core::EndgamePosition* takeStartRequest();

private:
    storage::TrainingStore* store_;
    std::unordered_map<std::string, storage::EndgameProgress> progress_;
    bool dirty_ = true;
    const core::EndgamePosition* startRequest_ = nullptr;
};

}  // namespace ui

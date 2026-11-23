#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "chess/GameTree.h"
#include "chess/Repertoire.h"

namespace storage {

using RepertoireId = std::int64_t;

struct RepertoireInfo {
    RepertoireId id = 0;
    std::string name;
    core::Color color = core::Color::White;  // the side the user plays
    std::string folder;                      // free-form group name; may be empty
};

struct EndgameProgress {
    int attempts = 0;
    int successes = 0;
};

// Opening repertoires with their spaced-repetition cards, and endgame training results.
class TrainingStore {
public:
    static std::unique_ptr<TrainingStore> open(const std::filesystem::path& file, std::string* error = nullptr);
    ~TrainingStore();
    TrainingStore(const TrainingStore&) = delete;
    TrainingStore& operator=(const TrainingStore&) = delete;

    // --- Repertoires --------------------------------------------------------------
    std::optional<RepertoireId> createRepertoire(std::string_view name, core::Color color, std::string_view folder = {});
    bool updateRepertoire(const RepertoireInfo& info);
    // Removes the repertoire together with its cards.
    bool removeRepertoire(RepertoireId id);
    std::vector<RepertoireInfo> repertoires() const;

    // The move tree of a repertoire (an empty tree if nothing was saved yet).
    core::GameTree loadTree(RepertoireId id) const;
    bool saveTree(RepertoireId id, const core::GameTree& tree);

    // --- Cards --------------------------------------------------------------------
    // Cards are keyed by position (see core::positionKey), so a position reached by
    // different move orders is learned once.
    std::unordered_map<std::string, core::CardState> cards(RepertoireId id) const;
    bool saveCard(RepertoireId id, const std::string& positionKey, const core::CardState& card);

    // Adds the bundled repertoires (PGN files with a [Side "White"|"Black"] tag) in the folder
    // "Starter" the first time it is called for this database. Returns how many were added.
    int addStarterRepertoires(const std::filesystem::path& directory);

    // --- Mistake drill ---------------------------------------------------------------
    // Cards for positions in which the user went wrong, keyed by the caller.
    std::unordered_map<std::string, core::CardState> mistakeCards() const;
    bool saveMistakeCard(const std::string& key, const core::CardState& card);

    // --- Endgames -----------------------------------------------------------------
    void recordEndgame(const std::string& positionId, bool success);
    std::unordered_map<std::string, EndgameProgress> endgameProgress() const;

private:
    TrainingStore();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace storage

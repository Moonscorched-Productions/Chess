#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "chess/GameTree.h"
#include "storage/GameStore.h"

namespace ui {

// The Game Library section: the list of stored games with search, import and export.
class LibraryScreen {
public:
    struct OpenRequest {
        storage::GameId id = 0;
        core::GameRecord record;
    };

    // `store` may be null when the database could not be opened; `openError` then says why.
    // `databaseFile` lets the background import open its own connection.
    LibraryScreen(storage::GameStore* store, std::filesystem::path databaseFile, std::string openError);
    ~LibraryScreen();
    LibraryScreen(const LibraryScreen&) = delete;
    LibraryScreen& operator=(const LibraryScreen&) = delete;

    // Picks up the result of a finished import. Call once per frame.
    void update();
    // Draws the section into the current ImGui window.
    void draw();

    // Reloads the list on the next frame (call after games were added elsewhere).
    void markDirty() { dirty_ = true; }

    // The game the user asked to open in Analysis, if any (cleared by the call).
    std::optional<OpenRequest> takeOpenRequest();
    // The game the user asked to review, if any (cleared by the call).
    std::optional<OpenRequest> takeReviewRequest();
    // True once after games were imported or deleted here.
    bool consumeChanged();

private:
    void reload();
    void openGame(storage::GameId id, bool review = false);
    void startImport(const std::filesystem::path& file);
    void finishImport();
    void exportGames(const std::vector<storage::GameId>& ids);

    storage::GameStore* store_;
    std::filesystem::path databaseFile_;
    std::string openError_;

    std::vector<storage::GameSummary> games_;
    int total_ = 0;
    bool dirty_ = true;
    storage::GameId selected_ = 0;
    std::array<char, 128> search_{};
    int resultFilter_ = 0;
    std::string notice_;
    bool wantDeleteDialog_ = false;
    std::optional<OpenRequest> openRequest_;
    std::optional<OpenRequest> reviewRequest_;
    bool changed_ = false;

    // Background import.
    std::thread importThread_;
    std::atomic<bool> importRunning_{false};
    std::atomic<bool> importDone_{false};
    std::atomic<bool> importCancel_{false};
    std::atomic<int> importProgress_{0};
    storage::ImportResult importResult_;  // written by the worker, read after it is done
};

}  // namespace ui

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chess/GameTree.h"

namespace storage {

using GameId = std::int64_t;

// One row of the game list (everything except the moves).
struct GameSummary {
    GameId id = 0;
    std::string white;
    std::string black;
    std::string result;  // "1-0", "0-1", "1/2-1/2" or "*"
    std::string date;    // PGN date, e.g. "2026.01.31"
    std::string event;
    int moves = 0;       // full moves of the main line
    std::string source;  // "play", "analysis" or "import"
};

struct GameFilter {
    std::string text;    // matches player and event names; empty = everything
    std::string result;  // exact result; empty = any
    int limit = 500;
};

// How long the app has been open.
struct Usage {
    std::int64_t totalSeconds = 0;
    std::int64_t todaySeconds = 0;
    int days = 0;  // days on which it was used
};

struct ImportResult {
    int imported = 0;
    int skipped = 0;  // games that could not be read
    std::string error;  // set when the file itself could not be processed
};

// The personal game database: PGN text plus searchable header fields, in SQLite.
// Each object owns one connection and must be used from one thread at a time;
// open a second store on the same file for work on another thread.
class GameStore {
public:
    // Opens (and if necessary creates) the database. Use ":memory:" for a temporary one.
    static std::unique_ptr<GameStore> open(const std::filesystem::path& file, std::string* error = nullptr);

    ~GameStore();
    GameStore(const GameStore&) = delete;
    GameStore& operator=(const GameStore&) = delete;

    // Stores a game. Returns its id, or nullopt on failure.
    std::optional<GameId> add(const core::GameRecord& record, std::string_view source);
    // Replaces the stored game `id`.
    bool update(GameId id, const core::GameRecord& record);
    bool remove(GameId id);
    // Adds time the app was open to today's total.
    void addUsage(std::int64_t seconds);
    Usage usage() const;

    // Newest games first.
    std::vector<GameSummary> list(const GameFilter& filter = {}) const;
    int count() const;
    // The PGN text of a game.
    std::optional<std::string> pgn(GameId id) const;

    // Imports every game of a PGN file in one transaction. `progress`, if given,
    // is updated with the number of games handled so far; setting `cancel` stops early.
    ImportResult importFile(const std::filesystem::path& file, std::atomic<int>* progress = nullptr,
                            const std::atomic<bool>* cancel = nullptr);
    // Writes the listed games (or all games when `ids` is empty) to a PGN file.
    bool exportFile(const std::filesystem::path& file, const std::vector<GameId>& ids = {}) const;

    const std::string& lastError() const { return lastError_; }

private:
    GameStore();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    mutable std::string lastError_;
};

}  // namespace storage

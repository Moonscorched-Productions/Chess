#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "chess/Puzzle.h"

namespace storage {

struct PuzzleFilter {
    int minRating = 0;
    int maxRating = 4000;
    std::string theme;         // a Lichess theme name such as "fork"; empty = any
    bool onlyUnsolved = true;  // skip puzzles that were already solved
};

struct PuzzleAttempt {
    std::string puzzleId;
    std::string when;  // local date and time
    bool solved = false;
    double seconds = 0.0;
    bool rated = false;
    int puzzleRating = 0;
    int ratingAfter = 0;  // the user's rating after this attempt
};

struct PuzzleStats {
    int attempts = 0;
    int solved = 0;
    int streak = 0;      // solved in a row, counting back from the latest attempt
    int bestStreak = 0;
    int rating = 1500;
    int ratedAttempts = 0;
};

struct PuzzleImportResult {
    int imported = 0;
    int skipped = 0;
    std::string error;
};

// Puzzles, the attempts made at them and the user's puzzle rating, in SQLite.
// One connection per object; open a second store on the same file for a background import.
class PuzzleStore {
public:
    static std::unique_ptr<PuzzleStore> open(const std::filesystem::path& file, std::string* error = nullptr);
    ~PuzzleStore();
    PuzzleStore(const PuzzleStore&) = delete;
    PuzzleStore& operator=(const PuzzleStore&) = delete;

    int count() const;
    bool add(const core::Puzzle& puzzle);
    std::optional<core::Puzzle> get(const std::string& id) const;

    // A random puzzle matching the filter. `seed` picks where in the database to look,
    // so a caller-supplied random number gives a different puzzle each time.
    std::optional<core::Puzzle> random(const PuzzleFilter& filter, std::uint64_t seed) const;
    // The same puzzle for everyone asking on the same day.
    std::optional<core::Puzzle> daily(int dayNumber) const;
    // A puzzle that was failed and has not been solved since (oldest failure first).
    std::optional<core::Puzzle> nextFailed() const;
    int failedCount() const;

    // Records an attempt. A rated attempt moves the user's rating; returns the new rating.
    int recordAttempt(const core::Puzzle& puzzle, bool solved, double seconds, bool rated);

    PuzzleStats stats() const;
    // Newest first.
    std::vector<PuzzleAttempt> history(int limit = 100) const;
    // The user's rating after each rated attempt, oldest first.
    std::vector<int> ratingHistory(int limit = 200) const;

    // Imports puzzles in the Lichess CSV format (PuzzleId,FEN,Moves,Rating,...,Themes).
    // Existing ids are left untouched. `progress` counts the lines handled.
    PuzzleImportResult importCsv(std::istream& input, std::atomic<int>* progress = nullptr,
                                 const std::atomic<bool>* cancel = nullptr);
    PuzzleImportResult importCsvFile(const std::filesystem::path& file, std::atomic<int>* progress = nullptr,
                                     const std::atomic<bool>* cancel = nullptr);

private:
    PuzzleStore();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace storage

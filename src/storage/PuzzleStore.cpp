#include "storage/PuzzleStore.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <istream>
#include <string_view>

#include "storage/Sqlite.h"

namespace storage {

namespace {

using detail::Statement;

constexpr const char* kSchema = R"(
CREATE TABLE IF NOT EXISTS puzzles (
    id     TEXT PRIMARY KEY,
    fen    TEXT NOT NULL,
    moves  TEXT NOT NULL,
    rating INTEGER NOT NULL,
    themes TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS puzzles_rating ON puzzles(rating);
CREATE TABLE IF NOT EXISTS puzzle_attempts (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    puzzle_id     TEXT NOT NULL,
    at            TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
    solved        INTEGER NOT NULL,
    seconds       REAL NOT NULL DEFAULT 0,
    rated         INTEGER NOT NULL DEFAULT 0,
    puzzle_rating INTEGER NOT NULL DEFAULT 0,
    rating_after  INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS puzzle_attempts_puzzle ON puzzle_attempts(puzzle_id);
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
)";

constexpr const char* kPuzzleColumns = "id, fen, moves, rating, themes";

core::Puzzle readPuzzle(const Statement& statement) {
    core::Puzzle puzzle;
    puzzle.id = statement.text(0);
    puzzle.fen = statement.text(1);
    puzzle.moves = core::splitMoves(statement.text(2));
    puzzle.rating = static_cast<int>(statement.integer(3));
    // Themes are stored with a space on both sides so " fork " can be matched exactly.
    std::string themes = statement.text(4);
    while (!themes.empty() && themes.front() == ' ') themes.erase(themes.begin());
    while (!themes.empty() && themes.back() == ' ') themes.pop_back();
    puzzle.themes = std::move(themes);
    return puzzle;
}

std::string joinMoves(const std::vector<std::string>& moves) {
    std::string text;
    for (const std::string& move : moves) {
        if (!text.empty()) text += ' ';
        text += move;
    }
    return text;
}

}  // namespace

struct PuzzleStore::Impl {
    std::unique_ptr<detail::Connection> connection;

    std::pair<std::int64_t, std::int64_t> rowRange() const {
        Statement statement(*connection, "SELECT MIN(rowid), MAX(rowid) FROM puzzles");
        if (!statement.valid() || !statement.step() || statement.isNull(0)) return {0, -1};
        return {statement.integer(0), statement.integer(1)};
    }

    int metaInt(const char* key, int fallback) const {
        Statement statement(*connection, "SELECT value FROM meta WHERE key=?1");
        if (!statement.valid()) return fallback;
        statement.bind(1, std::string_view(key));
        if (!statement.step()) return fallback;
        const std::string text = statement.text(0);
        int value = fallback;
        std::from_chars(text.data(), text.data() + text.size(), value);
        return value;
    }

    void setMetaInt(const char* key, int value) {
        Statement statement(*connection, "INSERT INTO meta (key, value) VALUES (?1, ?2) "
                                         "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
        if (!statement.valid()) return;
        statement.bind(1, std::string_view(key));
        statement.bind(2, std::string_view(std::to_string(value)));
        statement.run();
    }
};

PuzzleStore::PuzzleStore() : impl_(std::make_unique<Impl>()) {}
PuzzleStore::~PuzzleStore() = default;

std::unique_ptr<PuzzleStore> PuzzleStore::open(const std::filesystem::path& file, std::string* error) {
    std::unique_ptr<PuzzleStore> store(new PuzzleStore());
    store->impl_->connection = detail::Connection::open(file, error);
    if (!store->impl_->connection) return nullptr;
    if (!store->impl_->connection->exec(kSchema)) {
        if (error != nullptr) *error = "Could not prepare the puzzle database: " + store->impl_->connection->message();
        return nullptr;
    }
    return store;
}

int PuzzleStore::count() const {
    Statement statement(*impl_->connection, "SELECT COUNT(*) FROM puzzles");
    if (!statement.valid() || !statement.step()) return 0;
    return static_cast<int>(statement.integer(0));
}

bool PuzzleStore::add(const core::Puzzle& puzzle) {
    Statement statement(*impl_->connection,
                        "INSERT OR IGNORE INTO puzzles (id, fen, moves, rating, themes) VALUES (?1, ?2, ?3, ?4, ?5)");
    if (!statement.valid() || puzzle.id.empty()) return false;
    statement.bind(1, puzzle.id);
    statement.bind(2, puzzle.fen);
    statement.bind(3, joinMoves(puzzle.moves));
    statement.bind(4, puzzle.rating);
    statement.bind(5, " " + puzzle.themes + " ");
    return statement.run() && impl_->connection->changes() > 0;
}

std::optional<core::Puzzle> PuzzleStore::get(const std::string& id) const {
    const std::string sql = std::string("SELECT ") + kPuzzleColumns + " FROM puzzles WHERE id=?1";
    Statement statement(*impl_->connection, sql.c_str());
    if (!statement.valid()) return std::nullopt;
    statement.bind(1, id);
    if (!statement.step()) return std::nullopt;
    return readPuzzle(statement);
}

std::optional<core::Puzzle> PuzzleStore::random(const PuzzleFilter& filter, std::uint64_t seed) const {
    const auto [first, last] = impl_->rowRange();
    if (last < first) return std::nullopt;

    // Start at a random row and take the first match after it. This stays fast on a
    // database with millions of puzzles, where sorting all matches randomly would not.
    const std::string sql = std::string("SELECT ") + kPuzzleColumns +
                            " FROM puzzles WHERE rowid >= ?1 AND rating BETWEEN ?2 AND ?3"
                            " AND (?4 = '' OR themes LIKE ?5 ESCAPE '\\')"
                            " AND (?6 = 0 OR id NOT IN (SELECT puzzle_id FROM puzzle_attempts WHERE solved = 1))"
                            " ORDER BY rowid LIMIT 1";
    std::string pattern = "% ";
    for (const char c : filter.theme) {
        if (c == '%' || c == '_' || c == '\\') pattern += '\\';
        pattern += c;
    }
    pattern += " %";

    const std::int64_t span = last - first + 1;
    const std::int64_t start = first + static_cast<std::int64_t>(seed % static_cast<std::uint64_t>(span));
    // Second pass from the beginning covers the rows before the random start.
    for (const std::int64_t from : {start, first}) {
        Statement statement(*impl_->connection, sql.c_str());
        if (!statement.valid()) return std::nullopt;
        statement.bind(1, from);
        statement.bind(2, filter.minRating);
        statement.bind(3, filter.maxRating);
        statement.bind(4, filter.theme);
        statement.bind(5, pattern);
        statement.bind(6, filter.onlyUnsolved ? 1 : 0);
        if (statement.step()) return readPuzzle(statement);
    }
    return std::nullopt;
}

std::optional<core::Puzzle> PuzzleStore::daily(int dayNumber) const {
    PuzzleFilter filter;
    filter.minRating = 1200;
    filter.maxRating = 2000;
    filter.onlyUnsolved = false;
    // A fixed scramble of the day number, so consecutive days land far apart.
    const std::uint64_t seed = static_cast<std::uint64_t>(dayNumber) * 0x9E3779B97F4A7C15ULL;
    if (std::optional<core::Puzzle> puzzle = random(filter, seed >> 16)) return puzzle;
    filter.minRating = 0;
    filter.maxRating = 4000;
    return random(filter, seed >> 16);
}

std::optional<core::Puzzle> PuzzleStore::nextFailed() const {
    const std::string sql = std::string("SELECT ") + kPuzzleColumns +
                            " FROM puzzles WHERE id = ("
                            "  SELECT a.puzzle_id FROM puzzle_attempts a WHERE a.solved = 0"
                            "  AND NOT EXISTS (SELECT 1 FROM puzzle_attempts b WHERE b.puzzle_id = a.puzzle_id AND b.solved = 1)"
                            "  ORDER BY a.id LIMIT 1)";
    Statement statement(*impl_->connection, sql.c_str());
    if (!statement.valid() || !statement.step()) return std::nullopt;
    return readPuzzle(statement);
}

int PuzzleStore::failedCount() const {
    Statement statement(*impl_->connection,
                        "SELECT COUNT(DISTINCT a.puzzle_id) FROM puzzle_attempts a WHERE a.solved = 0 "
                        "AND NOT EXISTS (SELECT 1 FROM puzzle_attempts b WHERE b.puzzle_id = a.puzzle_id AND b.solved = 1)");
    if (!statement.valid() || !statement.step()) return 0;
    return static_cast<int>(statement.integer(0));
}

int PuzzleStore::recordAttempt(const core::Puzzle& puzzle, bool solved, double seconds, bool rated) {
    int rating = impl_->metaInt("puzzle_rating", 1500);
    if (rated) {
        const int ratedSoFar = impl_->metaInt("puzzle_rated_attempts", 0);
        rating = core::updatedRating(rating, puzzle.rating, solved, ratedSoFar);
        impl_->setMetaInt("puzzle_rating", rating);
        impl_->setMetaInt("puzzle_rated_attempts", ratedSoFar + 1);
    }
    Statement statement(*impl_->connection,
                        "INSERT INTO puzzle_attempts (puzzle_id, solved, seconds, rated, puzzle_rating, rating_after) "
                        "VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
    if (statement.valid()) {
        statement.bind(1, puzzle.id);
        statement.bind(2, solved ? 1 : 0);
        statement.bind(3, seconds);
        statement.bind(4, rated ? 1 : 0);
        statement.bind(5, puzzle.rating);
        statement.bind(6, rating);
        statement.run();
    }
    return rating;
}

PuzzleStats PuzzleStore::stats() const {
    PuzzleStats stats;
    stats.rating = impl_->metaInt("puzzle_rating", 1500);
    stats.ratedAttempts = impl_->metaInt("puzzle_rated_attempts", 0);

    Statement statement(*impl_->connection, "SELECT solved FROM puzzle_attempts ORDER BY id");
    if (!statement.valid()) return stats;
    int run = 0;
    while (statement.step()) {
        ++stats.attempts;
        if (statement.integer(0) != 0) {
            ++stats.solved;
            ++run;
            stats.bestStreak = std::max(stats.bestStreak, run);
        } else {
            run = 0;
        }
    }
    stats.streak = run;
    return stats;
}

std::vector<PuzzleAttempt> PuzzleStore::history(int limit) const {
    std::vector<PuzzleAttempt> attempts;
    Statement statement(*impl_->connection,
                        "SELECT puzzle_id, at, solved, seconds, rated, puzzle_rating, rating_after "
                        "FROM puzzle_attempts ORDER BY id DESC LIMIT ?1");
    if (!statement.valid()) return attempts;
    statement.bind(1, limit > 0 ? limit : 100);
    while (statement.step()) {
        PuzzleAttempt attempt;
        attempt.puzzleId = statement.text(0);
        attempt.when = statement.text(1);
        attempt.solved = statement.integer(2) != 0;
        attempt.seconds = statement.real(3);
        attempt.rated = statement.integer(4) != 0;
        attempt.puzzleRating = static_cast<int>(statement.integer(5));
        attempt.ratingAfter = static_cast<int>(statement.integer(6));
        attempts.push_back(std::move(attempt));
    }
    return attempts;
}

std::vector<int> PuzzleStore::ratingHistory(int limit) const {
    std::vector<int> ratings;
    Statement statement(*impl_->connection,
                        "SELECT rating_after FROM (SELECT id, rating_after FROM puzzle_attempts WHERE rated = 1 "
                        "ORDER BY id DESC LIMIT ?1) ORDER BY id");
    if (!statement.valid()) return ratings;
    statement.bind(1, limit > 0 ? limit : 200);
    while (statement.step()) ratings.push_back(static_cast<int>(statement.integer(0)));
    return ratings;
}

PuzzleImportResult PuzzleStore::importCsv(std::istream& input, std::atomic<int>* progress, const std::atomic<bool>* cancel) {
    PuzzleImportResult result;
    Statement statement(*impl_->connection,
                        "INSERT OR IGNORE INTO puzzles (id, fen, moves, rating, themes) VALUES (?1, ?2, ?3, ?4, ?5)");
    if (!statement.valid() || !impl_->connection->exec("BEGIN IMMEDIATE;")) {
        result.error = "Database error: " + impl_->connection->message();
        return result;
    }

    std::string line;
    int handled = 0;
    while (std::getline(input, line)) {
        if (cancel != nullptr && cancel->load()) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        ++handled;
        if (progress != nullptr && (handled & 0xFF) == 0) progress->store(handled);

        // PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,...
        std::string_view fields[8];
        std::string_view rest = line;
        int found = 0;
        for (; found < 8; ++found) {
            const std::size_t comma = rest.find(',');
            fields[found] = rest.substr(0, comma);
            if (comma == std::string_view::npos) {
                ++found;
                break;
            }
            rest = rest.substr(comma + 1);
        }
        int rating = 0;
        const bool ratingOk = found >= 4 && std::from_chars(fields[3].data(), fields[3].data() + fields[3].size(), rating).ec == std::errc();
        const bool usable = ratingOk && !fields[0].empty() && fields[1].find('/') != std::string_view::npos &&
                            fields[2].find(' ') != std::string_view::npos;
        if (!usable) {
            if (fields[0] != "PuzzleId") ++result.skipped;  // the header is not an error
            continue;
        }

        statement.reset();
        statement.bind(1, fields[0]);
        statement.bind(2, fields[1]);
        statement.bind(3, fields[2]);
        statement.bind(4, rating);
        statement.bind(5, " " + std::string(found >= 8 ? fields[7] : std::string_view()) + " ");
        if (statement.run() && impl_->connection->changes() > 0) {
            ++result.imported;
        } else {
            ++result.skipped;
        }
    }
    if (progress != nullptr) progress->store(handled);

    if (!impl_->connection->exec("COMMIT;")) {
        result.error = "Database error: " + impl_->connection->message();
        impl_->connection->exec("ROLLBACK;");
        result.imported = 0;
    }
    return result;
}

PuzzleImportResult PuzzleStore::importCsvFile(const std::filesystem::path& file, std::atomic<int>* progress,
                                              const std::atomic<bool>* cancel) {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        PuzzleImportResult result;
        result.error = "Could not open the file.";
        return result;
    }
    return importCsv(input, progress, cancel);
}

}  // namespace storage

#include "storage/GameStore.h"

#include <fstream>
#include <utility>

#include "storage/Sqlite.h"

#include "chess/Pgn.h"

namespace storage {

namespace {

using detail::Statement;


constexpr const char* kSchema = R"(
CREATE TABLE IF NOT EXISTS games (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    white      TEXT NOT NULL DEFAULT '',
    black      TEXT NOT NULL DEFAULT '',
    result     TEXT NOT NULL DEFAULT '*',
    date       TEXT NOT NULL DEFAULT '',
    event      TEXT NOT NULL DEFAULT '',
    moves      INTEGER NOT NULL DEFAULT 0,
    source     TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    pgn        TEXT NOT NULL
);
-- Time the app was open, per local day.
CREATE TABLE IF NOT EXISTS usage (
    day     TEXT PRIMARY KEY,
    seconds INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS games_white ON games(white);
CREATE INDEX IF NOT EXISTS games_black ON games(black);
)";

int fullMoves(const core::GameRecord& record) {
    return (static_cast<int>(record.tree.mainLine().size()) + 1) / 2;
}

std::string cleanTag(const core::GameRecord& record, std::string_view name) {
    std::string value = record.tag(name);
    if (value == "?" || value == "????.??.??") value.clear();
    return value;
}

void bindRecord(Statement& statement, const core::GameRecord& record, const std::string& pgn) {
    std::string result = record.tag("Result");
    if (result.empty()) result = "*";
    statement.bind(1, cleanTag(record, "White"));
    statement.bind(2, cleanTag(record, "Black"));
    statement.bind(3, result);
    statement.bind(4, cleanTag(record, "Date"));
    statement.bind(5, cleanTag(record, "Event"));
    statement.bind(6, static_cast<std::int64_t>(fullMoves(record)));
    statement.bind(7, pgn);
}

}  // namespace

struct GameStore::Impl {
    std::unique_ptr<detail::Connection> connection;

    bool exec(const char* sql) { return connection->exec(sql); }
    std::string message() const { return connection->message(); }
};

GameStore::GameStore() : impl_(std::make_unique<Impl>()) {}
GameStore::~GameStore() = default;

std::unique_ptr<GameStore> GameStore::open(const std::filesystem::path& file, std::string* error) {
    std::unique_ptr<GameStore> store(new GameStore());
    store->impl_->connection = detail::Connection::open(file, error);
    if (!store->impl_->connection) return nullptr;
    if (!store->impl_->exec(kSchema)) {
        if (error != nullptr) *error = "Could not prepare the game database: " + store->impl_->message();
        return nullptr;
    }
    return store;
}

void GameStore::addUsage(std::int64_t seconds) {
    if (seconds <= 0) return;
    Statement statement(*impl_->connection,
                        "INSERT INTO usage (day, seconds) VALUES (date('now', 'localtime'), ?1) "
                        "ON CONFLICT(day) DO UPDATE SET seconds = seconds + excluded.seconds");
    if (!statement.valid()) return;
    statement.bind(1, seconds);
    statement.step();
}

std::vector<std::pair<std::string, std::int64_t>> GameStore::usageByDay() const {
    std::vector<std::pair<std::string, std::int64_t>> days;
    Statement statement(*impl_->connection, "SELECT day, seconds FROM usage WHERE seconds > 0 ORDER BY day");
    if (!statement.valid()) return days;
    while (statement.step()) days.emplace_back(statement.text(0), statement.integer(1));
    return days;
}

Usage GameStore::usage() const {
    Usage result;
    Statement statement(*impl_->connection,
                        "SELECT COUNT(*), COALESCE(SUM(seconds), 0), "
                        "COALESCE(SUM(CASE WHEN day = date('now', 'localtime') THEN seconds END), 0) FROM usage");
    if (!statement.valid() || !statement.step()) return result;
    result.days = static_cast<int>(statement.integer(0));
    result.totalSeconds = statement.integer(1);
    result.todaySeconds = statement.integer(2);
    return result;
}

std::optional<GameId> GameStore::add(const core::GameRecord& record, std::string_view source) {
    Statement statement(*impl_->connection,
                        "INSERT INTO games (white, black, result, date, event, moves, pgn, source) "
                        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
    if (!statement.valid()) {
        lastError_ = impl_->message();
        return std::nullopt;
    }
    bindRecord(statement, record, core::writePgn(record));
    statement.bind(8, source);
    statement.step();
    if (!statement.done()) {
        lastError_ = impl_->message();
        return std::nullopt;
    }
    return impl_->connection->lastInsertId();
}

bool GameStore::update(GameId id, const core::GameRecord& record) {
    Statement statement(*impl_->connection,
                        "UPDATE games SET white=?1, black=?2, result=?3, date=?4, event=?5, moves=?6, pgn=?7 "
                        "WHERE id=?8");
    if (!statement.valid()) return false;
    bindRecord(statement, record, core::writePgn(record));
    statement.bind(8, id);
    statement.step();
    if (!statement.done()) lastError_ = impl_->message();
    return statement.done() && impl_->connection->changes() > 0;
}

bool GameStore::remove(GameId id) {
    Statement statement(*impl_->connection, "DELETE FROM games WHERE id=?1");
    if (!statement.valid()) return false;
    statement.bind(1, id);
    statement.step();
    return statement.done() && impl_->connection->changes() > 0;
}

std::vector<GameSummary> GameStore::list(const GameFilter& filter) const {
    std::vector<GameSummary> games;
    // Parameters are always bound, so user text never becomes part of the SQL.
    Statement statement(*impl_->connection,
                        "SELECT id, white, black, result, date, event, moves, source FROM games "
                        "WHERE (?1 = '' OR white LIKE ?2 ESCAPE '\\' OR black LIKE ?2 ESCAPE '\\' OR event LIKE ?2 ESCAPE '\\') "
                        "AND (?3 = '' OR result = ?3) "
                        "ORDER BY id DESC LIMIT ?4");
    if (!statement.valid()) {
        lastError_ = impl_->message();
        return games;
    }
    std::string pattern = "%";
    for (const char c : filter.text) {
        if (c == '%' || c == '_' || c == '\\') pattern += '\\';
        pattern += c;
    }
    pattern += '%';
    statement.bind(1, filter.text);
    statement.bind(2, pattern);
    statement.bind(3, filter.result);
    statement.bind(4, static_cast<std::int64_t>(filter.limit > 0 ? filter.limit : 500));

    while (statement.step()) {
        GameSummary game;
        game.id = statement.integer(0);
        game.white = statement.text(1);
        game.black = statement.text(2);
        game.result = statement.text(3);
        game.date = statement.text(4);
        game.event = statement.text(5);
        game.moves = static_cast<int>(statement.integer(6));
        game.source = statement.text(7);
        games.push_back(std::move(game));
    }
    return games;
}

int GameStore::count() const {
    Statement statement(*impl_->connection, "SELECT COUNT(*) FROM games");
    if (!statement.valid() || !statement.step()) return 0;
    return static_cast<int>(statement.integer(0));
}

std::optional<std::string> GameStore::pgn(GameId id) const {
    Statement statement(*impl_->connection, "SELECT pgn FROM games WHERE id=?1");
    if (!statement.valid()) return std::nullopt;
    statement.bind(1, id);
    if (!statement.step()) return std::nullopt;
    return statement.text(0);
}

ImportResult GameStore::importFile(const std::filesystem::path& file, std::atomic<int>* progress,
                                   const std::atomic<bool>* cancel) {
    ImportResult result;
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        result.error = "Could not open the file.";
        return result;
    }

    Statement statement(*impl_->connection,
                        "INSERT INTO games (white, black, result, date, event, moves, pgn, source) "
                        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 'import')");
    if (!statement.valid() || !impl_->exec("BEGIN IMMEDIATE;")) {
        result.error = "Database error: " + impl_->message();
        return result;
    }

    core::PgnSplitter splitter(input);
    std::string text;
    while (splitter.next(text)) {
        if (cancel != nullptr && cancel->load()) break;
        const std::optional<core::GameRecord> record = core::parsePgn(text);
        // Header-only entries without a single move are not games worth keeping.
        if (!record || record->tree.moveCount() == 0) {
            ++result.skipped;
        } else {
            statement.reset();
            bindRecord(statement, *record, core::writePgn(*record));
            statement.step();
            if (statement.done()) {
                ++result.imported;
            } else {
                ++result.skipped;
            }
        }
        if (progress != nullptr) progress->store(result.imported + result.skipped);
    }

    if (!impl_->exec("COMMIT;")) {
        result.error = "Database error: " + impl_->message();
        impl_->exec("ROLLBACK;");
        result.imported = 0;
    }
    return result;
}

bool GameStore::exportFile(const std::filesystem::path& file, const std::vector<GameId>& ids) const {
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (!output) return false;

    if (ids.empty()) {
        Statement statement(*impl_->connection, "SELECT pgn FROM games ORDER BY id");
        if (!statement.valid()) return false;
        while (statement.step()) output << statement.text(0) << '\n';
    } else {
        for (const GameId id : ids) {
            if (const std::optional<std::string> text = pgn(id)) output << *text << '\n';
        }
    }
    output.flush();
    return output.good();
}

}  // namespace storage

#pragma once

// Small RAII helpers over the SQLite C API, shared by the stores in this module.
// Not meant to be included from outside src/storage.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <sqlite3.h>

namespace storage::detail {

// Owns one database connection.
class Connection {
public:
    // Opens (creating if needed) the database file, or ":memory:".
    static std::unique_ptr<Connection> open(const std::filesystem::path& file, std::string* error) {
        std::unique_ptr<Connection> connection(new Connection());
        const std::u8string utf8 = file.u8string();
        const std::string name(utf8.begin(), utf8.end());
        const int code = sqlite3_open_v2(name.c_str(), &connection->db_,
                                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
        if (code != SQLITE_OK) {
            if (error != nullptr) *error = "Could not open the database: " + connection->message();
            return nullptr;
        }
        sqlite3_busy_timeout(connection->db_, 5000);
        // WAL lets a background import write while the UI connection reads.
        connection->exec("PRAGMA journal_mode=WAL;");
        connection->exec("PRAGMA synchronous=NORMAL;");
        return connection;
    }

    ~Connection() { sqlite3_close(db_); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    sqlite3* handle() const { return db_; }
    bool exec(const char* sql) { return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
    std::string message() const { return db_ != nullptr ? sqlite3_errmsg(db_) : "database is not open"; }
    std::int64_t lastInsertId() const { return sqlite3_last_insert_rowid(db_); }
    int changes() const { return sqlite3_changes(db_); }

private:
    Connection() = default;
    sqlite3* db_ = nullptr;
};

// Owns a prepared statement. Parameters are always bound, never spliced into the SQL.
class Statement {
public:
    Statement(const Connection& connection, const char* sql) {
        if (sqlite3_prepare_v2(connection.handle(), sql, -1, &statement_, nullptr) != SQLITE_OK) statement_ = nullptr;
    }
    ~Statement() { sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool valid() const { return statement_ != nullptr; }

    void bind(int index, std::string_view text) {
        // A null pointer would bind SQL NULL; an empty view must stay an empty string.
        const char* data = text.data() != nullptr ? text.data() : "";
        sqlite3_bind_text(statement_, index, data, static_cast<int>(text.size()), SQLITE_TRANSIENT);
    }
    void bind(int index, std::int64_t value) { sqlite3_bind_int64(statement_, index, value); }
    void bind(int index, int value) { sqlite3_bind_int64(statement_, index, value); }
    void bind(int index, double value) { sqlite3_bind_double(statement_, index, value); }

    // Returns true while there is a row to read.
    bool step() { return (lastCode_ = sqlite3_step(statement_)) == SQLITE_ROW; }
    // Runs a statement that returns no rows. Returns true on success.
    bool run() {
        step();
        return lastCode_ == SQLITE_DONE;
    }
    // True if the last step() finished the statement without an error.
    bool done() const { return lastCode_ == SQLITE_DONE; }
    void reset() {
        sqlite3_reset(statement_);
        sqlite3_clear_bindings(statement_);
    }

    std::string text(int column) const {
        const unsigned char* value = sqlite3_column_text(statement_, column);
        const int size = sqlite3_column_bytes(statement_, column);
        return value != nullptr ? std::string(reinterpret_cast<const char*>(value), static_cast<std::size_t>(size))
                                : std::string();
    }
    std::int64_t integer(int column) const { return sqlite3_column_int64(statement_, column); }
    double real(int column) const { return sqlite3_column_double(statement_, column); }
    bool isNull(int column) const { return sqlite3_column_type(statement_, column) == SQLITE_NULL; }

private:
    sqlite3_stmt* statement_ = nullptr;
    int lastCode_ = SQLITE_OK;
};

}  // namespace storage::detail

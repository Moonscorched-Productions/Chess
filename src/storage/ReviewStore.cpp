#include "storage/ReviewStore.h"

#include <utility>

#include "storage/Sqlite.h"

namespace storage {

namespace {

using detail::Statement;

// One row per player of a reviewed game. q1..q6 count moves by core::MoveQuality,
// p0..p2 hold the three game phases.
constexpr const char* kSchema = R"(
CREATE TABLE IF NOT EXISTS reviews (
    game_id     INTEGER NOT NULL,
    color       INTEGER NOT NULL,
    moves       INTEGER NOT NULL DEFAULT 0,
    accuracy    REAL NOT NULL DEFAULT 0,
    q1 INTEGER NOT NULL DEFAULT 0, q2 INTEGER NOT NULL DEFAULT 0, q3 INTEGER NOT NULL DEFAULT 0,
    q4 INTEGER NOT NULL DEFAULT 0, q5 INTEGER NOT NULL DEFAULT 0, q6 INTEGER NOT NULL DEFAULT 0,
    p0_moves INTEGER NOT NULL DEFAULT 0, p0_accuracy REAL NOT NULL DEFAULT 0,
    p1_moves INTEGER NOT NULL DEFAULT 0, p1_accuracy REAL NOT NULL DEFAULT 0,
    p2_moves INTEGER NOT NULL DEFAULT 0, p2_accuracy REAL NOT NULL DEFAULT 0,
    opening     TEXT NOT NULL DEFAULT '',
    reviewed_at TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (game_id, color)
);
)";

constexpr const char* kSideColumns =
    "r.moves, r.accuracy, r.q1, r.q2, r.q3, r.q4, r.q5, r.q6, "
    "r.p0_moves, r.p0_accuracy, r.p1_moves, r.p1_accuracy, r.p2_moves, r.p2_accuracy";

// Reads the columns of kSideColumns starting at `column`.
core::SideReview readSide(const Statement& statement, int column) {
    core::SideReview side;
    side.moves = static_cast<int>(statement.integer(column));
    side.accuracy = statement.real(column + 1);
    for (int q = 1; q < core::kQualityCount; ++q) {
        side.counts[static_cast<std::size_t>(q)] = static_cast<int>(statement.integer(column + 1 + q));
    }
    for (int p = 0; p < core::kPhaseCount; ++p) {
        side.phases[static_cast<std::size_t>(p)].moves = static_cast<int>(statement.integer(column + 8 + 2 * p));
        side.phases[static_cast<std::size_t>(p)].accuracy = statement.real(column + 9 + 2 * p);
    }
    return side;
}

}  // namespace

struct ReviewStore::Impl {
    std::unique_ptr<detail::Connection> connection;
};

ReviewStore::ReviewStore() : impl_(std::make_unique<Impl>()) {}
ReviewStore::~ReviewStore() = default;

std::unique_ptr<ReviewStore> ReviewStore::open(const std::filesystem::path& file, std::string* error) {
    std::unique_ptr<ReviewStore> store(new ReviewStore());
    store->impl_->connection = detail::Connection::open(file, error);
    if (!store->impl_->connection) return nullptr;
    if (!store->impl_->connection->exec(kSchema)) {
        if (error != nullptr) *error = "Could not prepare the review tables: " + store->impl_->connection->message();
        return nullptr;
    }
    return store;
}

bool ReviewStore::save(GameId game, const core::GameReview& review, std::string_view opening) {
    detail::Connection& connection = *impl_->connection;
    Statement statement(connection,
                        "INSERT OR REPLACE INTO reviews (game_id, color, moves, accuracy, q1, q2, q3, q4, q5, q6, "
                        "p0_moves, p0_accuracy, p1_moves, p1_accuracy, p2_moves, p2_accuracy, opening) "
                        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)");
    if (!statement.valid() || !connection.exec("BEGIN IMMEDIATE;")) return false;

    bool ok = true;
    for (int color = 0; color < 2 && ok; ++color) {
        const core::SideReview& side = color == 0 ? review.white : review.black;
        statement.reset();
        statement.bind(1, game);
        statement.bind(2, color);
        statement.bind(3, side.moves);
        statement.bind(4, side.accuracy);
        for (int q = 1; q < core::kQualityCount; ++q) statement.bind(4 + q, side.counts[static_cast<std::size_t>(q)]);
        for (int p = 0; p < core::kPhaseCount; ++p) {
            statement.bind(11 + 2 * p, side.phases[static_cast<std::size_t>(p)].moves);
            statement.bind(12 + 2 * p, side.phases[static_cast<std::size_t>(p)].accuracy);
        }
        statement.bind(17, opening);
        ok = statement.run();
    }
    // Reviews of games that were deleted from the library go at the same time.
    ok = ok && connection.exec("DELETE FROM reviews WHERE game_id NOT IN (SELECT id FROM games);");
    if (!ok || !connection.exec("COMMIT;")) {
        connection.exec("ROLLBACK;");
        return false;
    }
    return true;
}

bool ReviewStore::remove(GameId game) {
    Statement statement(*impl_->connection, "DELETE FROM reviews WHERE game_id=?1");
    if (!statement.valid()) return false;
    statement.bind(1, game);
    return statement.run();
}

std::unordered_map<GameId, ReviewAccuracy> ReviewStore::accuracies() const {
    std::unordered_map<GameId, ReviewAccuracy> result;
    Statement statement(*impl_->connection,
                        "SELECT r.game_id, r.color, r.accuracy FROM reviews r JOIN games g ON g.id = r.game_id");
    if (!statement.valid()) return result;
    while (statement.step()) {
        ReviewAccuracy& entry = result[statement.integer(0)];
        (statement.integer(1) == 0 ? entry.white : entry.black) = statement.real(2);
    }
    return result;
}

std::vector<GameId> ReviewStore::unreviewed(int limit) const {
    std::vector<GameId> result;
    Statement statement(*impl_->connection,
                        "SELECT id FROM games WHERE moves > 0 AND id NOT IN (SELECT game_id FROM reviews) "
                        "ORDER BY id LIMIT ?1");
    if (!statement.valid()) return result;
    statement.bind(1, limit);
    while (statement.step()) result.push_back(statement.integer(0));
    return result;
}

std::vector<core::PlayerGame> ReviewStore::playerGames(std::string_view names) const {
    std::vector<core::PlayerGame> result;
    const std::string sql = std::string("SELECT g.id, g.date, g.white, g.black, g.result, r.color, r.opening, ") + kSideColumns +
                            " FROM reviews r JOIN games g ON g.id = r.game_id ORDER BY g.date, g.id";
    Statement statement(*impl_->connection, sql.c_str());
    if (!statement.valid()) return result;

    while (statement.step()) {
        const std::string white = statement.text(2);
        const std::string black = statement.text(3);
        const bool isWhite = statement.integer(5) == 0;
        // The names decide which side is the user's; the row for the other side is skipped.
        const bool whiteMatches = core::matchesPlayer(white, names);
        const bool blackMatches = core::matchesPlayer(black, names);
        if (whiteMatches == blackMatches || whiteMatches != isWhite) continue;

        core::PlayerGame game;
        game.gameId = statement.integer(0);
        game.date = statement.text(1);
        game.color = isWhite ? core::Color::White : core::Color::Black;
        game.opponent = isWhite ? black : white;
        game.opening = statement.text(6);
        const std::string outcome = statement.text(4);
        if (outcome == "1-0") game.score = isWhite ? 1.0 : 0.0;
        else if (outcome == "0-1") game.score = isWhite ? 0.0 : 1.0;
        else if (outcome == "1/2-1/2") game.score = 0.5;
        game.review = readSide(statement, 7);
        result.push_back(std::move(game));
    }
    return result;
}

}  // namespace storage

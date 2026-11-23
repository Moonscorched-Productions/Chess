#include "storage/TrainingStore.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

#include "chess/Pgn.h"
#include "storage/Sqlite.h"

namespace storage {

namespace {

using detail::Statement;

constexpr const char* kSchema = R"(
CREATE TABLE IF NOT EXISTS repertoires (
    id     INTEGER PRIMARY KEY AUTOINCREMENT,
    name   TEXT NOT NULL,
    color  INTEGER NOT NULL DEFAULT 0,
    folder TEXT NOT NULL DEFAULT '',
    pgn    TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS repertoire_cards (
    repertoire_id INTEGER NOT NULL,
    position      TEXT NOT NULL,
    due_day       INTEGER NOT NULL DEFAULT 0,
    interval_days INTEGER NOT NULL DEFAULT 0,
    ease          REAL NOT NULL DEFAULT 2.5,
    reviews       INTEGER NOT NULL DEFAULT 0,
    streak        INTEGER NOT NULL DEFAULT 0,
    lapses        INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (repertoire_id, position)
);
-- Positions before the user's own mistakes, drilled with spaced repetition.
CREATE TABLE IF NOT EXISTS mistake_cards (
    position      TEXT PRIMARY KEY,
    due_day       INTEGER NOT NULL DEFAULT 0,
    interval_days INTEGER NOT NULL DEFAULT 0,
    ease          REAL NOT NULL DEFAULT 2.5,
    reviews       INTEGER NOT NULL DEFAULT 0,
    streak        INTEGER NOT NULL DEFAULT 0,
    lapses        INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS endgame_results (
    position_id TEXT PRIMARY KEY,
    attempts    INTEGER NOT NULL DEFAULT 0,
    successes   INTEGER NOT NULL DEFAULT 0
);
)";

}  // namespace

struct TrainingStore::Impl {
    std::unique_ptr<detail::Connection> connection;
};

TrainingStore::TrainingStore() : impl_(std::make_unique<Impl>()) {}
TrainingStore::~TrainingStore() = default;

std::unique_ptr<TrainingStore> TrainingStore::open(const std::filesystem::path& file, std::string* error) {
    std::unique_ptr<TrainingStore> store(new TrainingStore());
    store->impl_->connection = detail::Connection::open(file, error);
    if (!store->impl_->connection) return nullptr;
    if (!store->impl_->connection->exec(kSchema)) {
        if (error != nullptr) *error = "Could not prepare the training database: " + store->impl_->connection->message();
        return nullptr;
    }
    return store;
}

std::optional<RepertoireId> TrainingStore::createRepertoire(std::string_view name, core::Color color, std::string_view folder) {
    Statement statement(*impl_->connection, "INSERT INTO repertoires (name, color, folder) VALUES (?1, ?2, ?3)");
    if (!statement.valid()) return std::nullopt;
    statement.bind(1, name);
    statement.bind(2, color == core::Color::Black ? 1 : 0);
    statement.bind(3, folder);
    if (!statement.run()) return std::nullopt;
    return impl_->connection->lastInsertId();
}

bool TrainingStore::updateRepertoire(const RepertoireInfo& info) {
    Statement statement(*impl_->connection, "UPDATE repertoires SET name=?1, color=?2, folder=?3 WHERE id=?4");
    if (!statement.valid()) return false;
    statement.bind(1, info.name);
    statement.bind(2, info.color == core::Color::Black ? 1 : 0);
    statement.bind(3, info.folder);
    statement.bind(4, info.id);
    return statement.run() && impl_->connection->changes() > 0;
}

bool TrainingStore::removeRepertoire(RepertoireId id) {
    Statement cards(*impl_->connection, "DELETE FROM repertoire_cards WHERE repertoire_id=?1");
    if (cards.valid()) {
        cards.bind(1, id);
        cards.run();
    }
    Statement statement(*impl_->connection, "DELETE FROM repertoires WHERE id=?1");
    if (!statement.valid()) return false;
    statement.bind(1, id);
    return statement.run() && impl_->connection->changes() > 0;
}

std::vector<RepertoireInfo> TrainingStore::repertoires() const {
    std::vector<RepertoireInfo> result;
    Statement statement(*impl_->connection, "SELECT id, name, color, folder FROM repertoires ORDER BY folder, color, name, id");
    if (!statement.valid()) return result;
    while (statement.step()) {
        RepertoireInfo info;
        info.id = statement.integer(0);
        info.name = statement.text(1);
        info.color = statement.integer(2) == 1 ? core::Color::Black : core::Color::White;
        info.folder = statement.text(3);
        result.push_back(std::move(info));
    }
    return result;
}

core::GameTree TrainingStore::loadTree(RepertoireId id) const {
    Statement statement(*impl_->connection, "SELECT pgn FROM repertoires WHERE id=?1");
    if (statement.valid()) {
        statement.bind(1, id);
        if (statement.step()) {
            if (std::optional<core::GameRecord> record = core::parsePgn(statement.text(0))) return std::move(record->tree);
        }
    }
    return core::GameTree();
}

bool TrainingStore::saveTree(RepertoireId id, const core::GameTree& tree) {
    Statement statement(*impl_->connection, "UPDATE repertoires SET pgn=?1 WHERE id=?2");
    if (!statement.valid()) return false;
    statement.bind(1, core::writePgn(core::GameRecord{{}, tree}));
    statement.bind(2, id);
    return statement.run() && impl_->connection->changes() > 0;
}

std::unordered_map<std::string, core::CardState> TrainingStore::mistakeCards() const {
    std::unordered_map<std::string, core::CardState> result;
    Statement statement(*impl_->connection,
                        "SELECT position, due_day, interval_days, ease, reviews, streak, lapses FROM mistake_cards");
    if (!statement.valid()) return result;
    while (statement.step()) {
        core::CardState card;
        card.dueDay = static_cast<int>(statement.integer(1));
        card.intervalDays = static_cast<int>(statement.integer(2));
        card.ease = statement.real(3);
        card.reviews = static_cast<int>(statement.integer(4));
        card.streak = static_cast<int>(statement.integer(5));
        card.lapses = static_cast<int>(statement.integer(6));
        result.emplace(statement.text(0), card);
    }
    return result;
}

bool TrainingStore::saveMistakeCard(const std::string& key, const core::CardState& card) {
    Statement statement(*impl_->connection,
                        "INSERT INTO mistake_cards (position, due_day, interval_days, ease, reviews, streak, lapses) "
                        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) ON CONFLICT(position) DO UPDATE SET due_day=excluded.due_day, "
                        "interval_days=excluded.interval_days, ease=excluded.ease, reviews=excluded.reviews, "
                        "streak=excluded.streak, lapses=excluded.lapses");
    if (!statement.valid()) return false;
    statement.bind(1, key);
    statement.bind(2, card.dueDay);
    statement.bind(3, card.intervalDays);
    statement.bind(4, card.ease);
    statement.bind(5, card.reviews);
    statement.bind(6, card.streak);
    statement.bind(7, card.lapses);
    return statement.run();
}

int TrainingStore::addStarterRepertoires(const std::filesystem::path& directory) {
    {
        Statement check(*impl_->connection, "SELECT 1 FROM meta WHERE key='starter_repertoires'");
        if (check.valid() && check.step()) return 0;  // added before (and maybe deleted since)
    }
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".pgn") files.push_back(it->path());
    }
    if (files.empty()) return 0;
    std::sort(files.begin(), files.end());
    int added = 0;
    for (const std::filesystem::path& file : files) {
        std::ifstream input(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const std::optional<core::GameRecord> record = core::parsePgn(text);
        if (!record || record->tree.moveCount() == 0) continue;
        std::string name = record->tag("Event");
        if (name.rfind("Starter: ", 0) == 0) name = name.substr(9);
        if (name.empty()) name = file.stem().string();
        const core::Color color = record->tag("Side") == "Black" ? core::Color::Black : core::Color::White;
        const std::optional<RepertoireId> id = createRepertoire(name, color, "Starter");
        if (id && saveTree(*id, record->tree)) ++added;
    }
    impl_->connection->exec("INSERT OR REPLACE INTO meta (key, value) VALUES ('starter_repertoires', '1')");
    return added;
}

std::unordered_map<std::string, core::CardState> TrainingStore::cards(RepertoireId id) const {
    std::unordered_map<std::string, core::CardState> result;
    Statement statement(*impl_->connection,
                        "SELECT position, due_day, interval_days, ease, reviews, streak, lapses "
                        "FROM repertoire_cards WHERE repertoire_id=?1");
    if (!statement.valid()) return result;
    statement.bind(1, id);
    while (statement.step()) {
        core::CardState card;
        card.dueDay = static_cast<int>(statement.integer(1));
        card.intervalDays = static_cast<int>(statement.integer(2));
        card.ease = statement.real(3);
        card.reviews = static_cast<int>(statement.integer(4));
        card.streak = static_cast<int>(statement.integer(5));
        card.lapses = static_cast<int>(statement.integer(6));
        result.emplace(statement.text(0), card);
    }
    return result;
}

bool TrainingStore::saveCard(RepertoireId id, const std::string& positionKey, const core::CardState& card) {
    Statement statement(*impl_->connection,
                        "INSERT INTO repertoire_cards (repertoire_id, position, due_day, interval_days, ease, reviews, streak, lapses) "
                        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
                        "ON CONFLICT(repertoire_id, position) DO UPDATE SET due_day=excluded.due_day, "
                        "interval_days=excluded.interval_days, ease=excluded.ease, reviews=excluded.reviews, "
                        "streak=excluded.streak, lapses=excluded.lapses");
    if (!statement.valid()) return false;
    statement.bind(1, id);
    statement.bind(2, positionKey);
    statement.bind(3, card.dueDay);
    statement.bind(4, card.intervalDays);
    statement.bind(5, card.ease);
    statement.bind(6, card.reviews);
    statement.bind(7, card.streak);
    statement.bind(8, card.lapses);
    return statement.run();
}

void TrainingStore::recordEndgame(const std::string& positionId, bool success) {
    Statement statement(*impl_->connection,
                        "INSERT INTO endgame_results (position_id, attempts, successes) VALUES (?1, 1, ?2) "
                        "ON CONFLICT(position_id) DO UPDATE SET attempts=attempts+1, successes=successes+excluded.successes");
    if (!statement.valid()) return;
    statement.bind(1, positionId);
    statement.bind(2, success ? 1 : 0);
    statement.run();
}

std::unordered_map<std::string, EndgameProgress> TrainingStore::endgameProgress() const {
    std::unordered_map<std::string, EndgameProgress> result;
    Statement statement(*impl_->connection, "SELECT position_id, attempts, successes FROM endgame_results");
    if (!statement.valid()) return result;
    while (statement.step()) {
        EndgameProgress progress;
        progress.attempts = static_cast<int>(statement.integer(1));
        progress.successes = static_cast<int>(statement.integer(2));
        result.emplace(statement.text(0), progress);
    }
    return result;
}

}  // namespace storage

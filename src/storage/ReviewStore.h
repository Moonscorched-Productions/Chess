#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "chess/Review.h"
#include "chess/Statistics.h"
#include "storage/GameStore.h"

namespace storage {

// Accuracy of both players in a reviewed game.
struct ReviewAccuracy {
    double white = 0.0;
    double black = 0.0;
};

// Results of game reviews, kept next to the games in the same database file
// (open the GameStore first: the queries here read its table). Like the other
// stores, each object owns one connection and belongs to one thread.
class ReviewStore {
public:
    static std::unique_ptr<ReviewStore> open(const std::filesystem::path& file, std::string* error = nullptr);

    ~ReviewStore();
    ReviewStore(const ReviewStore&) = delete;
    ReviewStore& operator=(const ReviewStore&) = delete;

    // Stores (or replaces) the review of a library game. `opening` is its opening name.
    bool save(GameId game, const core::GameReview& review, std::string_view opening);
    bool remove(GameId game);

    // Accuracies of every reviewed game that still exists in the library.
    std::unordered_map<GameId, ReviewAccuracy> accuracies() const;
    // Library games without a review, oldest first.
    std::vector<GameId> unreviewed(int limit = 1000) const;

    // Reviewed games in which one player's name is in `names` (comma separated),
    // seen from that player's side, oldest first. Games where both or neither
    // player match are left out.
    std::vector<core::PlayerGame> playerGames(std::string_view names) const;

private:
    ReviewStore();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace storage

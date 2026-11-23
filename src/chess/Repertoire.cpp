#include "chess/Repertoire.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace core {

CardState reviewCard(CardState card, bool correct, int today) {
    ++card.reviews;
    if (correct) {
        ++card.streak;
        if (card.streak == 1) {
            card.intervalDays = 1;
        } else if (card.streak == 2) {
            card.intervalDays = 3;
        } else {
            card.intervalDays = std::max(card.intervalDays + 1,
                                         static_cast<int>(std::lround(static_cast<double>(card.intervalDays) * card.ease)));
        }
        card.intervalDays = std::min(card.intervalDays, 365);
        card.ease = std::min(card.ease + 0.05, 3.0);
        card.dueDay = today + card.intervalDays;
    } else {
        card.streak = 0;
        ++card.lapses;
        card.intervalDays = 0;
        card.ease = std::max(card.ease - 0.2, 1.3);
        card.dueDay = today;  // ask again in this session
    }
    return card;
}

int todayNumber() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    // Days from the civil date, so the number changes at local midnight.
    int year = local.tm_year + 1900;
    const int month = local.tm_mon + 1;
    const int day = local.tm_mday;
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const int yearOfEra = year - era * 400;
    const int dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

std::string positionKey(std::string_view fen) {
    // Keep the first three space separated fields.
    std::size_t end = 0;
    for (int field = 0; field < 3; ++field) {
        end = fen.find(' ', end);
        if (end == std::string_view::npos) return std::string(fen);
        if (field < 2) ++end;
    }
    return std::string(fen.substr(0, end));
}

Color sideToMoveAt(const GameTree& tree, NodeId id) {
    if (id == kRootNode) return tree.startSideToMove();
    return opposite(tree.mover(id));
}

std::vector<NodeId> decisionNodes(const GameTree& tree, Color side) {
    std::vector<NodeId> result;
    std::vector<NodeId> pending{kRootNode};
    while (!pending.empty()) {
        const NodeId id = pending.back();
        pending.pop_back();
        const TreeNode& node = tree.node(id);
        if (!node.children.empty() && sideToMoveAt(tree, id) == side) result.push_back(id);
        pending.insert(pending.end(), node.children.begin(), node.children.end());
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::vector<NodeId>> allLines(const GameTree& tree, std::size_t limit) {
    std::vector<std::vector<NodeId>> lines;
    std::vector<NodeId> leaves;
    std::vector<NodeId> pending{kRootNode};
    while (!pending.empty() && leaves.size() < limit) {
        const NodeId id = pending.back();
        pending.pop_back();
        const TreeNode& node = tree.node(id);
        if (node.children.empty()) {
            if (id != kRootNode) leaves.push_back(id);
            continue;
        }
        // Reverse order keeps the main line first.
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) pending.push_back(*it);
    }
    lines.reserve(leaves.size());
    for (const NodeId leaf : leaves) lines.push_back(tree.pathTo(leaf));
    return lines;
}

}  // namespace core

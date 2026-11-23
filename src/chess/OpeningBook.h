#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "chess/GameTree.h"

namespace core {

struct OpeningName {
    std::string eco;   // e.g. "C65"
    std::string name;  // e.g. "Ruy Lopez: Berlin Defense"
};

// Names openings by position, using the Lichess opening tables (tab separated files
// with the columns eco, name and pgn). Positions are matched regardless of move order.
class OpeningBook {
public:
    // Reads every *.tsv file in the directory. Returns the number of named positions.
    int load(const std::filesystem::path& directory);
    // Adds one table from text (the same format), mainly for tests.
    int loadText(std::string_view tsv);

    bool empty() const { return names_.empty(); }
    std::size_t size() const { return names_.size(); }

    // The name of exactly this position, or nullptr.
    const OpeningName* find(std::string_view fen) const;
    // The most specific name along the line leading to `id`: the last named position on it.
    const OpeningName* identify(const GameTree& tree, NodeId id) const;

private:
    std::unordered_map<std::string, OpeningName> names_;
};

}  // namespace core

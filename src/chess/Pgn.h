#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

#include "chess/GameTree.h"

namespace core {

// Parses the first game found in `text`: tags, moves, nested variations, comments,
// annotation glyphs and "[%eval ...]" evaluations.
// Returns nullopt and fills `error` when the text holds no game or an illegal move.
std::optional<GameRecord> parsePgn(std::string_view text, std::string* error = nullptr);

// Writes a game as PGN text, including variations, comments and evaluations.
std::string writePgn(const GameRecord& record);

// Splits a PGN stream holding many games into one chunk of text per game,
// without loading the whole stream into memory.
class PgnSplitter {
public:
    explicit PgnSplitter(std::istream& input) : input_(input) {}

    // Reads the next game into `game`. Returns false at the end of the stream.
    bool next(std::string& game);

private:
    std::istream& input_;
    std::string pendingLine_;
    bool hasPendingLine_ = false;
};

// Today's date in PGN format ("2026.01.31").
std::string pgnDateToday();

}  // namespace core

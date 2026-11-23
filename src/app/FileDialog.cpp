#include "app/FileDialog.h"

#include "platform/Platform.h"

namespace app {

namespace {

const platform::FileType kPgn{"PGN files", "pgn"};
const platform::FileType kCsv{"CSV files", "csv"};
// The formats SFML's audio module reads.
const platform::FileType kSound{"Sound files", "ogg", {"mp3", "wav", "flac"}};

}  // namespace

std::optional<std::filesystem::path> openPgnDialog() { return platform::openFileDialog(kPgn); }

std::optional<std::filesystem::path> openCsvDialog() { return platform::openFileDialog(kCsv); }

std::optional<std::filesystem::path> openSoundDialog() { return platform::openFileDialog(kSound); }

std::optional<std::filesystem::path> savePgnDialog(const std::filesystem::path& suggestedName) {
    return platform::saveFileDialog(kPgn, suggestedName);
}

}  // namespace app

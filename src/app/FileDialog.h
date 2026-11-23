#pragma once

#include <filesystem>
#include <optional>

namespace app {

// Native "open" and "save" dialogs for PGN files. They block until the user answers.
// Return nullopt when the user cancels (or on platforms without an implementation).
std::optional<std::filesystem::path> openPgnDialog();
std::optional<std::filesystem::path> openCsvDialog();
std::optional<std::filesystem::path> savePgnDialog(const std::filesystem::path& suggestedName);

}  // namespace app

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace app {

// Directory containing the running executable.
std::filesystem::path executableDir();

// Searches the executable directory and its parents for a directory or file
// with the given name. Returns an empty path if none is found.
std::filesystem::path findUpwards(std::string_view name);

// Per-user writable directory for settings and (later) databases. Created on demand.
std::filesystem::path userDataDir();

// Conversions between paths and the UTF-8 strings used in settings and the UI.
std::filesystem::path pathFromUtf8(std::string_view text);
std::string pathToUtf8(const std::filesystem::path& path);

// Looks for a Stockfish executable inside an engines/ folder next to (or above)
// the executable. Returns an empty path if there is none.
std::filesystem::path findBundledEngine();

// The engine to launch: the configured path if there is one, otherwise the bundled engine.
// `configured` is UTF-8. Returns an empty path if no engine is available.
std::filesystem::path resolveEnginePath(std::string_view configured);

}  // namespace app

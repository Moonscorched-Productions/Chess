#pragma once

// Everything the application needs from the operating system, behind one interface.
// Implementations live in windows/ and posix/ (macOS and Linux); the build picks one.
// See also Process.h (child processes), LocalConnection.h (talking to other programs, e.g.
// Discord) and LiveResize.h (drawing during window resize).

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace platform {

// --- Locations -------------------------------------------------------------------

// Directory containing the running executable.
std::filesystem::path executableDir();

// Per-user writable directory for the application's data. Not created by this call.
//   Windows: %APPDATA%\<appName>
//   macOS:   ~/Library/Application Support/<appName>
//   Linux:   $XDG_DATA_HOME/<appName> or ~/.local/share/<appName>
std::filesystem::path userDataDir(std::string_view appName);

// Font files suited to user interface text, best first. Some may not exist.
std::vector<std::filesystem::path> uiFontCandidates();

// True if the file looks like a program this system can run (".exe" on Windows,
// the executable permission elsewhere).
bool isExecutableFile(const std::filesystem::path& file);

// --- Windows ---------------------------------------------------------------------

// Makes an SFML window render at the display's full pixel resolution (Retina on macOS).
// Afterwards SFML reports window sizes and mouse positions in pixels. `window` is the
// native handle (an NSWindow on macOS). Call again after every window (re)creation.
// Does nothing on other platforms.
void enableHighDpi(void* window);

// Pixels per point of the window: 2 on a Retina display after enableHighDpi, otherwise 1.
float windowScaleFactor(void* window);

// --- File dialogs ----------------------------------------------------------------

struct FileType {
    std::string description;  // e.g. "PGN files"
    std::string extension;    // without the dot, e.g. "pgn"
    // Further extensions the open dialog accepts, e.g. {"mp3", "wav"}. Saving uses `extension`.
    std::vector<std::string> alternatives = {};
};

// Native dialogs. They block until the user answers and return nullopt on cancel
// (or when the system offers no dialog).
std::optional<std::filesystem::path> openFileDialog(const FileType& type);
std::optional<std::filesystem::path> saveFileDialog(const FileType& type, const std::filesystem::path& suggestedName);

}  // namespace platform

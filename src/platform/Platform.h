#pragma once

// Everything the application needs from the operating system, behind one interface.
// Implementations live in windows/ and posix/ (macOS and Linux); the build picks one.
// See also Process.h (child processes) and LiveResize.h (drawing during window resize).

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

// --- File dialogs ----------------------------------------------------------------

struct FileType {
    std::string description;  // e.g. "PGN files"
    std::string extension;    // without the dot, e.g. "pgn"
};

// Native dialogs. They block until the user answers and return nullopt on cancel
// (or when the system offers no dialog).
std::optional<std::filesystem::path> openFileDialog(const FileType& type);
std::optional<std::filesystem::path> saveFileDialog(const FileType& type, const std::filesystem::path& suggestedName);

// --- Shared libraries ------------------------------------------------------------

// File name of a shared library on this system: "name.dll", "name.dylib" or "name.so".
std::string sharedLibraryFileName(std::string_view baseName);

// A dynamically loaded library. Stays loaded for the lifetime of the object.
class SharedLibrary {
public:
    // Returns nullptr if the library cannot be loaded.
    static std::unique_ptr<SharedLibrary> load(const std::filesystem::path& file);
    ~SharedLibrary();
    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;

    // Address of an exported function or variable, or nullptr.
    void* symbol(const char* name) const;

private:
    SharedLibrary() = default;
    void* handle_ = nullptr;
};

}  // namespace platform

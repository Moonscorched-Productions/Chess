#include "platform/Platform.h"

// macOS and Linux implementation.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <system_error>

#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace platform {

namespace fs = std::filesystem;

namespace {

fs::path homeDir() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') return home;
    return {};
}

// Runs a shell command and returns the first line it prints (without the newline).
std::string firstLineOf(const std::string& command) {
    std::string line;
    if (FILE* pipe = popen(command.c_str(), "r")) {
        char buffer[4096];
        if (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) line = buffer;
        pclose(pipe);
    }
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    return line;
}

// Text that is safe inside a double-quoted shell or AppleScript string: anything that
// could end the quoting is dropped rather than escaped.
std::string plainText(std::string_view text) {
    std::string result;
    for (const char c : text) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' ||
                        c == '-' || c == '_';
        if (ok) result += c;
    }
    return result;
}

std::optional<fs::path> runDialog(bool save, const FileType& type, const fs::path& suggestedName) {
    const std::string extension = plainText(type.extension);
    const std::string name = plainText(suggestedName.filename().string());
#ifdef __APPLE__
    // AppleScript's standard file panels, so no Objective-C is needed.
    std::string script;
    if (save) {
        script = "POSIX path of (choose file name with prompt \"Save\" default name \"" + name + "\")";
    } else {
        script = "POSIX path of (choose file with prompt \"Open\" of type {\"" + extension + "\"})";
    }
    const std::string result = firstLineOf("osascript -e '" + script + "' 2>/dev/null");
#else
    std::string command = "zenity --file-selection --file-filter=\"*." + extension + "\"";
    if (save) command += " --save --confirm-overwrite --filename=\"" + name + "\"";
    const std::string result = firstLineOf(command + " 2>/dev/null");
#endif
    if (result.empty()) return std::nullopt;
    fs::path path(result);
    // A save without an extension gets the expected one.
    if (save && !path.has_extension() && !extension.empty()) path += "." + extension;
    return path;
}

}  // namespace

fs::path executableDir() {
    std::error_code ec;
#ifdef __APPLE__
    char buffer[4096];
    std::uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        const fs::path resolved = fs::weakly_canonical(fs::path(buffer), ec);
        if (!ec) return resolved.parent_path();
    }
#else
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return self.parent_path();
#endif
    return fs::current_path(ec);
}

fs::path userDataDir(std::string_view appName) {
    const fs::path home = homeDir();
#ifdef __APPLE__
    const fs::path base = home.empty() ? executableDir() : home / "Library" / "Application Support";
#else
    fs::path base;
    if (const char* data = std::getenv("XDG_DATA_HOME"); data != nullptr && *data != '\0') {
        base = data;
    } else {
        base = home.empty() ? executableDir() : home / ".local" / "share";
    }
#endif
    return base / appName;
}

std::vector<fs::path> uiFontCandidates() {
#ifdef __APPLE__
    return {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Verdana.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
#else
    return {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    };
#endif
}

bool isExecutableFile(const fs::path& file) {
    std::error_code ec;
    return fs::is_regular_file(file, ec) && access(file.c_str(), X_OK) == 0;
}

std::optional<fs::path> openFileDialog(const FileType& type) { return runDialog(false, type, {}); }

std::optional<fs::path> saveFileDialog(const FileType& type, const fs::path& suggestedName) {
    return runDialog(true, type, suggestedName);
}

}  // namespace platform

#ifndef __APPLE__
// macOS has its own implementation in macos/HighDpiMac.mm; X11 windows need nothing.
namespace platform {
void enableHighDpi(void*) {}
float windowScaleFactor(void*) { return 1.0f; }
}  // namespace platform
#endif

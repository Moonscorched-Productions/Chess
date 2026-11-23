#include "app/Paths.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>

#include "platform/Platform.h"

namespace app {

namespace fs = std::filesystem;

fs::path executableDir() { return platform::executableDir(); }

fs::path findUpwards(std::string_view name) {
    std::error_code ec;
    fs::path dir = executableDir();
    for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
        const fs::path candidate = dir / name;
        if (fs::exists(candidate, ec)) return candidate;
#ifdef __APPLE__
        // Inside an app bundle the program is in Contents/MacOS, data in Contents/Resources
        // and helper programs (the engine) in Contents/Helpers.
        for (const char* bundleFolder : {"Resources", "Helpers"}) {
            const fs::path inBundle = dir / bundleFolder / name;
            if (fs::exists(inBundle, ec)) return inBundle;
        }
#endif
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

fs::path userDataDir() {
    const fs::path dir = platform::userDataDir("ChessDesktop");
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

fs::path pathFromUtf8(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string pathToUtf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path findBundledEngine() {
    const fs::path root = findUpwards("engines");
    if (root.empty()) return {};
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::string name = pathToUtf8(it->path().filename());
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // The platform decides what counts as a program (".exe", or the executable bit).
        if (name.starts_with("stockfish") && platform::isExecutableFile(it->path())) return it->path();
    }
    return {};
}

fs::path resolveEnginePath(std::string_view configured) {
    return configured.empty() ? findBundledEngine() : pathFromUtf8(configured);
}

}  // namespace app

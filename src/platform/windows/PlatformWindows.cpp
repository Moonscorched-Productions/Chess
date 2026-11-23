#include "platform/Platform.h"

#include <iterator>
#include <system_error>

#include <windows.h>

#include <commdlg.h>

namespace platform {

namespace fs = std::filesystem;

fs::path executableDir() {
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer)) return fs::path(buffer).parent_path();
    std::error_code ec;
    return fs::current_path(ec);
}

fs::path userDataDir(std::string_view appName) {
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    const fs::path base = length > 0 && length < std::size(buffer) ? fs::path(buffer) : executableDir();
    return base / appName;
}

std::vector<fs::path> uiFontCandidates() {
    wchar_t buffer[MAX_PATH];
    const UINT length = GetWindowsDirectoryW(buffer, static_cast<UINT>(std::size(buffer)));
    const fs::path fonts = (length > 0 && length < std::size(buffer) ? fs::path(buffer) : fs::path(L"C:\\Windows")) / "Fonts";
    return {fonts / "segoeui.ttf", fonts / "arial.ttf", fonts / "tahoma.ttf"};
}

bool isExecutableFile(const fs::path& file) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return false;
    const std::wstring extension = file.extension().wstring();
    return _wcsicmp(extension.c_str(), L".exe") == 0;
}

namespace {

std::optional<fs::path> runDialog(bool save, const FileType& type, const fs::path& suggestedName) {
    wchar_t buffer[2048] = L"";
    const std::wstring suggestion = suggestedName.wstring();
    if (suggestion.size() < std::size(buffer)) suggestion.copy(buffer, suggestion.size());

    // The filter is a list of null separated pairs, ended by an empty string.
    const std::wstring extension = fs::path(type.extension).wstring();
    std::wstring filter = fs::path(type.description).wstring() + L" (*." + extension + L")";
    filter.push_back(L'\0');
    filter += L"*." + extension;
    filter.push_back(L'\0');
    filter += L"All files (*.*)";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.lpstrDefExt = extension.c_str();
    // NOCHANGEDIR: the dialog must not move the process's working directory.
    dialog.Flags = OFN_NOCHANGEDIR | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    const BOOL ok = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!ok) return std::nullopt;
    return fs::path(buffer);
}

}  // namespace

std::optional<fs::path> openFileDialog(const FileType& type) { return runDialog(false, type, {}); }

std::optional<fs::path> saveFileDialog(const FileType& type, const fs::path& suggestedName) {
    return runDialog(true, type, suggestedName);
}

}  // namespace platform

namespace platform {
// The window is not DPI-aware: Windows scales it, and SFML sizes stay in window units.
void enableHighDpi(void*) {}
float windowScaleFactor(void*) { return 1.0f; }
}  // namespace platform

#include "platform/LiveResize.h"

#include <utility>


#include <windows.h>

#include <commctrl.h>

namespace platform {

namespace {

constexpr UINT_PTR kSubclassId = 0xC4E55;
constexpr UINT_PTR kTimerId = 0xC4E56;

LRESULT CALLBACK subclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    auto* self = reinterpret_cast<LiveResize*>(data);
    switch (message) {
        case WM_ENTERSIZEMOVE:
            SetTimer(hwnd, kTimerId, 8, nullptr);
            break;
        case WM_EXITSIZEMOVE:
            KillTimer(hwnd, kTimerId);
            break;
        case WM_TIMER:
            if (wParam == kTimerId) {
                self->runFrame();
                return 0;
            }
            break;
        case WM_SIZE: {
            // Redraw right away so a resize never shows a stretched old frame.
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            self->runFrame();
            return result;
        }
        case WM_NCDESTROY:
            KillTimer(hwnd, kTimerId);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

}  // namespace

LiveResize::~LiveResize() { remove(); }

void LiveResize::install(void* window, std::function<void()> onFrame) {
    remove();
    window_ = window;
    onFrame_ = std::move(onFrame);
    installed_ = SetWindowSubclass(static_cast<HWND>(window_), subclassProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this)) != FALSE;
}

void LiveResize::remove() {
    if (!installed_) return;
    KillTimer(static_cast<HWND>(window_), kTimerId);
    RemoveWindowSubclass(static_cast<HWND>(window_), subclassProc, kSubclassId);
    installed_ = false;
    onFrame_ = nullptr;
}

void LiveResize::runFrame() {
    // Rendering can itself produce window messages; never nest frames.
    if (inFrame_ || !onFrame_) return;
    inFrame_ = true;
    onFrame_();
    inFrame_ = false;
}

}  // namespace platform


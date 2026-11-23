#pragma once

#include <functional>


namespace platform {

// While the user drags or resizes a window, Windows runs its own message loop
// and the application's main loop stands still. This hooks the window so that
// `onFrame` keeps being called during that time and the contents stay live.
//
// Does nothing on other platforms. `onFrame` must not pump window events.
class LiveResize {
public:
    LiveResize() = default;
    ~LiveResize();
    LiveResize(const LiveResize&) = delete;
    LiveResize& operator=(const LiveResize&) = delete;

    // `window` is the native window handle (an HWND on Windows).
    void install(void* window, std::function<void()> onFrame);
    void remove();

    // Used by the window hook.
    void runFrame();

private:
    void* window_ = nullptr;
    std::function<void()> onFrame_;
    bool installed_ = false;
    bool inFrame_ = false;
};

}  // namespace platform

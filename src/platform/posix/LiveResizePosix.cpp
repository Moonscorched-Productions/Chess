#include "platform/LiveResize.h"

// Nothing to hook outside Windows: the window is simply redrawn once the resize ends.

namespace platform {

LiveResize::~LiveResize() = default;
void LiveResize::install(void*, std::function<void()>) {}
void LiveResize::remove() {}
void LiveResize::runFrame() {}

}  // namespace platform

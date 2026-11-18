#include "core/host_chrome.h"

#include "vkm_platform/window/window_manager.h"

namespace Vkm::Engine {

HostChrome::ViewportRect HostChrome::viewportIn(const Platform::WindowManager& window) const {
    if (viewport.width != 0 && viewport.height != 0) return viewport;
    const auto width  = static_cast<uint32_t>(window.getWidth());
    const auto height = static_cast<uint32_t>(window.getHeight());
    return ViewportRect{0, 0, width, height};
}

} // namespace Vkm::Engine

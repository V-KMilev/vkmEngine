#include "core/host_chrome.h"

#include "platform/window/window_manager.h"

namespace Vkm::Engine {

HostChrome::ViewportRect HostChrome::viewport(const WindowManager& window) const {
    if (m_viewport.width != 0 && m_viewport.height != 0) return m_viewport;
    const auto width  = static_cast<uint32_t>(window.getWidth());
    const auto height = static_cast<uint32_t>(window.getHeight());
    return ViewportRect{0, 0, width, height};
}

} // namespace Vkm::Engine

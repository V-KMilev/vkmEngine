#include "core/host_chrome.h"

#include "platform/window/window_manager.h"

namespace Vkm::Engine {

HostChrome::ViewportRect HostChrome::viewport(const WindowManager& window) const {
    if (m_viewport.width != 0 && m_viewport.height != 0) return m_viewport;
    return ViewportRect{0, 0,
                        static_cast<uint32_t>(window.getWidth()),
                        static_cast<uint32_t>(window.getHeight())};
}

} // namespace Vkm::Engine

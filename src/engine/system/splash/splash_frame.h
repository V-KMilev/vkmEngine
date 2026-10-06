#pragma once

#include <string>

namespace Vkm::Engine {

/**
 * @brief What the splash looks like this frame, or nothing; SplashSystem's product.
 *
 * Names an image rather than carrying one: the core cannot decode files (the
 * loaders live in vkm_tools). The key doubles as the backend's cache key.
 */
struct SplashFrame {
    std::string key;              ///< Image path; empty means there is no splash.
    float       opacity = 0.0f;   ///< 0..1.

    bool isShowing() const { return !key.empty(); }
};

} // namespace Vkm::Engine

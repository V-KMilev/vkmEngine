#pragma once

#include <string>

namespace Vkm::Engine {

/**
 * @brief What the splash looks like this frame, or nothing.
 *
 * A per-frame product like the UI draw list, and carried the same way: the
 * SplashSystem publishes it, the RenderView copies it, and the backend's last
 * pass draws it.
 *
 * It names an image rather than carrying one because decoding a file is not
 * something the engine core can do: the loaders live in vkm_tools, which sits
 * above it and which the backend links. The key is also the backend's cache
 * key, so the decode and the upload happen once per logo rather than per frame.
 */
struct SplashFrame {
    std::string key;              ///< Image path; empty means there is no splash.
    float       opacity = 0.0f;   ///< 0..1, the fade this frame lands at.

    bool isShowing() const { return !key.empty(); }
};

} // namespace Vkm::Engine

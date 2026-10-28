#pragma once

#include <string>

namespace Vkm::Engine {

/// An ident's length, seconds, its fades included (SplashSystem::addIdent): the backend times its
/// reveal by it.
inline constexpr float IDENT_SECONDS = 10.0f;

/**
 * @brief What the splash looks like this frame, or nothing; SplashSystem's product.
 *
 * Names an image rather than carrying one: the core cannot decode files (the loaders live in
 * vkm_tools). The key doubles as the backend's cache key.
 */
struct SplashFrame {
    std::string key;              ///< Image path; empty for none.
    bool        reveal = false;   ///< An ident's: lit by a comet's light that strikes it, not faded in.
    std::string caption;          ///< An image of a line shown small at the screen's foot with it; or none.
    float       opacity = 0.0f;   ///< 0..1.
    float       time    = 0.0f;   ///< Seconds since this entry began, its fade in included.

    bool isShowing() const { return !key.empty(); }
};

} // namespace Vkm::Engine

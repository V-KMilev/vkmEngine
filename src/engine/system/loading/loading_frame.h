#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "ecs/component/ui/ui_canvas.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Where the loading screen's bar sits: a share of the screen's width across, centred; its top
 *        a share of the height down; reference pixels high, at a screen LOADING_REFERENCE_HEIGHT
 *        pixels tall.
 *
 * Its wave rises and falls within that height.
 */
inline constexpr float LOADING_REFERENCE_HEIGHT = UICanvas::REFERENCE_HEIGHT;
inline constexpr float LOADING_BAR_WIDTH        = 0.32f;
inline constexpr float LOADING_BAR_AT           = 0.8f;
inline constexpr float LOADING_BAR_HEIGHT       = 14.0f;

/// The loading bar's rectangle in screen pixels, as loadingBar places it.
struct LoadingBarRect {
    float x      = 0.0f;
    float y      = 0.0f;  ///< Down from the top.
    float width  = 0.0f;
    float height = 0.0f;
};

/**
 * @brief The bar's rectangle on a screen @p width by @p height pixels.
 *
 * @param width  Screen width, pixels.
 * @param height Screen height, pixels.
 * @return The rectangle.
 */
inline LoadingBarRect loadingBar(float width, float height) {
    const float scale = height / LOADING_REFERENCE_HEIGHT;
    LoadingBarRect bar;
    bar.width  = std::round(width * LOADING_BAR_WIDTH);
    bar.height = std::max(std::round(LOADING_BAR_HEIGHT * scale), 2.0f);
    bar.x      = std::round((width - bar.width) * 0.5f);
    bar.y      = std::round(height * LOADING_BAR_AT);
    return bar;
}

/// What the loading screen shows this frame, or nothing; LoadingSystem's product.
struct LoadingFrame {
    float       opacity      = 0.0f;  ///< 0..1; 0 shows nothing.
    float       content      = 1.0f;  ///< 0..1: how far the screen's content has come in after a splash.
    float       progress     = 0.0f;  ///< 0..1: how far the bar is filled.
    std::string status;               ///< One line saying what is being waited on.
    std::string tip;                  ///< One of the project's tips, or none.
    float       tipShown     = 1.0f;  ///< 0..1: how far the tip has faded in, as the tips change.
    /// The project's own screen: UI canvases drawn over the water, under the bar; or none.
    Scene*      screen       = nullptr;
    /// The asset graph (ResourceManager::epoch) the screen was built against: drawn only while it is
    /// current, as one swapped in later in the frame leaves its handles stale.
    uint64_t    screenAssets = 0;
    bool        water        = true;  ///< Whether the engine's water is drawn behind; else plain black.

    /// A frame shown since a LoadExpected was already the cover, so a load may hold the next; always
    /// while a host authors the world, which no load waits on.
    bool covering = true;

    bool isShowing() const { return opacity > 0.0f; }
};

} // namespace Vkm::Engine

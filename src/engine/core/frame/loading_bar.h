#pragma once

#include <algorithm>
#include <cmath>

#include "ecs/component/ui/ui_canvas.h"

namespace Vkm::Engine {

inline constexpr float LOADING_REFERENCE_HEIGHT = UICanvas::REFERENCE_HEIGHT;
    ///< The screen height, in pixels, the bar's reference pixels are measured at.
inline constexpr float LOADING_BAR_WIDTH        = 0.32f;  ///< The share of the screen's width the bar spans,
                                                          ///< centered.
inline constexpr float LOADING_BAR_AT           = 0.8f;   ///< The share of the height down its top sits.
inline constexpr float LOADING_BAR_HEIGHT       = 14.0f;  ///< Reference pixels high; its wave rises and falls
                                                          ///< within it.

/**
 * @brief The loading bar's rectangle in screen pixels, as loadingBar places it.
 */
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

} // namespace Vkm::Engine

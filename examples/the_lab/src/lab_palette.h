#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Lab {

// The colors the lab's screens share: the HUD, the signs and the pause menu.
inline const glm::vec4 INK   {0.03f, 0.04f, 0.08f, 0.92f};  ///< A panel's ground.
inline const glm::vec4 WHITE {1.00f, 1.00f, 1.00f, 1.00f};  ///< Headings.
inline const glm::vec4 SOFT  {0.86f, 0.90f, 0.96f, 1.00f};  ///< Text under a heading.
inline const glm::vec4 SHADOW{0.00f, 0.00f, 0.00f, 0.85f};  ///< Behind text over the world.

// Their canvases' UICanvas::sortOrder: a higher layer draws over a lower one.
inline constexpr int32_t LAYER_SIGNS = 5;   ///< LabSign's labels, under everything.
inline constexpr int32_t LAYER_HUD   = 10;  ///< LabHud.
inline constexpr int32_t LAYER_MENU  = 20;  ///< LabPauseMenu, over everything.

} // namespace Lab

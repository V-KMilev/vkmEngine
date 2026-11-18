#pragma once

#include <glm/glm.hpp>

#include "ecs/water/ripples.h"
#include "ecs/water/wave_surface.h"

namespace Vkm::Engine {

/**
 * @brief The scene's waves at the moment being computed (WaterSystem): what moves each water by its
 *        share of them, and what the renderer draws them by.
 */
struct WaveFrame {
    WaveSurface surface;
    Ripples     ripples;                        ///< The scene's ripples, which every water shows by its
                                                ///< share (Water::ripples).
    glm::vec2   detailDrift = glm::vec2(0.0f);  ///< How far water's maps have drifted with the wind, in
                                                ///< repeats, wrapped to [0, 1).
};

} // namespace Vkm::Engine

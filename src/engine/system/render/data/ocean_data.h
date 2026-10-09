#pragma once

#include <glm/glm.hpp>

#include "system/ocean/ocean_surface.h"

namespace Vkm::Engine {

/**
 * @brief The scene's ocean for the frame (see GLOcean): its waves at this moment, and its look.
 */
struct OceanData {
    OceanSurface surface;
    OceanRipples ripples;
    float        level       = 0.0f;             ///< World height the water rests at: its Transform's.
    glm::vec3    foamColor   = glm::vec3(1.0f);  ///< Linear.
    float        foam        = 0.0f;             ///< How readily a squeezed crest whitens.
    float        waterline   = 0.0f;             ///< Metres of foam where the water meets anything.
    float        detailScale = 1.0f;             ///< Metres across one repeat of a material's maps.
    /// How far the maps have drifted with the wind, in repeats, wrapped to [0, 1).
    glm::vec2    detailDrift = glm::vec2(0.0f);
};

} // namespace Vkm::Engine

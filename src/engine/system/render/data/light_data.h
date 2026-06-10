#pragma once

#include <glm/glm.hpp>

#include "ecs/component/light.h"

namespace Engine {

/**
 * @brief One light affecting the frame, resolved to world space.
 *
 * Covers every LightType: position/direction for punctual lights, radius for
 * finite falloff, cone angles for spots, and the half-extent axes for area
 * lights (Rect/Disk). The axes fold the light's rotation and size into two
 * world-space vectors so the backend never re-derives them.
 */
struct LightData {
    LightType type;
    glm::vec3 color;
    float     intensity;
    glm::vec3 position;

    glm::vec3 direction;    ///< Direction of the light (Directional, Spot)

    float radius;           ///< Attenuation radius (point/spot/area)

    float innerConeAngle;   ///< Spot: full-brightness cone (radians)
    float outerConeAngle;   ///< Spot: falloff edge (radians)

    glm::vec3 axisU;        ///< Rect/Disk: half-right world axis (rotation * +X * halfWidth | areaRadius)
    glm::vec3 axisV;        ///< Rect/Disk: half-up    world axis (rotation * +Y * halfHeight | areaRadius)
    bool      twoSided;     ///< Rect/Disk: emit from both faces

    bool  castShadows;
    float shadowBias;
    float shadowExtent;
};

} // namespace Engine

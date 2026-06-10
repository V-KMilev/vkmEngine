#pragma once

#include <glm/glm.hpp>

#include "ecs/component/light.h"  // LightType

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
    glm::vec3 direction;

    float radius = 10.0f;          ///< Attenuation radius (point/spot/area)

    float innerConeAngle = 0.0f;   ///< Spot: full-brightness cone (radians)
    float outerConeAngle = 0.0f;   ///< Spot: falloff edge (radians)

    glm::vec3 axisU = {0,0,0};     ///< Rect/Disk: half-right world axis (rotation * +X * halfWidth | areaRadius)
    glm::vec3 axisV = {0,0,0};     ///< Rect/Disk: half-up    world axis (rotation * +Y * halfHeight | areaRadius)
    bool      twoSided = false;    ///< Rect/Disk: emit from both faces
};

} // namespace Engine

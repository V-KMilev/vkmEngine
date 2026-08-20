#pragma once

#include <glm/glm.hpp>

#include "ecs/component/render/light.h"

namespace Vkm::Engine {

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

    glm::vec3 direction;    ///< Travel direction; filled for all types (from rotation), consumed by directional/spot

    float radius;           ///< Attenuation radius (point/spot/area)

    float innerConeAngle;   ///< Spot: full-brightness cone (radians)
    float outerConeAngle;   ///< Spot: falloff edge (radians)

    // Area-light fields, set only for Rect/Disk; defaulted so punctual lights
    // (which leave them unset in build) read as zero, matching the backend's
    // "axisU/axisV are zero for punctual lights" contract.
    glm::vec3 axisU{0.0f};  ///< Rect/Disk: half-right world axis (rotation * +X * halfWidth | areaRadius)
    glm::vec3 axisV{0.0f};  ///< Rect/Disk: half-up    world axis (rotation * +Y * halfHeight | areaRadius)
    bool      twoSided;     ///< Rect/Disk: emit from both faces

    bool  castShadows;
    float shadowBias;       ///< Depth comparison bias (slope-scaled for 2D, constant for cube)
    float shadowDistance;   ///< Directional only: max world distance the cascades cover.
};

} // namespace Vkm::Engine

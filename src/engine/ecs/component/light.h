#pragma once

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief Enumeration of light types.
 */
enum class LightType {
    Directional = 0,    ///< Directional light (sun-like, no position, only direction)
    Point       = 1,    ///< Point light (light bulb, has position and radius)
    Spot        = 2     ///< Spot light (flashlight, has position, direction, and cone)
};

/**
 * @brief Component representing a light source in the scene.
 *
 * Simple data-only component. Light calculations and culling should be handled
 * by systems that process this component.
 * 
 * For directional lights, use the Transform component's rotation to define direction.
 * For point and spot lights, use the Transform component's position.
 */
struct Light {
    LightType type  = LightType::Directional;  ///< Type of light
    glm::vec3 color = {1.0f, 1.0f, 1.0f};      ///< Light color (RGB)
    float intensity = 1.0f;                    ///< Light intensity multiplier

    // Point & Spot light parameters
    float radius = 10.0f;                      ///< Attenuation radius (for point & spot)

    // Spot light parameters
    float innerConeAngle = 0.5f;               ///< Inner cone angle in radians (full brightness)
    float outerConeAngle = 0.785f;             ///< Outer cone angle in radians (45 degrees default)

    // Shadow parameters
    bool  castShadows  = true;                 ///< Should this light cast shadows?
    float shadowBias   = 0.005f;               ///< Depth comparison bias (slope-scaled for 2D, constant for cube)
    float shadowExtent = 50.0f;                ///< Directional only: ortho half-size in world units. Ignored for spot/point (uses radius).

    bool enabled = true;                       ///< Is this light enabled?
};

} // namespace Engine

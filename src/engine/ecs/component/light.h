#pragma once

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief Enumeration of light types.
 *
 * Rect and Disk are area lights - they have a finite emissive surface, not
 * a single point. Their diffuse is the LTC Lambertian polygon integral
 * (Disk is approximated by a 12-vertex polygon) and their specular is the
 * Karis representative-point GGX with a broadened lobe; shadows are
 * currently point-style, cast from the emitter's centre.
 */
enum class LightType {
    Directional = 0,    ///< Directional light (sun-like, no position, only direction)
    Point       = 1,    ///< Point light (light bulb, has position and radius)
    Spot        = 2,    ///< Spot light (flashlight, has position, direction, and cone)
    Rect        = 3,    ///< Rectangular area light (width x height, faces -direction)
    Disk        = 4     ///< Disk area light (areaRadius, faces -direction)
};

/**
 * @brief Component representing a light source in the scene.
 *
 * Simple data-only component. Light calculations and culling should be handled
 * by systems that process this component.
 *
 * For directional lights, use the Transform component's rotation to define direction.
 * For point, spot, and area lights, use the Transform component's position.
 * Area lights face along -direction (i.e. their surface normal is -direction,
 * matching how spotlights are oriented).
 */
struct Light {
    LightType type  = LightType::Directional;  ///< Type of light
    glm::vec3 color = {1.0f, 1.0f, 1.0f};      ///< Light color (RGB)
    float intensity = 1.0f;                    ///< Light intensity multiplier

    // Point, Spot & area light parameters
    float radius = 10.0f;                      ///< Attenuation radius (point/spot/area cutoff distance)

    // Spot light parameters
    float innerConeAngle = 0.5f;               ///< Inner cone angle in radians (full brightness)
    float outerConeAngle = 0.785f;             ///< Outer cone angle in radians (45 degrees default)

    // Area light parameters (Rect, Disk)
    float areaWidth  = 1.0f;                   ///< Rect width along the local X axis
    float areaHeight = 1.0f;                   ///< Rect height along the local Y axis
    float areaRadius = 0.5f;                   ///< Disk radius
    bool  twoSided   = false;                  ///< Area lights: emit from both faces

    // Shadow parameters
    bool  castShadows  = true;                 ///< Should this light cast shadows?
    float shadowBias   = 0.005f;               ///< Depth comparison bias (slope-scaled for 2D, constant for cube)
    float shadowExtent = 50.0f;                ///< Directional only: ortho half-size in world units. Ignored for spot/point/area (uses radius).

    bool enabled = true;                       ///< Is this light enabled?
};

} // namespace Engine

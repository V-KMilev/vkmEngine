#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

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
    Disk        = 4,    ///< Disk area light (areaRadius, faces -direction)
    Count               ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};
/**
 * @brief Component representing a light source in the scene.
 *
 * For directional lights, use the Transform component's rotation to define direction.
 * For point, spot, and area lights, use the Transform component's position.
 * Area lights face along -direction (i.e. their surface normal is -direction,
 * matching how spotlights are oriented).
 */
struct Light {
    LightType type  = LightType::Directional;
    glm::vec3 color = {1.0f, 1.0f, 1.0f};      ///< Light color (RGB)
    float intensity = 1.0f;                    ///< Light intensity multiplier

    float radius = 10.0f;                      ///< Attenuation radius (point/spot/area cutoff distance)

    float innerConeAngle = 0.5f;               ///< Spot: inner cone in radians (full brightness)
    float outerConeAngle = 0.785f;             ///< Spot: outer cone in radians (45 degrees default)

    float areaWidth  = 1.0f;                   ///< Rect width along the local X axis
    float areaHeight = 1.0f;                   ///< Rect height along the local Y axis
    float areaRadius = 0.5f;                   ///< Disk radius
    bool  twoSided   = false;                  ///< Area lights: emit from both faces

    bool  castShadows    = true;
    float shadowBias     = 0.005f;             ///< Depth comparison bias (slope-scaled for 2D, constant for cube)
    float shadowDistance = 100.0f;             ///< Directional only: max world distance the cascades cover.

    bool enabled = true;
};
} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::LightType, "Directional", "Point", "Spot", "Rect", "Disk")

VKM_REFLECT_BEGIN(::Vkm::Engine::Light)
    VKM_F(type),
    VKM_F(color),
    VKM_F(intensity),
    VKM_F(radius),
    VKM_F(innerConeAngle),
    VKM_F(outerConeAngle),
    VKM_F(areaWidth),
    VKM_F(areaHeight),
    VKM_F(areaRadius),
    VKM_F(twoSided),
    VKM_F(castShadows),
    VKM_F(shadowBias),
    VKM_F(shadowDistance),
    VKM_F(enabled)
VKM_REFLECT_END()

#pragma once

#include <glm/glm.hpp>

#include "ecs/component/light.h"

namespace Engine {

/**
 * @brief Generate a directional light (sun-like).
 * 
 * @param color Light color (RGB).
 * @param intensity Light intensity multiplier.
 * @param castShadows Whether this light should cast shadows.
 * @return A Light component configured as a directional light.
 */
Light generateDirectionalLight(
    const glm::vec3& color = {1.0f, 1.0f, 1.0f},
    float intensity = 1.0f,
    bool castShadows = true
);

/**
 * @brief Generate a point light (light bulb).
 * 
 * @param color Light color (RGB).
 * @param intensity Light intensity multiplier.
 * @param radius Attenuation radius.
 * @param castShadows Whether this light should cast shadows.
 * @return A Light component configured as a point light.
 */
Light generatePointLight(
    const glm::vec3& color = {1.0f, 1.0f, 1.0f},
    float intensity = 1.0f,
    float radius = 10.0f,
    bool castShadows = true
);

/**
 * @brief Generate a spot light (flashlight).
 * 
 * @param color Light color (RGB).
 * @param intensity Light intensity multiplier.
 * @param radius Attenuation radius.
 * @param innerConeAngle Inner cone angle in radians (full brightness).
 * @param outerConeAngle Outer cone angle in radians (fade to zero).
 * @param castShadows Whether this light should cast shadows.
 * @return A Light component configured as a spot light.
 */
Light generateSpotLight(
    const glm::vec3& color = {1.0f, 1.0f, 1.0f},
    float intensity = 1.0f,
    float radius = 20.0f,
    float innerConeAngle = 0.5f,
    float outerConeAngle = 0.785f,
    bool castShadows = true
);

/**
 * @brief Generate a rectangular area light (e.g. softbox, monitor).
 *
 * Shaded with the LTC Lambertian diffuse integral and Karis representative-
 * point GGX specular. Shadows are still point-style (cast from the rect's
 * centre).
 *
 * @param color Light color (RGB).
 * @param intensity Light intensity multiplier.
 * @param width Rect width along the local X axis.
 * @param height Rect height along the local Y axis.
 * @param radius Attenuation cutoff distance.
 * @param twoSided Emit from both faces.
 * @param castShadows Whether this light should cast shadows.
 * @return A Light component configured as a rectangular area light.
 */
Light generateRectLight(
    const glm::vec3& color = {1.0f, 1.0f, 1.0f},
    float intensity = 1.0f,
    float width = 1.0f,
    float height = 1.0f,
    float radius = 15.0f,
    bool twoSided = false,
    bool castShadows = true
);

/**
 * @brief Generate a disk area light (e.g. spotlight reflector, sun-through-window).
 *
 * Shaded with the LTC Lambertian diffuse integral (12-vertex polygon
 * approximation) and Karis representative-point GGX specular (exact for
 * circles). Shadows are still point-style (cast from the disk's centre).
 *
 * @param color Light color (RGB).
 * @param intensity Light intensity multiplier.
 * @param areaRadius Disk radius (the emitter size, not the attenuation cutoff).
 * @param radius Attenuation cutoff distance.
 * @param twoSided Emit from both faces.
 * @param castShadows Whether this light should cast shadows.
 * @return A Light component configured as a disk area light.
 */
Light generateDiskLight(
    const glm::vec3& color = {1.0f, 1.0f, 1.0f},
    float intensity = 1.0f,
    float areaRadius = 0.5f,
    float radius = 15.0f,
    bool twoSided = false,
    bool castShadows = true
);

} // namespace Engine

#pragma once

#include <glm/glm.hpp>

#include "light.h"

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
    float intensity = 3.0f,
    bool castShadows = false
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
    float intensity = 10.0f,
    float radius = 10.0f,
    bool castShadows = false
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
    float intensity = 15.0f,
    float radius = 20.0f,
    float innerConeAngle = 0.5f,
    float outerConeAngle = 0.785f,
    bool castShadows = false
);

} // namespace Engine

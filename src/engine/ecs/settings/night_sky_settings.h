#pragma once

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief What the sky is once the sun is down.
 *
 * The atmosphere is nearly black with the sun down, so night's light is authored.
 * It fades in across TWILIGHT_DEGREES around the horizon.
 */
struct NightSkySettings {
    glm::vec3 radiance      = {0.004f, 0.006f, 0.014f};  ///< The skyglow that lights the night: dark, not
                                                         ///< black.
    float     moonIntensity = 1.2f;                      ///< Disc radiance; its halo follows.

    // The key light aimed at the moon once the sun is down, so night has
    // direction, shadows and speculars.
    glm::vec3 moonlightColor     = {0.55f, 0.65f, 1.0f};
    float     moonlightIntensity = 0.12f;

    static constexpr float TWILIGHT_DEGREES = 8.0f;  ///< Degrees either side of the horizon across which day
                                                     ///< hands over to night. Read by SkySystem and
                                                     ///< shaders/opengl/include/night_sky.glsl (as a sine,
                                                     ///< via the prelude).

    static constexpr float MOON_TILT           = 15.0f;   ///< Degrees off directly opposite the sun.
    static constexpr float MOON_ANGULAR_RADIUS = 0.03f;   ///< Radians.
    static constexpr float STAR_INTENSITY      = 3.0f;    ///< Clears the bloom threshold.
    static constexpr float STAR_DENSITY        = 140.0f;  ///< Higher packs more, smaller stars.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::NightSkySettings)
    VKM_F(radiance, "Lights the scene after dark, so night is dark, not black")
    VKM_F(moonIntensity, 0.0f, 10.0f, "How bright the moon looks; its halo follows")
    VKM_F(moonlightColor, "The key light's color after dark")
    VKM_F(moonlightIntensity, 0.0f, 2.0f, "How much the moon lights the world")
VKM_REFLECT_END()

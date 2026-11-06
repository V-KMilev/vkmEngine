#pragma once

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"
#include "ecs/settings/acoustics_settings.h"
#include "ecs/settings/fog_settings.h"
#include "ecs/settings/night_sky_settings.h"
#include "ecs/settings/sky_settings.h"
#include "ecs/settings/wave_settings.h"

namespace Vkm::Engine {

/**
 * @brief Where a celestial body sits, in the authored angle form.
 *
 * The sun is authored as this pair and the moon derived as one.
 */
struct SkyAngles {
    float elevation = 0.0f;  ///< Degrees above the horizon. Negative is below it.
    float azimuth   = 0.0f;  ///< Degrees around the horizon, from +Z toward +X.
};

/**
 * @brief The scene's environment: sky, night sky, fog, how its spaces sound, and its waves.
 *
 * Scene-global, not a component: one per Scene (Scene::environment()), saved
 * with it. The backend re-bakes the IBL whenever the sky changes.
 */
struct Environment {
    SkySettings       sky;
    NightSkySettings  night;
    FogSettings       fog;
    AcousticsSettings acoustics;
    WaveSettings      waves;

    /**
     * @brief Where the sun sits, straight off the authored fields.
     *
     * @return Elevation/azimuth in degrees.
     */
    SkyAngles sunAngles() const {
        return {sky.sunElevation, sky.sunAzimuth};
    }

    /**
     * @brief Where the moon sits: opposite the sun, tilted off that axis.
     *
     * Derived, so lowering the sun raises the moon by itself.
     *
     * @return Elevation/azimuth in degrees.
     */
    SkyAngles moonAngles() const {
        return {-sky.sunElevation + NightSkySettings::MOON_TILT, sky.sunAzimuth + 180.0f};
    }

    /**
     * @brief Direction TO the sun from the authored angles.
     *
     * Azimuth 0 is +Z by definition of the angles, not the engine's forward.
     *
     * @return Unit direction pointing at the sun.
     */
    glm::vec3 sunDirection() const {
        const SkyAngles a = sunAngles();
        return directionFromAngles(a.elevation, a.azimuth);
    }

    /**
     * @brief Direction TO the moon.
     *
     * @return Unit direction pointing at the moon.
     */
    glm::vec3 moonDirection() const {
        const SkyAngles a = moonAngles();
        return directionFromAngles(a.elevation, a.azimuth);
    }

    /**
     * @brief Unit direction for an elevation/azimuth pair, both in degrees.
     *
     * @param elevationDeg Degrees above the horizon.
     * @param azimuthDeg Degrees around the horizon, from +Z toward +X.
     * @return Unit direction.
     */
    static glm::vec3 directionFromAngles(float elevationDeg, float azimuthDeg);
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Environment)
    VKM_F(sky)
    VKM_F(night)
    VKM_F(fog)
    VKM_F(acoustics)
    VKM_F(waves)
VKM_REFLECT_END()

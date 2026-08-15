#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief The scene's global settings: the lighting environment (equirectangular
 *        HDR baked into the IBL product set and drawn as the skybox, plus
 *        brightness + visibility) together with the physics-world parameters.
 *
 * Scene-global state, NOT an entity/component - one per Scene, always present
 * (owned by Scene::environment()), and it round-trips with the scene. The
 * backend re-bakes the IBL whenever hdrPath changes; the skybox samples that
 * baked product, so the visible background follows the swap automatically. The
 * physics fields are read once per fixed step by PhysicsSystem.
 */
/**
 * @brief Sky and image-based lighting: what the world is lit by and set against.
 */
struct SkySettings {
    std::string hdrPath;                ///< Equirect HDR baked into IBL + skybox; empty = none.
    float       intensity  = 1.0f;      ///< Indirect-lighting + skybox brightness multiplier.
    /// Draw the skybox background; the IBL still lights the scene when off.
    bool        showSkybox = true;

    // Procedural sky: a Rayleigh + Mie atmosphere baked into the IBL cubemap in
    // place of loading hdrPath. On by default so a scene is lit before it owns
    // any assets.
    bool  procedural       = true;   ///< Bake the atmosphere instead of loading hdrPath.
    float sunIntensity     = 22.0f;  ///< Atmosphere sun radiance scale.
    float rayleigh         = 1.0f;   ///< Rayleigh (blue-sky) scattering scale.
    float mie              = 1.0f;   ///< Mie (haze / sun glow) scattering scale.
    float mieG             = 0.76f;  ///< Mie phase asymmetry (0..0.99; higher = tighter sun glow).
    /// Analytic sun-disc radius in the skybox (radians; ~0.0047 is life-size).
    float sunAngularRadius = 0.02f;
    float sunDiscIntensity = 15.0f;  ///< Analytic sun-disc radiance (added over the atmospheric glow).

    // Where the sun is, and therefore what time of day it is - the sky's one
    // real control; everything else here is appearance. Kept here rather than
    // read off a directional light because the sky is scene-global and must work
    // whether or not the scene has one. SkySystem points a light FROM these, so
    // the key light and the sky cannot disagree. Below the horizon is night.
    float sunElevation = 50.0f;  ///< Degrees above the horizon. Negative is night.
    float sunAzimuth   = 30.0f;  ///< Degrees around the horizon, from +Z toward +X.

    // The key light's daylight settings. Here rather than on the Light because
    // the sky drives that light: it has to know what full daylight looks like to
    // hand over to moonlight and back. Its own colour/intensity are unused while
    // the procedural sky is on.
    glm::vec3 lightColor     = {1.0f, 0.96f, 0.90f};  ///< Key light colour at midday.
    float     lightIntensity = 3.0f;                  ///< Key light intensity at midday.
};

/**
 * @brief What the sky is once the sun is down.
 *
 * Single scattering with the sun below the horizon is very nearly black -
 * physically right and useless to light by - so night's own light is authored.
 * It fades in across a twilight band around the horizon (_common/sky.glsl)
 * rather than switching at exactly zero.
 */
struct NightSkySettings {
    /// Skyglow the scene is lit by; the floor that keeps night dark, not black.
    glm::vec3 radiance = {0.004f, 0.006f, 0.014f};
    /// Degrees off the point exactly opposite the sun, so the two are not a mirror.
    float     moonTilt          = 15.0f;
    float     moonAngularRadius = 0.03f;            ///< Moon disc radius in the skybox (radians).
    float     moonIntensity     = 1.2f;             ///< Moon disc radiance; its halo follows.
    /// Star brightness; 0 disables the field. Tuned so the brightest cores clear
    /// the bloom threshold while the faint ones stay faint.
    float     starIntensity     = 3.0f;
    float     starDensity       = 140.0f;           ///< Grid density; higher packs more, smaller stars.

    // Moonlight: the key light again, aimed at the moon once the sun is down.
    // Real light rather than a painted glow, so night has direction, shadows and
    // speculars. Dim and blue because moonlight is sunlight scattered twice.
    glm::vec3 moonlightColor     = {0.55f, 0.65f, 1.0f};  ///< Key light colour at night.
    /// Key light intensity at night. Moonlight is a tiny fraction of daylight.
    float     moonlightIntensity = 0.12f;
};

/**
 * @brief Froxel volumetric fog: a compute pass scatters the scene lights through
 *        a height-falloff medium and applies it to the frame.
 */
struct FogSettings {
    bool      enabled       = false;
    float     density       = 0.03f;               ///< Base extinction at height.
    float     height        = 5.0f;                ///< World Y where the medium is densest.
    float     heightFalloff = 0.15f;               ///< Density e-folding per world unit above height.
    float     anisotropy    = 0.7f;                ///< Henyey-Greenstein g (forward scatter).
    glm::vec3 albedo        = {0.8f, 0.85f, 1.0f}; ///< Scattering tint.
    /// Froxel grid width (screen tiles). Higher = sharper shafts, more compute.
    uint32_t  resolutionX   = 160;
    uint32_t  resolutionY   = 90;                  ///< Froxel grid height (screen tiles).
    uint32_t  resolutionZ   = 64;                  ///< Froxel grid depth  (exponential slices).
};

/**
 * @brief The physics world's own parameters, read once per fixed step.
 *
 * Scene-global like the Environment, and deliberately NOT part of it: what the
 * world is lit by and what it falls at are unrelated, and every engine that has
 * both keeps them apart. Owned by Scene, beside the Environment.
 */
struct PhysicsSettings {
    glm::vec3 gravity          = {0.0f, -9.81f, 0.0f};  ///< World gravity (m/s^2).
    int       solverIterations = 8;                     ///< PGS solver passes per fixed step.
};

struct Environment {
    SkySettings      sky;
    NightSkySettings night;
    FogSettings      fog;

    /**
     * @brief Direction TO the sun from the authored angles.
     *
     * The one place elevation/azimuth become a vector, so the sky bake, the
     * skybox and the light that follows them cannot each roll their own and
     * drift. Matches the engine's forward convention (+Z at azimuth 0).
     *
     * @return Unit direction pointing at the sun.
     */
    glm::vec3 sunDirection() const {
        return directionFromAngles(sky.sunElevation, sky.sunAzimuth);
    }

    /**
     * @brief Direction TO the moon, opposite the sun and tilted off that axis.
     *
     * Derived rather than authored: a moon is only interesting relative to the
     * sun, and tying them means dropping the sun below the horizon raises the
     * moon by itself. moonTilt keeps the two from being an exact mirror.
     *
     * @return Unit direction pointing at the moon.
     */
    glm::vec3 moonDirection() const {
        return directionFromAngles(-sky.sunElevation + night.moonTilt, sky.sunAzimuth + 180.0f);
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

VKM_REFLECT_BEGIN(SkySettings)
    VKM_F(hdrPath),
    VKM_F(intensity),
    VKM_F(showSkybox),
    VKM_F(procedural),
    VKM_F(sunIntensity),
    VKM_F(rayleigh),
    VKM_F(mie),
    VKM_F(mieG),
    VKM_F(sunAngularRadius),
    VKM_F(sunDiscIntensity),
    VKM_F(sunElevation),
    VKM_F(sunAzimuth),
    VKM_F(lightColor),
    VKM_F(lightIntensity)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(NightSkySettings)
    VKM_F(radiance),
    VKM_F(moonTilt),
    VKM_F(moonAngularRadius),
    VKM_F(moonIntensity),
    VKM_F(starIntensity),
    VKM_F(starDensity),
    VKM_F(moonlightColor),
    VKM_F(moonlightIntensity)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(FogSettings)
    VKM_F(enabled),
    VKM_F(density),
    VKM_F(height),
    VKM_F(heightFalloff),
    VKM_F(anisotropy),
    VKM_F(albedo),
    VKM_F(resolutionX),
    VKM_F(resolutionY),
    VKM_F(resolutionZ)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(PhysicsSettings)
    VKM_F(gravity),
    VKM_F(solverIterations)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(Environment)
    VKM_F(sky),
    VKM_F(night),
    VKM_F(fog)
VKM_REFLECT_END()

} // namespace Engine

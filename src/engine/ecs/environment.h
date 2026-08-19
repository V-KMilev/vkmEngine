#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

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
    bool  procedural       = true;
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
 * Scene-global like the Environment and deliberately not part of it: what the
 * world is lit by and what it falls at are unrelated. Owned by Scene, beside
 * the Environment.
 */
struct PhysicsSettings {
    glm::vec3 gravity          = {0.0f, -9.81f, 0.0f};  ///< World gravity (m/s^2).
    int       solverIterations = 8;                     ///< PGS solver passes per fixed step.
};

/**
 * @brief Where a celestial body sits, in the authored angle form.
 *
 * The sun is authored as this pair and the moon is derived as one, so anything
 * that needs a body's placement - a direction vector, a light's rotation - can
 * take the angles rather than re-deriving them from the raw fields.
 */
struct SkyAngles {
    float elevation = 0.0f;  ///< Degrees above the horizon. Negative is below it.
    float azimuth   = 0.0f;  ///< Degrees around the horizon, from +Z toward +X.
};

/**
 * @brief The scene's lighting environment: sky, night sky and fog.
 *
 * Scene-global state, NOT an entity/component - one per Scene, always present
 * (owned by Scene::environment()), and it round-trips with the scene. The
 * backend re-bakes the IBL whenever the sky changes; the skybox samples that
 * baked product, so the visible background follows automatically.
 */
struct Environment {
    SkySettings      sky;
    NightSkySettings night;
    FogSettings      fog;

    /**
     * @brief Where the sun sits, straight off the authored fields.
     *
     * @return The sun's elevation/azimuth in degrees.
     */
    SkyAngles sunAngles() const {
        return {sky.sunElevation, sky.sunAzimuth};
    }

    /**
     * @brief Where the moon sits: opposite the sun, tilted off that axis.
     *
     * Derived rather than authored: a moon is only interesting relative to the
     * sun, and tying them means dropping the sun below the horizon raises the
     * moon by itself. moonTilt keeps the two from being an exact mirror. This is
     * the one place that derivation lives, so the drawn moon and the light aimed
     * at it cannot drift apart.
     *
     * @return The moon's elevation/azimuth in degrees.
     */
    SkyAngles moonAngles() const {
        return {-sky.sunElevation + night.moonTilt, sky.sunAzimuth + 180.0f};
    }

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
        const SkyAngles a = sunAngles();
        return directionFromAngles(a.elevation, a.azimuth);
    }

    /**
     * @brief Direction TO the moon.
     *
     * The vector form of moonAngles(), which is where the placement is decided.
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

VKM_REFLECT_BEGIN(::Vkm::Engine::SkySettings)
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

VKM_REFLECT_BEGIN(::Vkm::Engine::NightSkySettings)
    VKM_F(radiance),
    VKM_F(moonTilt),
    VKM_F(moonAngularRadius),
    VKM_F(moonIntensity),
    VKM_F(starIntensity),
    VKM_F(starDensity),
    VKM_F(moonlightColor),
    VKM_F(moonlightIntensity)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::FogSettings)
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

VKM_REFLECT_BEGIN(::Vkm::Engine::PhysicsSettings)
    VKM_F(gravity),
    VKM_F(solverIterations)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::Environment)
    VKM_F(sky),
    VKM_F(night),
    VKM_F(fog)
VKM_REFLECT_END()

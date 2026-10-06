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
    float       intensity  = 1.0f;      ///< Indirect-lighting + skybox brightness.
    bool        showSkybox = true;      ///< The IBL still lights the scene when off.

    // A Rayleigh + Mie atmosphere baked into the IBL in place of hdrPath; on by
    // default so a scene is lit before it owns any assets.
    bool  procedural       = true;
    float sunIntensity     = 22.0f;  ///< Atmosphere sun radiance scale.
    float rayleigh         = 1.0f;   ///< Blue-sky scattering scale.
    float mie              = 1.0f;   ///< Haze / sun glow scattering scale.
    float mieG             = 0.76f;  ///< Phase asymmetry, 0..MAX_MIE_G; higher = tighter glow.
    float sunAngularRadius = 0.02f;  ///< Radians; ~0.0047 is life-size.
    float sunDiscIntensity = 15.0f;  ///< Added over the atmospheric glow.

    // SkySystem aims the key light from these, so the drawn sun and the shadows agree.
    float sunElevation = 50.0f;  ///< Degrees above the horizon. Negative is night.
    float sunAzimuth   = 30.0f;  ///< Degrees around the horizon, from +Z toward +X.

    // The key light's daylight end; SkySystem blends it with the moonlight, so
    // the Light's own colour and intensity are unused under the procedural sky.
    glm::vec3 lightColor     = {1.0f, 0.96f, 0.90f};  ///< With the sun overhead.
    float     lightIntensity = 3.0f;                  ///< At midday.

    /// At 1 the phase function divides zero by zero.
    static constexpr float MAX_MIE_G = 0.99f;
};

/**
 * @brief What the sky is once the sun is down.
 *
 * Single scattering at night is nearly black, so night's light is authored. It
 * fades in across TWILIGHT_DEGREES around the horizon.
 */
struct NightSkySettings {
    /// Skyglow the scene is lit by, so night is dark, not black.
    glm::vec3 radiance = {0.004f, 0.006f, 0.014f};
    float     moonTilt          = 15.0f;            ///< Degrees off directly opposite the sun.
    float     moonAngularRadius = 0.03f;            ///< Radians.
    float     moonIntensity     = 1.2f;             ///< Disc radiance; its halo follows.
    float     starIntensity     = 3.0f;             ///< 0 disables; the default clears the bloom threshold.
    float     starDensity       = 140.0f;           ///< Higher packs more, smaller stars.

    // The key light aimed at the moon once the sun is down, so night has
    // direction, shadows and speculars.
    glm::vec3 moonlightColor     = {0.55f, 0.65f, 1.0f};
    float     moonlightIntensity = 0.12f;

    /**
     * @brief Degrees either side of the horizon across which day hands over to night.
     *
     * Read by SkySystem and shaders/sky.glsl (as a sine, via the prelude).
     */
    static constexpr float TWILIGHT_DEGREES = 8.0f;
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
    float     anisotropy    = 0.7f;                ///< Henyey-Greenstein g, within MAX_ANISOTROPY.
    glm::vec3 albedo        = {0.8f, 0.85f, 1.0f}; ///< Scattering tint.
    uint32_t  resolutionX   = 160;                 ///< Screen tiles; higher = sharper shafts.
    uint32_t  resolutionY   = 90;                  ///< Screen tiles.
    uint32_t  resolutionZ   = 64;                  ///< Exponential slices.
    /**
     * @brief How far from the eye the slices reach, in metres.
     *
     * Or the far plane if nearer; a point beyond takes the fog accumulated to here.
     */
    float     maxDistance   = 200.0f;

    /// At +-1 the phase function divides zero by zero.
    static constexpr float MAX_ANISOTROPY = 0.95f;

    static constexpr uint32_t MIN_FROXELS = 16;   ///< Per axis.
    static constexpr uint32_t MAX_FROXELS = 512;  ///< Per axis.
    /**
     * @brief The most froxels the whole grid holds; a grid over it is scaled down evenly.
     *
     * Holds the two RGBA16F volumes to 64 MB, against a gigabyte each at 512^3.
     */
    static constexpr uint32_t MAX_FROXEL_COUNT = 1u << 22;

    /**
     * @brief The grid the volume is allocated at: the authored one, bounded.
     *
     * Each axis clamped, then all scaled evenly to fit MAX_FROXEL_COUNT.
     *
     * @return Froxels across, up and deep.
     */
    glm::uvec3 froxelGrid() const;
};

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
 * @brief The scene's lighting environment: sky, night sky and fog.
 *
 * Scene-global, not a component: one per Scene (Scene::environment()), saved
 * with it. The backend re-bakes the IBL whenever the sky changes.
 */
struct Environment {
    SkySettings      sky;
    NightSkySettings night;
    FogSettings      fog;

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
        return {-sky.sunElevation + night.moonTilt, sky.sunAzimuth + 180.0f};
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

VKM_REFLECT_BEGIN(::Vkm::Engine::SkySettings)
    VKM_F(hdrPath)
    VKM_F(intensity)
    VKM_F(showSkybox)
    VKM_F(procedural)
    VKM_F(sunIntensity)
    VKM_F(rayleigh)
    VKM_F(mie)
    VKM_F(mieG)
    VKM_F(sunAngularRadius)
    VKM_F(sunDiscIntensity)
    VKM_F(sunElevation)
    VKM_F(sunAzimuth)
    VKM_F(lightColor)
    VKM_F(lightIntensity)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::NightSkySettings)
    VKM_F(radiance)
    VKM_F(moonTilt)
    VKM_F(moonAngularRadius)
    VKM_F(moonIntensity)
    VKM_F(starIntensity)
    VKM_F(starDensity)
    VKM_F(moonlightColor)
    VKM_F(moonlightIntensity)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::FogSettings)
    VKM_F(enabled)
    VKM_F(density)
    VKM_F(height)
    VKM_F(heightFalloff)
    VKM_F(anisotropy)
    VKM_F(albedo)
    VKM_F(resolutionX)
    VKM_F(resolutionY)
    VKM_F(resolutionZ)
    VKM_F(maxDistance)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::Environment)
    VKM_F(sky)
    VKM_F(night)
    VKM_F(fog)
VKM_REFLECT_END()

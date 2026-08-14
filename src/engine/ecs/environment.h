#pragma once

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
struct Environment {
    // Lighting (skybox + image-based lighting)
    std::string hdrPath;                ///< Equirect HDR baked into IBL + skybox; empty = none.
    float       intensity  = 1.0f;      ///< Indirect-lighting + skybox brightness multiplier.
    bool        showSkybox = true;      ///< Draw the skybox background; the IBL still lights the scene when off.

    // Procedural sky: a Rayleigh + Mie atmosphere baked into the IBL cubemap in
    // place of loading hdrPath. The sun direction follows the scene's primary
    // directional light, so the sky and the key light stay consistent, and the
    // bake re-runs only when the sun or a parameter below changes. On by
    // default so a scene is lit before it owns any assets.
    bool  proceduralSky   = true;   ///< Bake the atmosphere instead of loading hdrPath.
    float skySunIntensity = 22.0f;  ///< Atmosphere sun radiance scale.
    float skyRayleigh     = 1.0f;   ///< Rayleigh (blue-sky) scattering scale.
    float skyMie          = 1.0f;   ///< Mie (haze / sun glow) scattering scale.
    float skyMieG         = 0.76f;  ///< Mie phase asymmetry (0..0.99; higher = tighter sun glow).
    float skySunAngularRadius = 0.02f;  ///< Analytic sun-disc radius in the skybox (radians; ~0.0047 is life-size).
    float skySunDiscIntensity = 15.0f;  ///< Analytic sun-disc radiance (added over the atmospheric glow).

    // Volumetric fog (froxel): a compute pass scatters the scene lights through a
    // height-falloff medium and applies it to the frame.
    bool      fogEnabled       = false;
    float     fogDensity       = 0.03f;               ///< Base extinction at fogHeight.
    float     fogHeight        = 5.0f;                ///< World Y where the medium is densest.
    float     fogHeightFalloff = 0.15f;              ///< Density e-folding per world unit above fogHeight.
    float     fogAnisotropy    = 0.7f;               ///< Henyey-Greenstein g (forward scattering toward lights).
    glm::vec3 fogAlbedo        = {0.8f, 0.85f, 1.0f}; ///< Scattering tint.
    uint32_t  fogResolutionX   = 160;                ///< Froxel grid width  (screen tiles). Higher = sharper light shafts, more compute.
    uint32_t  fogResolutionY   = 90;                 ///< Froxel grid height (screen tiles).
    uint32_t  fogResolutionZ   = 64;                 ///< Froxel grid depth  (exponential slices).

    // Physics world
    glm::vec3 gravity          = {0.0f, -9.81f, 0.0f};       ///< World gravity (m/s^2).
    int       solverIterations = 8;                          ///< PGS solver passes per fixed step.
};

VKM_REFLECT_BEGIN(Environment)
    VKM_F(hdrPath),
    VKM_F(intensity),
    VKM_F(showSkybox),
    VKM_F(proceduralSky),
    VKM_F(skySunIntensity),
    VKM_F(skyRayleigh),
    VKM_F(skyMie),
    VKM_F(skyMieG),
    VKM_F(skySunAngularRadius),
    VKM_F(skySunDiscIntensity),
    VKM_F(fogEnabled),
    VKM_F(fogDensity),
    VKM_F(fogHeight),
    VKM_F(fogHeightFalloff),
    VKM_F(fogAnisotropy),
    VKM_F(fogAlbedo),
    VKM_F(fogResolutionX),
    VKM_F(fogResolutionY),
    VKM_F(fogResolutionZ),
    VKM_F(gravity),
    VKM_F(solverIterations)
VKM_REFLECT_END()

} // namespace Engine

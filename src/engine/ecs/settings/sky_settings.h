#pragma once

#include <string>

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Sky and image-based lighting: what the world is lit by and set against.
 */
struct SkySettings {
    std::string hdrPath;            ///< Equirect HDR baked into IBL + skybox; empty = none.
    float       intensity  = 1.0f;  ///< Indirect-lighting + skybox brightness.
    bool        showSkybox = true;  ///< The IBL still lights the scene when off.

    // A Rayleigh, Mie and ozone atmosphere baked into the IBL in place of hdrPath,
    // lit by the sun authored further down; on by default so a scene is lit before it owns
    // any assets.
    bool  procedural       = true;
    float mie              = 5.0f;  ///< Haze / sun glow scattering scale; 1 is pristine air.
    float sunDiscIntensity = 5.0f;  ///< The drawn disc's radiance per unit of lightIntensity, added over the
                                    ///< glow; its size is the key light's sourceRadius.

    // SkySystem aims the key light from these, so the drawn sun and the shadows agree.
    float sunElevation = 50.0f;  ///< Degrees above the horizon. Negative is night.
    float sunAzimuth   = 30.0f;  ///< Degrees around the horizon, from +Z toward +X.

    // The sun: the key light's daylight end, which SkySystem blends with the moonlight,
    // and what lights the sky (Atmosphere::solarIlluminance). The Light's own color and
    // intensity are unused under the procedural sky.
    glm::vec3 lightColor     = {1.0f, 0.96f, 0.90f};  ///< With the sun overhead.
    float     lightIntensity = 3.0f;                  ///< Illuminance with the sun overhead.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::SkySettings)
    VKM_F(hdrPath, "The skybox's HDR, under the project")
    VKM_F(intensity, 0.0f, 3.0f, "Indirect light's strength")
    VKM_F(showSkybox)
    VKM_F(procedural, "Bakes an atmosphere instead of the HDR, and drives the key light")
    VKM_F(mie, 0.0f, 10.0f, "Haze: the white round the sun; 1 is pristine air")
    VKM_F(sunDiscIntensity, 0.0f, 20.0f, "The drawn sun per unit of its light")
    VKM_F(sunElevation, -90.0f, 90.0f, "Above the horizon; below zero is night")
    VKM_F(sunAzimuth, -180.0f, 180.0f, "Around the horizon, from +Z toward +X")
    VKM_F(lightColor, "The key light's color with the sun overhead")
    VKM_F(lightIntensity, 0.0f, 20.0f, "The sun's light overhead; the sky follows it")
VKM_REFLECT_END()

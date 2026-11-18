#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"
#include "ecs/component/light/light.h"

namespace Vkm::Engine {

/**
 * @brief Froxel volumetric fog: a compute pass scatters the scene lights through a height-falloff
 *        medium, which everything drawn through it takes at its own depth as it is drawn.
 */
struct FogSettings {
    bool      enabled       = false;
    float     density       = 0.03f;                   ///< Base extinction at height.
    float     height        = 5.0f;                    ///< World Y where the medium is densest.
    float     heightFalloff = 0.15f;                   ///< Density e-folding per world unit above height.
    float     anisotropy    = 0.7f;                    ///< Henyey-Greenstein g, within MAX_ANISOTROPY.
    glm::vec3 albedo        = {0.8f, 0.85f, 1.0f};     ///< Scattering tint.
    float     maxDistance   = Light{}.shadowDistance;  ///< How far from the eye the slices reach, in meters,
                                                       ///< or the far plane if nearer; a point beyond takes
                                                       ///< the fog accumulated to here. The sun's default
                                                       ///< shadowDistance, so no sunlight scatters past its
                                                       ///< shadows unshadowed.

    static constexpr float MAX_ANISOTROPY = 0.95f;  ///< At +-1 the phase function divides zero by zero.

    static constexpr uint32_t FROXELS_X = 96;  ///< Screen tiles; higher = sharper shafts.
    static constexpr uint32_t FROXELS_Y = 54;  ///< Screen tiles.
    static constexpr uint32_t FROXELS_Z = 48;  ///< Exponential slices.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::FogSettings)
    VKM_F(enabled, "Fog scattering the scene's lights, thinning with height")
    VKM_F(density, 0.0f, 0.3f, "How thick it is where densest")
    VKM_F(height, -100.0f, 1000.0f, "Where it is densest")
    VKM_F(heightFalloff, 0.0f, 1.0f, "How fast it thins above Height")
    VKM_F(anisotropy, -FogSettings::MAX_ANISOTROPY, FogSettings::MAX_ANISOTROPY, "How much it scatters forward, toward the light")
    VKM_F(albedo)
    VKM_F(maxDistance, 10.0f, 2000.0f, "How far from the eye it is drawn")
VKM_REFLECT_END()

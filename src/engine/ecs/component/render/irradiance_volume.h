#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Baked global illumination: a grid of irradiance probes filling a box.
 *
 * Each probe stores the scene captured offline as spherical harmonics; objects
 * in the box sample it trilinearly for indirect diffuse only. The box is centred
 * on the Transform. A scene uses one: @ref findIrradianceVolume picks it.
 */
struct IrradianceVolume {
    /**
     * @brief Most probes one axis may carry.
     *
     * The bake runs inside one frame and renders around every probe (see
     * GLIrradianceBaker::bake), so the cost is cubic in this.
     */
    static constexpr uint32_t MAX_RESOLUTION = 64;

    glm::vec3 halfExtents = glm::vec3(10.0f, 5.0f, 10.0f);  ///< World units.

    // Probe counts per axis.
    uint32_t resolutionX = 8;
    uint32_t resolutionY = 4;
    uint32_t resolutionZ = 8;

    float intensity = 1.0f;  ///< Linear-HDR multiplier.

    /**
     * @brief How far inside the box its light fades in, in metres.
     *
     * Nearer a face, light blends toward the sky's, so the edge is no seam.
     * A distance, not a share, so fades are even on a long flat box. 0 is a hard edge.
     */
    float blendDistance = 1.0f;

    /**
     * @brief Bump to force a re-bake.
     *
     * For changes the params miss (sun moved, geometry edited); moving or resizing re-bakes alone.
     */
    uint32_t bakeVersion = 0;

    /**
     * @brief The probe grid size every reader walks; loading does not clamp, so readers clamp here.
     *
     * Also keeps the product of the three inside a uint32.
     *
     * @param volume The volume asked about.
     * @return Each axis, clamped to MAX_RESOLUTION.
     */
    static glm::uvec3 clampedResolution(const IrradianceVolume& volume) {
        const auto axis = [](uint32_t asked) {
            return asked > MAX_RESOLUTION ? MAX_RESOLUTION : asked;
        };
        return glm::uvec3(axis(volume.resolutionX), axis(volume.resolutionY), axis(volume.resolutionZ));
    }
};

/**
 * @brief The scene's irradiance volume: the lowest-slot entity carrying one.
 *
 * A Transform is required too.
 *
 * @param scene Scene searched.
 * @return The volume entity, or {} when there is none.
 */
EntityId findIrradianceVolume(const Scene& scene);

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::IrradianceVolume)
    VKM_F(halfExtents)
    VKM_F(resolutionX)
    VKM_F(resolutionY)
    VKM_F(resolutionZ)
    VKM_F(intensity)
    VKM_F(blendDistance)
    VKM_F(bakeVersion)
VKM_REFLECT_END()

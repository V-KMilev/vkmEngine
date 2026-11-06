#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Baked global illumination: a grid of irradiance probes filling a box.
 *
 * Each probe stores the scene captured offline as spherical harmonics; objects
 * in the box sample it trilinearly for indirect diffuse only. The box is centered
 * on the Transform. A scene uses one: findIrradianceVolume() picks it.
 */
struct IrradianceVolume {
    static constexpr uint32_t MAX_RESOLUTION = 64;  ///< Most probes one axis may carry. The backend's bake
                                                    ///< renders around every probe, a slice of them a frame;
                                                    ///< MAX_PROBES bounds its cost.

    static constexpr uint32_t MAX_PROBES = 4096;  ///< Most probes the whole grid may carry. Each keeps what
                                                  ///< it sees on the GPU for relighting, at the face size of
                                                  ///< the backend's bake.

    glm::vec3 halfExtents = glm::vec3(10.0f, 5.0f, 10.0f);  ///< World units.

    // Probe counts per axis.
    uint32_t resolutionX = 8;
    uint32_t resolutionY = 4;
    uint32_t resolutionZ = 8;

    float intensity = 1.0f;  ///< Linear-HDR multiplier.

    float blendDistance = 1.0f;  ///< How far inside the box its light fades in, in meters. Nearer a face,
                                 ///< light blends toward the sky's, so the edge is no seam. A distance, not a
                                 ///< share, so fades are even on a long flat box. 0 is a hard edge.

    uint32_t bakeVersion = 0;  ///< Bump to force a re-bake. For changes the params miss (geometry edited);
                               ///< moving or resizing re-bakes alone, and a change of the lights or the sky
                               ///< relights the bake without one.

    /**
     * @brief The probe grid size every reader walks; loading does not clamp, so readers clamp here.
     *
     * @param volume The volume asked about.
     * @return Each axis clamped to MAX_RESOLUTION, then all three scaled down alike until the
     *         grid holds at most MAX_PROBES.
     */
    static glm::uvec3 clampedResolution(const IrradianceVolume& volume);
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
    VKM_F(halfExtents, 0.1f, 1000.0f, "Half its box, in meters")
    VKM_F(resolutionX, 1.0f, IrradianceVolume::MAX_RESOLUTION, "Probes along X")
    VKM_F(resolutionY, 1.0f, IrradianceVolume::MAX_RESOLUTION, "Probes along Y")
    VKM_F(resolutionZ, 1.0f, IrradianceVolume::MAX_RESOLUTION, "Probes along Z")
    VKM_F(intensity, 0.0f, 8.0f, "Scales its baked light")
    VKM_F(blendDistance, 0.0f, 50.0f, "How far inside the box its light fades in; 0 is a hard edge")
    VKM_F(bakeVersion)
VKM_REFLECT_END()

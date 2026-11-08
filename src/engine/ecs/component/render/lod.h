#pragma once

#include <string>
#include <vector>

#include "base/reflect/reflect.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief One detail level: the mesh to draw while the entity is near enough.
 */
struct LODLevel {
    MeshHandle mesh;
    float      maxDistance = 0.0f;  ///< Used while the camera is within this range.
};

/**
 * @brief Distance-selected geometry for an entity.
 *
 * Replaces only the Mesh component's geometry. Levels run near to far, selected
 * in the visibility cull on bounds-center distance scaled to REFERENCE_P11's field
 * of view; past a level's range the next fades in, across Culling::FADE_BAND of it.
 * Past the last level it keeps drawing, to the camera's far plane.
 */
struct LOD {
    std::vector<LODLevel> levels;

    float bias = 1.0f;  ///< Scales every level's range; >1 keeps detail longer.

    static constexpr float REFERENCE_P11 = 1.7320508f;  ///< projection[1][1] of the 60-degree vertical field
                                                        ///< of view level ranges are set against.

    /**
     * @brief The name of the coarser mesh that stands in for another at a level.
     *
     * @param base  The mesh's name.
     * @param level The level below it, from 1.
     * @return The level's asset name.
     */
    static std::string levelName(const std::string& base, uint32_t level) {
        return base + ":lod" + std::to_string(level);
    }
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::LODLevel)
    VKM_F(mesh)
    VKM_F(maxDistance, 0.0f, 100000.0f, "Drawn while the camera is within this range")
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::LOD)
    VKM_F(levels)
    VKM_F(bias, 0.1f, 4.0f, "Scales every level's range: above 1 keeps detail further out")
VKM_REFLECT_END()

#pragma once

#include <vector>

#include "core/reflect.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief One detail level: the mesh to draw while the entity is near enough.
 */
struct LODLevel {
    MeshHandle mesh;
    float      maxDistance = 0.0f;    ///< Used while the camera is within this range.
};

/**
 * @brief Distance-selected geometry for an entity.
 *
 * Replaces only the Mesh component's geometry. Levels run near to far, selected
 * in the visibility cull on bounds-centre distance scaled to REFERENCE_P11's field
 * of view. Past the last level it keeps drawing; Culling::isNearEnough drops entities.
 */
struct LOD {
    std::vector<LODLevel> levels;

    /**
     * @brief Scales every level's range; >1 keeps detail longer.
     */
    float bias = 1.0f;

    /// projection[1][1] of the 60-degree vertical field of view level ranges are set against.
    static constexpr float REFERENCE_P11 = 1.7320508f;
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::LODLevel)
    VKM_F(mesh)
    VKM_F(maxDistance)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::LOD)
    VKM_F(levels)
    VKM_F(bias)
VKM_REFLECT_END()

#pragma once

#include <vector>

#include "mesh_asset.h"
#include "material_asset.h"

namespace Engine {

/**
 * @brief LOD level definition for a mesh.
 */
struct MeshLODLevel {
    MeshHandle mesh;       ///< Mesh to use at this LOD level
    float maxDistance;     ///< Maximum distance for this LOD (squared for fast comparison)
};

/**
 * @brief Component representing a renderable mesh (geometry + material) in the world.
 *
 * Simple data-only component. Holds references to mesh and material data, as well as visibility flag.
 * Supports LOD (Level of Detail) with multiple mesh levels.
 */
struct Mesh {
    MeshHandle mesh;          ///< Handle to mesh geometry (LOD 0 / base mesh)
    MaterialHandle material;  ///< Handle to material
    bool visible = true;      ///< Is mesh visible?

    // LOD support (optional)
    std::vector<MeshLODLevel> lodLevels;  ///< Additional LOD levels (sorted by distance)
    float lodBias = 0.0f;                  ///< Distance bias for LOD selection

    /**
     * @brief Get the appropriate mesh for a given squared distance.
     * @param distanceSquared Squared distance from camera.
     * @return Mesh handle to use.
     */
    MeshHandle getMeshForDistance(float distanceSquared) const {
        if (lodLevels.empty()) {
            return mesh;
        }

        float biasedDist = distanceSquared + lodBias;
        for (const auto& lod : lodLevels) {
            if (biasedDist <= lod.maxDistance * lod.maxDistance) {
                return lod.mesh;
            }
        }
        return lodLevels.back().mesh;
    }

    /**
     * @brief Check if this mesh has LOD levels.
     */
    bool hasLOD() const { return !lodLevels.empty(); }
};

} // namespace Engine

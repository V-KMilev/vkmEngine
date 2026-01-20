#pragma once

#include <glm/glm.hpp>

#include "material_asset.h"
#include "mesh_asset.h"

namespace Engine {

/**
 * @brief Component representing a renderable mesh (geometry + material) in the world.
 *
 * Simple data-only component. Holds references to mesh and material data, visibility flag,
 * and cached world-space AABB (filled by computeBounds).
 */
struct Mesh {
    MeshHandle mesh;              ///< Handle to mesh geometry
    MaterialHandle material;      ///< Handle to material
    bool visible = true;          ///< Is mesh visible?

    glm::vec3 boundsMin{0.0f};    ///< Cached world-space AABB minimum
    glm::vec3 boundsMax{0.0f};    ///< Cached world-space AABB maximum
};

} // namespace Engine

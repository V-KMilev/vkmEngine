#pragma once

#include "mesh_asset.h"
#include "material_asset.h"

namespace Engine {

/**
 * @brief Component representing a renderable mesh (geometry + material) in the world.
 *
 * Simple data-only component. Holds references to mesh and material data, as well as visibility flag.
 */
struct Mesh {
    MeshHandle mesh;          ///< Handle to mesh geometry
    MaterialHandle material;  ///< Handle to material
    bool visible = true;      ///< Is mesh visible?
};

} // namespace Engine

#pragma once

#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"

namespace Engine {

/**
 * @brief Component representing a renderable mesh (geometry + material) in the world.
 */
struct Mesh {
    MeshHandle     mesh;
    MaterialHandle material;
    bool           visible     = true;
    bool           castShadows = true;  ///< Should this mesh contribute to shadow maps?
};

} // namespace Engine

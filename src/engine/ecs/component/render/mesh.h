#pragma once

#include "core/reflect.h"

#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief Component representing a renderable mesh (geometry + material) in the world.
 */
struct Mesh {
    MeshHandle     mesh;
    MaterialHandle material;
    bool           visible     = true;
    bool           castShadows = true;  ///< Should this mesh contribute to shadow maps?
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Mesh)
    VKM_F(mesh),
    VKM_F(material),
    VKM_F(visible),
    VKM_F(castShadows)
VKM_REFLECT_END()

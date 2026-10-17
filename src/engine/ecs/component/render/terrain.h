#pragma once

#include "core/reflect.h"
#include "resource/asset/terrain_asset.h"

namespace Vkm::Engine {

/**
 * @brief A terrain in the world: its asset, centred on the entity.
 *
 * Drawn, collided with and scattered with plants as its asset says (TerrainSystem); the entity's
 * position and rotation place it, as they place its collider, while a scale on it is not read - a
 * terrain is the size its asset gives.
 */
struct Terrain {
    TerrainHandle terrain;
    /// Whether its ground casts shadows, and its plants where their layer does.
    bool          castShadows = true;
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Terrain)
    VKM_F(terrain)
    VKM_F(castShadows)
VKM_REFLECT_END()

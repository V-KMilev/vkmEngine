#pragma once

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"
#include "ecs/entity.h"
#include "resource/asset/terrain_asset.h"

namespace Vkm::Engine {

class Scene;
struct Transform;

/**
 * @brief A terrain in the world: its asset, centered on the entity.
 *
 * Drawn, collided with and scattered with plants as its asset says (TerrainSystem); the entity's
 * position and rotation place it, as they place its collider, while a scale on it is not read - a
 * terrain is the size its asset gives.
 */
struct Terrain {
    TerrainHandle terrain;
    bool          castShadows = true;  ///< Whether its ground casts shadows, and its plants where their layer
                                       ///< does.
};

/**
 * @brief Where a terrain lies: its entity's world position and turn, unscaled, as the
 *        solver places its collider.
 *
 * @param scene  Holds the entity.
 * @param entity The terrain's entity.
 * @param local  Its Transform.
 * @return The terrain's model matrix.
 */
glm::mat4 terrainModel(const Scene& scene, EntityId entity, const Transform& local);

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Terrain)
    VKM_F(terrain)
    VKM_F(castShadows)
VKM_REFLECT_END()

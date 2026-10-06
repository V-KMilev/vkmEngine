#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "ecs/component/physics/collider.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Give @p collider a triangle-mesh part made from @p mesh.
 *
 * The shape of last resort and the only non-convex one, costing a tree walk and a routine per
 * triangle; it buys geometry with holes and overhangs. For static geometry: a soup has no inside to push
 * a deep overlap out of, and two mesh parts never collide. A dynamic body with one still meets boxes and
 * capsules, with its bound's inertia, but falls through any mesh floor.
 *
 * One per collider; a second is refused. Boxes and capsules may sit beside it. The part stores the
 * mesh's name; syncMeshCollider rebuilds the triangles whenever it changes.
 *
 * @param collider  Collider to add the part to; existing parts are kept.
 * @param mesh      Source geometry, in its own local space.
 * @param resources Where @p mesh resolves.
 * @param scale     Applied to every vertex, since the solver ignores Transform scale.
 * @return Triangles in the part; zero, and no part added, when the mesh does not resolve, has no whole
 *         triangle, or the collider already has a mesh part.
 */
uint32_t addMeshCollider(
    Collider& collider,
    MeshHandle mesh,
    const ResourceManager& resources,
    const glm::vec3& scale = glm::vec3(1.0f)
);

/**
 * @brief Bring @p collider's triangles and their tree up to date with the mesh its mesh part names.
 *
 * Rebuilt when the mesh, its version or the scale differ from Collider::meshBuiltFrom, emptied when
 * there is no mesh part or it does not resolve, so a late-loading or re-imported mesh is collided as it
 * now is. Costs a handle lookup when nothing changed.
 *
 * @param collider  Collider to bring up to date.
 * @param resources Where its mesh resolves.
 * @return True when the triangles were rebuilt or emptied.
 */
bool syncMeshCollider(Collider& collider, const ResourceManager& resources);

} // namespace Vkm::Engine

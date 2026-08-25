#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/component/physics/collider.h"

namespace Vkm::Engine {

struct MeshAsset;

/**
 * @brief Give @p collider a triangle-mesh part built from @p mesh.
 *
 * The shape of last resort, and the only one that is not convex: everything
 * else here can be approximated by boxes, capsules or a hull, and should be,
 * because a convex shape is one support query where a mesh is a tree walk and a
 * query per triangle it finds. What a mesh buys is terrain and architecture -
 * geometry with holes and overhangs, which no hull describes.
 *
 * Static only, and not because it is forbidden: a triangle soup has no volume,
 * so there is no inside for the solver to push out of and no inertia tensor to
 * spin. A dynamic body given one falls through the world.
 *
 * One per collider, and the second is refused: the hierarchy over the
 * triangles spans a single range, so a second part would be collided through
 * the first one's tree. Boxes, capsules and hulls may sit beside it freely.
 *
 * @param collider Collider to add the part to; existing parts are kept.
 * @param mesh Source geometry, read in its own local space.
 * @param scale Applied to every vertex, since the solver ignores Transform
 *        scale and a mesh authored at another size would otherwise not fit.
 * @return Triangles added; zero when the mesh has none, or when the collider
 *         already has a mesh part.
 */
uint32_t addMeshCollider(Collider& collider, const MeshAsset& mesh,
                         const glm::vec3& scale = glm::vec3(1.0f));

/**
 * @brief Rebuild the hierarchy over @p collider's mesh triangles.
 *
 * Called for a collider whose nodes are empty and whose parts include a mesh -
 * after a scene load, where the triangles were read back but the tree was not
 * written. Reorders the triangles, so any index into them is invalidated.
 *
 * @param collider Collider to rebuild.
 */
void rebuildMeshBvh(Collider& collider);

} // namespace Vkm::Engine

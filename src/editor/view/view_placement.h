#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "base/math/ray.h"
#include "ecs/entity.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;

/**
 * @brief Where the editor's view looks, kept each frame so a new entity can go there.
 */
struct ViewAim {
    Math::Ray ray;                               ///< From the viewpoint, straight ahead.
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  ///< The view's world rotation, which a new camera takes.
    float     focus = 10.0f;                     ///< Distance to what the view is on.
    bool      known = false;                     ///< False until a frame has stated it.
};

/// Where a new entity goes (the surface the view or a ray meets) and how far a dropped one falls to rest.
namespace ViewPlacement {

inline constexpr float MIN_DISTANCE = 2.0f;  ///< Nearest and furthest ahead of the view a new entity goes
                                             ///< when the view's center meets nothing.
inline constexpr float MAX_DISTANCE = 20.0f;

inline constexpr float REACH = 500.0f;  ///< Furthest along the view's center a surface is looked for.

inline constexpr float SINK_REACH = 0.25f;  ///< How far into a surface an entity may be sunk and still be
                                            ///< lifted out onto it by dropBelow.

/**
 * @brief Where @p ray first meets a mesh or a terrain, within REACH.
 *
 * Both faces of a mesh count, so from inside a room it is the wall. A posed mesh, its triangles stored
 * in bind pose, is met at its box. Linear in the scene: for a click, not a frame.
 *
 * @param scene     Its Mesh and Terrain entities are tested.
 * @param resources Resolves their assets.
 * @param ray       World space.
 * @param[out] t    Distance along @p ray; written only on true.
 * @param ignore    Entities never met, such as what is being moved.
 * @param[out] met  The entity met, when not null; written only on true.
 * @return True when it meets one.
 */
bool firstSurface(
    const Scene& scene,
    const ResourceManager& resources,
    const Math::Ray& ray,
    float& t,
    const std::vector<EntityId>& ignore = {},
    EntityId* met = nullptr
);

/**
 * @brief How far @p entity and everything under it must fall to rest on what is below.
 *
 * Straight down from their boxes' bottom center onto the first surface not their own, else the ground.
 *
 * @param scene     Holds the entity.
 * @param resources Resolves the assets.
 * @param entity    Has a Transform.
 * @param[out] drop World units down; negative rises out of what it is sunk in. Written only on true.
 * @return False when nothing is below it.
 */
bool dropBelow(const Scene& scene, const ResourceManager& resources, EntityId entity, float& drop);

/**
 * @brief Where along @p ray a new entity goes, by placeInView's rule.
 *
 * @param scene     Its surfaces are aimed at.
 * @param resources Resolves their assets.
 * @param ray       World space, such as the one under the cursor.
 * @param ahead     How far along it to go when it meets nothing, held to [MIN_DISTANCE, MAX_DISTANCE].
 * @param base      How far the entity reaches below its origin.
 * @return World position.
 */
glm::vec3 placeAlong(
    const Scene& scene,
    const ResourceManager& resources,
    const Math::Ray& ray,
    float ahead,
    float base
);

/**
 * @brief How far @p mesh reaches below its origin: the base a placed mesh is raised by, so it stands.
 *
 * @param resources Holds the mesh.
 * @param mesh      May be null or gone.
 * @return The mesh's own units, world units at unit scale; 0 for no mesh.
 */
float meshBase(const ResourceManager& resources, MeshHandle mesh);

/**
 * @brief Where a new entity goes; the origin until the view is stated.
 *
 * Where the view's center meets a surface; else the ground plane within MAX_DISTANCE; else ahead at the
 * focus distance, held to [MIN_DISTANCE, MAX_DISTANCE]. On a surface or the ground it is raised by
 * @p base, so what reaches below its origin stands on it.
 *
 * @param scene     Its surfaces are aimed at.
 * @param resources Resolves their assets.
 * @param aim       The editor's view.
 * @param base      How far the entity reaches below its origin.
 * @return World position.
 */
glm::vec3 placeInView(const Scene& scene, const ResourceManager& resources, const ViewAim& aim, float base);

} // namespace ViewPlacement

} // namespace Vkm::Engine

#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/math/bounds.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

struct FrameContext;
class CameraControllerSystem;
class ResourceManager;
class Scene;

/**
 * @brief Moving the view onto what it shows - a selection, the scene, an entity's own view -
 *        and the boxes and distances that takes.
 *
 * They move only the view, so take no undo step. A stood-down controller refuses
 * the move.
 */
namespace ViewFraming {

/**
 * @brief Frame @p selection with everything under it, whole in the view.
 *
 * Each entity counts by its drawn box where the frame has one (posed, so a rig frames
 * where its bones put it), else its mesh's or terrain's box, else its position.
 * No-op when nothing of it has a place in the world.
 *
 * @param ctx Supplies the scene, the frame's boxes and the mesh bounds.
 * @param selection Entities to frame; the dead are skipped.
 * @param camera View moved.
 */
void frameSelected(
    const FrameContext& ctx,
    const std::vector<EntityId>& selection,
    CameraControllerSystem& camera
);

/**
 * @brief Stand the view where @p entity stands, looking along its forward.
 *
 * The view has no roll, so an entity's is dropped. Refused by a stood-down controller.
 *
 * @param scene  Holds the entity, which has a Transform.
 * @param entity Typically a camera, to look through as it will.
 * @param camera View moved.
 */
void viewFromEntity(const Scene& scene, EntityId entity, CameraControllerSystem& camera);

/**
 * @brief Where an entity lies by its own parts: its terrain's box, its mesh's, or its position.
 *
 * Unposed: a skinned mesh counts at its bind-pose box.
 *
 * @param scene     Holds the entity, which has a Transform.
 * @param resources Resolves its assets.
 * @param id        The entity.
 * @return World-space box; a point for an entity with neither.
 */
Math::AABB worldBox(const Scene& scene, const ResourceManager& resources, EntityId id);

/**
 * @brief How far back a view must stand to see a sphere whole.
 *
 * @param radius The sphere's, floored so a point is framed from a sensible distance.
 * @param camera Its field of view decides.
 * @return World units from the sphere's centre.
 */
float fitDistance(float radius, const CameraControllerSystem& camera);

/**
 * @brief Frame the union of this frame's visible boxes.
 *
 * No-op when nothing is visible.
 *
 * @param ctx Its visibility result supplies the boxes.
 * @param camera View moved.
 */
void frameAll(const FrameContext& ctx, CameraControllerSystem& camera);

/**
 * @brief How far from @p target a viewpoint must stand to be outside everything drawn.
 *
 * Every render object counts, culled or not, since an orthographic view culls what is
 * behind its near plane.
 *
 * @param ctx    Supplies the frame's render objects.
 * @param target The point the view will look at.
 * @return World units: the furthest bounds corner from @p target, or 0 with nothing drawn.
 */
float clearance(const FrameContext& ctx, const glm::vec3& target);

} // namespace ViewFraming

} // namespace Vkm::Engine

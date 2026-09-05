#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;
struct Transform;

/**
 * @brief Where a body sits in the world, and the frame that maps back to local.
 *
 * Physics runs in world space, but a body's Transform is its parent's frame, so
 * every entry point needs the same conversion. `parented` false means the local
 * Transform was already the world pose - the common case, and the one that costs
 * nothing. The two parent fields are only meaningful when it is true, and exist
 * for writeback, which has to put a solved world pose back where it came from.
 * The parent's matrix rather than its inverse, because only the bodies that are
 * actually written back need the inverse and a sleeping one is not among them.
 */
struct BodyPose {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};        ///< World position
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};  ///< World orientation
    bool      parented = false;                     ///< Came from a parent
    glm::mat4 parentWorld = glm::mat4(1.0f);        ///< Parent model matrix
    glm::quat parentRot = {1.0f, 0.0f, 0.0f, 0.0f}; ///< Parent world rotation
};

/**
 * @brief Resolve an entity's world pose for physics.
 *
 * A hierarchy root is its own world pose. A parented body's is resolved by
 * walking its chain of Transforms - not by reading WorldTransform, which the
 * Transform stage writes after physics runs, so it is a frame stale where it
 * exists and absent entirely on a body parented this tick - and the parent
 * frame is recorded so a solved pose can be mapped back.
 *
 * @param scene Scene the entity belongs to.
 * @param id    Entity to resolve.
 * @param local The entity's Transform, already fetched by the caller.
 * @return The world pose, and the parent frame when there is one.
 */
BodyPose worldPoseOf(Scene& scene, EntityId id, const Transform& local);

} // namespace Vkm::Engine

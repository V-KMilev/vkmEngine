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
 * Physics runs in world space and a Transform in its parent's frame. The parent fields are meaningful
 * only when `parented`, to put a solved pose back; the matrix, not its inverse, because only a body
 * actually written back needs the inverse.
 */
struct BodyPose {
    glm::vec3 position    = {0.0f, 0.0f, 0.0f};        ///< World position
    glm::quat rotation    = {1.0f, 0.0f, 0.0f, 0.0f};  ///< World orientation
    bool      parented    = false;                     ///< Came from a parent
    glm::mat4 parentWorld = glm::mat4(1.0f);           ///< Parent model matrix
    glm::quat parentRot   = {1.0f, 0.0f, 0.0f, 0.0f};  ///< Parent world rotation
};

/**
 * @brief Resolve an entity's world pose for physics.
 *
 * A parented body's chain of Transforms is walked: WorldTransform is written after physics, so it is a
 * frame stale, or absent on a body parented this tick.
 *
 * @param scene Scene the entity belongs to.
 * @param id    Entity to resolve.
 * @param local The entity's Transform, already fetched.
 * @return The world pose, and the parent frame when there is one.
 */
BodyPose worldPoseOf(Scene& scene, EntityId id, const Transform& local);

} // namespace Vkm::Engine

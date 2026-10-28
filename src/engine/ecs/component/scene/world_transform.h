#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;
struct Transform;

/**
 * @brief Cached world-space model matrix for entities in a hierarchy.
 *
 * Written by HierarchySystem. Without one, the Transform is the world pose; see
 * resolvedWorldMatrix.
 */
struct WorldTransform {
    glm::mat4 model = glm::mat4(1.0f);
};

/**
 * @brief World matrix of a possibly-parented entity, as the last resolve left it.
 *
 * The engine's world-pose rule: the WorldTransform when present, else the local
 * Transform's model matrix. Unlike HierarchyOperations::computeWorldMatrix, which
 * walks ancestors now, this reads the Transform stage's result, so a later write
 * shows next frame.
 *
 * @param scene  Scene holding the entity's WorldTransform.
 * @param entity Entity asked about.
 * @param local  Its local Transform, the answer when it has no WorldTransform.
 * @return Its world matrix.
 */
glm::mat4 resolvedWorldMatrix(const Scene& scene, EntityId entity, const Transform& local);

/**
 * @brief World position of a possibly-parented entity, as the last resolve left it.
 *
 * Same rule as resolvedWorldMatrix; no matrix is built without a WorldTransform.
 *
 * @param scene  Scene holding the entity's WorldTransform.
 * @param entity Entity asked about.
 * @param local  Its local Transform, the answer when it has no WorldTransform.
 * @return Its world position.
 */
glm::vec3 resolvedWorldPosition(const Scene& scene, EntityId entity, const Transform& local);

/**
 * @brief World rotation of a possibly-parented entity, as the last resolve left it.
 *
 * Same rule as resolvedWorldMatrix, via Math::worldRotationOf; without a
 * WorldTransform it is the authored quaternion exactly.
 *
 * @param scene  Scene holding the entity's WorldTransform.
 * @param entity Entity asked about.
 * @param local  Its local Transform, the answer when it has no WorldTransform.
 * @return Its world rotation.
 */
glm::quat resolvedWorldRotation(const Scene& scene, EntityId entity, const Transform& local);

/**
 * @brief World scale of a possibly-parented entity, as the last resolve left it.
 *
 * Same rule as resolvedWorldMatrix. From a matrix a mirrored (negative) scale
 * comes back positive; from the Transform, as written.
 *
 * @param scene  Scene holding the entity's WorldTransform.
 * @param entity Entity asked about.
 * @param local  Its local Transform, the answer when it has no WorldTransform.
 * @return Its world scale.
 */
glm::vec3 resolvedWorldScale(const Scene& scene, EntityId entity, const Transform& local);

} // namespace Vkm::Engine

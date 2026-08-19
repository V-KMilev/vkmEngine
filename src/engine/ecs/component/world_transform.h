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
 * Populated by HierarchySystem each frame for every entity with a Hierarchy
 * component. Consumers (visibility, rendering) read this when present; entities
 * without WorldTransform are at the world root and use Transform directly.
 */
struct WorldTransform {
    glm::mat4 model = glm::mat4(1.0f);
};

/**
 * @brief World matrix of a possibly-parented entity, as the last resolve left it.
 *
 * The engine's world-pose rule in one place: an entity's world matrix is its
 * WorldTransform when it has one, and its local Transform's model matrix
 * otherwise (a root entity carries no WorldTransform). Every consumer
 * downstream of the Transform stage - rendering, visibility, particles, the
 * editor's gizmos - reads a pose this way. Distinct from
 * HierarchyOperations::computeWorldMatrix, which walks the ancestor chain and
 * answers for the scene as it stands right now; this reads what HierarchySystem
 * resolved during the Transform stage, so a Transform written after that stage
 * is not reflected until the next frame.
 */
glm::mat4 resolvedWorldMatrix(const Scene& scene, EntityId entity, const Transform& local);

/**
 * @brief World position of a possibly-parented entity, as the last resolve left it.
 *
 * Same rule as resolvedWorldMatrix, reading only the translation - so a root
 * entity costs no matrix construction.
 */
glm::vec3 resolvedWorldPosition(const Scene& scene, EntityId entity, const Transform& local);

/**
 * @brief World rotation of a possibly-parented entity, as the last resolve left it.
 *
 * Same rule as resolvedWorldMatrix, extracting the rotation via
 * Math::worldRotationOf - so a root entity yields its authored quaternion
 * exactly rather than one round-tripped through a matrix.
 */
glm::quat resolvedWorldRotation(const Scene& scene, EntityId entity, const Transform& local);

} // namespace Vkm::Engine

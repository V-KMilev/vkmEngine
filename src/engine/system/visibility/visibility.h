#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Engine {

/**
 * @brief A single visible entity, its world model matrix, and world-space AABB.
 *
 * Combining the fields in one struct improves cache locality when iterating
 * visibility results (vs parallel vectors that cross cache lines). The AABB
 * is the result of localToWorldAABB on the mesh's local bounds with this
 * entity's model matrix; downstream passes (AABB debug, picking) consume it
 * directly instead of re-transforming.
 */
struct VisibleEntity {
    EntityId id;
    glm::mat4 model;
    glm::vec3 worldMin{0.0f};
    glm::vec3 worldMax{0.0f};
};

/**
 * @brief Result of a visibility pass: camera data and visible entities.
 *
 * Populated by VisibilitySystem each frame and consumed by downstream
 * systems (RenderSystem, AnimationSystem).
 *
 * Camera data is computed once during culling and forwarded to avoid redundant lookups.
 */
struct Visibility {
    std::vector<VisibleEntity> entries;
    std::vector<VisibleEntity> shadowCasters;

    glm::mat4 view           = glm::mat4(1.0f);
    glm::mat4 projection     = glm::mat4(1.0f);
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    float     cameraExposure = 1.0f;

    /// Near and far clip planes of the active camera. Forwarded from the
    /// Camera component so downstream consumers (RenderView -> forward pass
    /// depth diagnostic) don't need to find the camera entity themselves.
    float     cameraZNear    = 0.1f;
    float     cameraZFar     = 1000.0f;

    bool hasCamera           = false;
};

} // namespace Engine

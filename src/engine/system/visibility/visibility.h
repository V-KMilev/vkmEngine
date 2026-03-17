#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Engine {

/**
 * @brief A single visible entity and its precomputed world model matrix.
 *
 * Combining ID and matrix in one struct improves cache locality when
 * iterating visibility results (vs parallel vectors that cross cache lines).
 */
struct VisibleEntity {
    EntityId id;
    glm::mat4 model;
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

    glm::mat4 view           = glm::mat4(1.0f);
    glm::mat4 projection     = glm::mat4(1.0f);
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    float     cameraExposure = 1.0f;
    bool hasCamera           = false;
};

} // namespace Engine

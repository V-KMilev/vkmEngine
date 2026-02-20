#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Engine {

/**
 * @brief Result of a visibility pass: camera data, entity IDs, and model matrices.
 *
 * entities[i] and modelMatrices[i] correspond. Populated by VisibilitySystem
 * each frame and consumed by downstream systems (RenderSystem, etc.).
 *
 * Camera data is computed once during culling and forwarded to avoid redundant lookups.
 */
struct Visibility {
    std::vector<EntityId> entities;
    std::vector<glm::mat4> modelMatrices;

    glm::mat4 view           = glm::mat4(1.0f);
    glm::mat4 projection     = glm::mat4(1.0f);
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    bool hasCamera           = false;
};

} // namespace Engine

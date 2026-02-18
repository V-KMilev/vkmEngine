#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Engine {

/**
 * @brief Result of a visibility pass: entity IDs and model matrices of visible meshes.
 *
 * entities[i] and modelMatrices[i] correspond. Populated by VisibilitySystem
 * each frame and consumed by downstream systems (RenderSystem, etc.).
 */
struct Visibility {
    std::vector<EntityId> entities;
    std::vector<glm::mat4> modelMatrices;
};

} // namespace Engine

#pragma once

#include <glm/glm.hpp>

namespace Engine {

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

} // namespace Engine

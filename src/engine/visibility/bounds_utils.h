#pragma once

#include <glm/glm.hpp>
#include <glm/ext/scalar_constants.hpp>

namespace Engine {

/**
 * @brief True if the AABB has non-degenerate extent (squared length of extent &gt; epsilon).
 * Degenerate or empty bounds return false. Uses squared extent to avoid sqrt.
 */
inline bool hasValidBounds(const glm::vec3& min, const glm::vec3& max) noexcept {
    const glm::vec3 extent = max - min;
    return glm::dot(extent, extent) > glm::epsilon<float>();
}

/**
 * @brief Transform an AABB from model space to world space.
 *
 * Transforms all eight corners by the model matrix and computes the encompassing AABB.
 *
 * @param modelMatrix Model-to-world matrix.
 * @param localMin Minimum corner in model space.
 * @param localMax Maximum corner in model space.
 * @param[out] worldMin Output minimum in world space.
 * @param[out] worldMax Output maximum in world space.
 */
inline void localToWorldAABB(
    const glm::mat4& modelMatrix,
    const glm::vec3& localMin,
    const glm::vec3& localMax,
    glm::vec3& worldMin,
    glm::vec3& worldMax
) {
    const glm::vec3 corners[8] = {
        {localMin.x, localMin.y, localMin.z}, {localMax.x, localMin.y, localMin.z},
        {localMin.x, localMax.y, localMin.z}, {localMax.x, localMax.y, localMin.z},
        {localMin.x, localMin.y, localMax.z}, {localMax.x, localMin.y, localMax.z},
        {localMin.x, localMax.y, localMax.z}, {localMax.x, localMax.y, localMax.z}
    };
    glm::vec3 t = glm::vec3(modelMatrix * glm::vec4(corners[0], 1.0f));
    worldMin = t;
    worldMax = t;
    for (int i = 1; i < 8; ++i) {
        t = glm::vec3(modelMatrix * glm::vec4(corners[i], 1.0f));
        worldMin = glm::min(worldMin, t);
        worldMax = glm::max(worldMax, t);
    }
}

} // namespace Engine
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
 * @brief Transform an AABB from model space to world space using Arvo's method.
 *
 * Uses algebraic AABB transformation instead of transforming 8 corners.
 * ~7x faster: 18 scalar muls vs 128 for corner-based approach.
 *
 * @param matrix Model-to-world matrix.
 * @param localMin Minimum corner in model space.
 * @param localMax Maximum corner in model space.
 * @param[out] worldMin Output minimum in world space.
 * @param[out] worldMax Output maximum in world space.
 */
inline void localToWorldAABB(
    const glm::mat4& matrix,
    const glm::vec3& localMin,
    const glm::vec3& localMax,
    glm::vec3& worldMin,
    glm::vec3& worldMax
) {
    // Start with translation component
    worldMin = glm::vec3(matrix[3]);
    worldMax = glm::vec3(matrix[3]);

    // For each matrix column (x, y, z basis vectors)
    for (int j = 0; j < 3; ++j) {
        const glm::vec3 col(matrix[j]);
        const glm::vec3 a = col * localMin[j];
        const glm::vec3 b = col * localMax[j];
        worldMin += glm::min(a, b);
        worldMax += glm::max(a, b);
    }
}

} // namespace Engine
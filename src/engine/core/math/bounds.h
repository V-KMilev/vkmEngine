#pragma once

#include <glm/glm.hpp>

namespace Engine::Math {

// Minimum squared extent for a valid AABB. glm::epsilon (~1.19e-7) is too small
// for world-space coordinates in range [-1000, 1000]. 1e-4 squared = 1e-8.
inline constexpr float BOUNDS_EPSILON_SQ = 1e-8f;

/**
 * @brief True if the AABB has non-degenerate extent (squared length of extent > epsilon).
 *
 * Degenerate or empty bounds return false. Uses squared extent to avoid sqrt.
 */
inline bool hasValidBounds(const glm::vec3& min, const glm::vec3& max) noexcept {
    const glm::vec3 extent = max - min;
    return glm::dot(extent, extent) > BOUNDS_EPSILON_SQ;
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

/**
 * @brief Ray-AABB intersection test using the slab method.
 *
 * @param origin    Ray origin in world space.
 * @param invDir    Component-wise inverse of ray direction (1/dir).
 * @param worldMin  AABB minimum corner in world space.
 * @param worldMax  AABB maximum corner in world space.
 * @param[out] tHit Distance along the ray to the first intersection ahead of the
 *                  origin - the entry point, or the exit point when the origin is
 *                  already inside the box.
 * @return True if the ray intersects the AABB (with tMax >= 0).
 */
inline bool rayIntersectsAABB(
    const glm::vec3& origin,
    const glm::vec3& invDir,
    const glm::vec3& worldMin,
    const glm::vec3& worldMax,
    float& tHit
) noexcept {
    const glm::vec3 t0 = (worldMin - origin) * invDir;
    const glm::vec3 t1 = (worldMax - origin) * invDir;

    const glm::vec3 tMinV = glm::min(t0, t1);
    const glm::vec3 tMaxV = glm::max(t0, t1);

    float tMin = glm::max(glm::max(tMinV.x, tMinV.y), tMinV.z);
    float tMax = glm::min(glm::min(tMaxV.x, tMaxV.y), tMaxV.z);

    // An origin inside the box puts tMin behind the ray, so the nearest hit in
    // front is the exit at tMax. Reporting the negative tMin instead lets any
    // box enclosing the camera - a room, a ground plane, a big trigger volume -
    // undercut every genuine hit in a `t < nearest` ranking.
    tHit = tMin > 0.0f ? tMin : tMax;
    return tMax >= tMin && tMax >= 0.0f;
}

} // namespace Engine::Math

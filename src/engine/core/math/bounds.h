#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine::Math {

// Minimum squared extent for a valid AABB. glm::epsilon (~1.19e-7) is too small
// for world-space coordinates in range [-1000, 1000]. 1e-4 squared = 1e-8.
inline constexpr float BOUNDS_EPSILON_SQ = 1e-8f;

/**
 * @brief An axis-aligned box, which is the most-passed shape in the engine.
 *
 * It travelled as a loose pair of `glm::vec3`s for a long time, which worked
 * because everyone spelled the pairing the same way - and the pairing was the
 * whole rule. A function taking four of them took four parameters that could be
 * given in the wrong order and still compile, and one that produced a box
 * returned it through two out-parameters because it had nothing to return.
 *
 * Naming it costs nothing at run time - two vec3s, same layout as the pair - and
 * turns "min then max, and do not mix two boxes up" from a convention into a
 * type.
 */
struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    /**
     * @brief True when the box has non-degenerate extent.
     *
     * Squared, to avoid a sqrt on a test that runs per drawable per frame.
     */
    bool valid() const noexcept {
        const glm::vec3 extent = max - min;
        return glm::dot(extent, extent) > BOUNDS_EPSILON_SQ;
    }

    /// The midpoint; where a marker, a label or a pivot goes.
    glm::vec3 center() const noexcept { return (min + max) * 0.5f; }

    /// Half the diagonal, which is what a screen-size test scales.
    glm::vec3 halfExtent() const noexcept { return (max - min) * 0.5f; }
};



/**
 * @brief Transform an AABB from model space to world space using Arvo's method.
 *
 * Uses algebraic AABB transformation instead of transforming 8 corners.
 * ~7x faster: 18 scalar muls vs 128 for corner-based approach.
 *
 * @param matrix Model-to-world matrix.
 * @param local  The box in model space.
 * @return The box in world space.
 */
inline AABB transform(const glm::mat4& matrix, const AABB& local) {
    AABB world;
    world.min = glm::vec3(matrix[3]);
    world.max = glm::vec3(matrix[3]);

    for (int j = 0; j < 3; ++j) {
        const glm::vec3 col(matrix[j]);
        const glm::vec3 a = col * local.min[j];
        const glm::vec3 b = col * local.max[j];
        world.min += glm::min(a, b);
        world.max += glm::max(a, b);
    }
    return world;
}

/**
 * @brief Ray-AABB intersection test using the slab method.
 *
 * @param origin    Ray origin in world space.
 * @param invDir    Component-wise inverse of ray direction (1/dir).
 * @param box       The box in world space.
 * @param[out] tHit Distance along the ray to the first intersection ahead of the
 *                  origin - the entry point, or the exit point when the origin is
 *                  already inside the box.
 * @return True if the ray intersects the AABB (with tMax >= 0).
 */
inline bool rayIntersectsAABB(
    const glm::vec3& origin,
    const glm::vec3& invDir,
    const AABB& box,
    float& tHit
) noexcept {
    const glm::vec3 t0 = (box.min - origin) * invDir;
    const glm::vec3 t1 = (box.max - origin) * invDir;

    const glm::vec3 tMinV = glm::min(t0, t1);
    const glm::vec3 tMaxV = glm::max(t0, t1);

    float tMin = glm::max(glm::max(tMinV.x, tMinV.y), tMinV.z);
    float tMax = glm::min(glm::min(tMaxV.x, tMaxV.y), tMaxV.z);

    // An origin inside the box puts tMin behind the ray, so the nearest hit in
    // front is the exit at tMax. A negative tMin would let any box enclosing
    // the camera undercut every genuine hit in a `t < nearest` ranking.
    tHit = tMin > 0.0f ? tMin : tMax;
    return tMax >= tMin && tMax >= 0.0f;
}

} // namespace Vkm::Engine::Math

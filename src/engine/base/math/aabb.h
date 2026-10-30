#pragma once

#include <limits>

#include <glm/glm.hpp>

namespace Vkm::Engine::Math {

// Minimum squared extent for a valid AABB (1e-4 squared). glm::epsilon is too
// small for world coordinates in [-1000, 1000].
inline constexpr float BOUNDS_EPSILON_SQ = 1e-8f;

/**
 * @brief An axis-aligned box.
 */
struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    /**
     * @brief True when the box has non-degenerate extent.
     *
     * @return Whether the squared diagonal exceeds BOUNDS_EPSILON_SQ.
     */
    bool valid() const noexcept {
        const glm::vec3 extent = max - min;
        return glm::dot(extent, extent) > BOUNDS_EPSILON_SQ;
    }

    glm::vec3 center() const noexcept { return (min + max) * 0.5f; }

    glm::vec3 halfExtent() const noexcept { return (max - min) * 0.5f; }

    /// A box holding nothing, which the first expand() makes the thing it holds.
    static AABB empty() noexcept {
        const float most = std::numeric_limits<float>::max();
        return {glm::vec3(most), glm::vec3(-most)};
    }

    /// Grow to hold @p point.
    void expand(const glm::vec3& point) noexcept {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    /// Grow to hold @p other.
    void expand(const AABB& other) noexcept {
        min = glm::min(min, other.min);
        max = glm::max(max, other.max);
    }
};

/**
 * @brief Transform an AABB by a linear map and then an offset - a rotation and a position, as a
 *        body's pose is held - by Arvo's method.
 *
 * @param linear The map, its columns the box's axes in the new space.
 * @param offset Where the box's origin lands.
 * @param local  Box in its own space.
 * @return Box in the new space.
 */
inline AABB transform(const glm::mat3& linear, const glm::vec3& offset, const AABB& local) {
    AABB moved{offset, offset};
    for (int j = 0; j < 3; ++j) {
        const glm::vec3 a = linear[j] * local.min[j];
        const glm::vec3 b = linear[j] * local.max[j];
        moved.min += glm::min(a, b);
        moved.max += glm::max(a, b);
    }
    return moved;
}

/**
 * @brief Transform an AABB from model to world space by Arvo's method, not 8 corners.
 *
 * @param matrix Model-to-world matrix.
 * @param local  Box in model space.
 * @return Box in world space.
 */
inline AABB transform(const glm::mat4& matrix, const AABB& local) {
    return transform(glm::mat3(matrix), glm::vec3(matrix[3]), local);
}

} // namespace Vkm::Engine::Math

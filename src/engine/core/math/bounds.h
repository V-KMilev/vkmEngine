#pragma once

#include <limits>
#include <optional>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

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
 * @brief Transform an AABB from model to world space by Arvo's method, not 8 corners.
 *
 * @param matrix Model-to-world matrix.
 * @param local  Box in model space.
 * @return Box in world space.
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
 * @brief A half-line in world space.
 */
struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};   ///< Unit length.
};

/**
 * @brief The world-space ray under a point of the image, for either projection.
 *
 * The origin is the near-plane point under the pixel, not the eye, which also
 * holds for orthographic rays that never pass through the camera.
 *
 * @param invViewProj Inverse of projection * view.
 * @param ndc Normalized device coordinates: -1..1 across, +y up.
 * @return The ray from the near plane toward the far one.
 */
inline Ray rayThroughNdc(const glm::mat4& invViewProj, const glm::vec2& ndc) {
    glm::vec4 nearPoint = invViewProj * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4 farPoint  = invViewProj * glm::vec4(ndc,  1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint  /= farPoint.w;
    return Ray{glm::vec3(nearPoint), glm::normalize(glm::vec3(farPoint - nearPoint))};
}

/**
 * @brief Ray-AABB intersection test using the slab method.
 *
 * @param origin    Ray origin in world space.
 * @param invDir    Component-wise inverse of ray direction (1/dir).
 * @param box       Box in world space.
 * @param[out] tHit Distance to the first intersection ahead of the origin - the
 *                  exit point when the origin is inside.
 * @return True if the ray hits the box ahead of the origin.
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

    // Inside the box tMin is behind, so the hit is the exit; a negative tMin would
    // let a box enclosing the camera undercut every genuine hit in a ranking.
    tHit = tMin > 0.0f ? tMin : tMax;
    return tMax >= tMin && tMax >= 0.0f;
}

/**
 * @brief Where @p ray meets the level plane at height @p y, ahead of its origin: what a click
 *        on a floor, a table or the sea lands on.
 *
 * @param ray Ray in world space.
 * @param y   The plane's height.
 * @return The point, or none for a ray level with the plane or pointing away from it.
 */
inline std::optional<glm::vec3> rayHitsLevel(const Ray& ray, float y) noexcept {
    if (glm::abs(ray.direction.y) < glm::epsilon<float>()) return std::nullopt;
    const float t = (y - ray.origin.y) / ray.direction.y;
    if (t <= 0.0f) return std::nullopt;
    return ray.origin + ray.direction * t;
}

} // namespace Vkm::Engine::Math

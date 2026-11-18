#pragma once

#include <optional>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "base/math/aabb.h"

namespace Vkm::Engine::Math {

/**
 * @brief A half-line in world space.
 */
struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};  ///< Unit length.
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

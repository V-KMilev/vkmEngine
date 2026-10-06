#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief Where a line crosses a triangle, by Moller-Trumbore.
 *
 * Behind the origin as well as ahead; which crossings count is the caller's question. A line parallel
 * to the plane, to float precision, crosses nowhere.
 *
 * @param origin A point on the line.
 * @param dir The line's direction; a unit one makes @p t a distance.
 * @param v0 First corner.
 * @param v1 Second corner.
 * @param v2 Third corner.
 * @param[out] t The crossing, in multiples of @p dir; written only on true.
 * @return True when the line passes through the triangle.
 */
bool rayTriangle(
    const glm::vec3& origin,
    const glm::vec3& dir,
    const glm::vec3& v0,
    const glm::vec3& v1,
    const glm::vec3& v2,
    float& t
);

} // namespace Vkm::Engine

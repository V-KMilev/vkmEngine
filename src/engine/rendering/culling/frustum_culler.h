#pragma once

#include <glm/glm.hpp>
#include <array>

namespace Engine {

/**
 * @brief Represents a view frustum for culling operations.
 *
 * Stores the 6 planes of a frustum in world space, computed from
 * view-projection matrix. Planes are stored in the format:
 * ax + by + cz + d = 0, where (a, b, c) is the normal.
 */
struct Frustum {
    glm::vec4 planes[6];  ///< left, right, bottom, top, near, far
};

/**
 * @brief Utility functions for frustum culling operations.
 *
 * Provides functions to extract frustum planes from view-projection matrices
 * and test AABB (Axis-Aligned Bounding Box) intersections with frustums.
 */
namespace FrustumCuller {
/**
 * @brief Extract frustum planes from a view-projection matrix.
 *
 * Extracts the 6 frustum planes (left, right, bottom, top, near, far)
 * from the combined view-projection matrix. Planes are normalized.
 *
 * @param viewProjection The combined view-projection matrix.
 * @return Frustum containing the 6 planes.
 */
Frustum extractFrustum(const glm::mat4& viewProjection);

/**
 * @brief Test if an AABB (in world space) intersects with a frustum.
 *
 * Uses the "half-space test" algorithm: for each frustum plane,
 * check if the AABB is entirely on the negative side. If so, the
 * AABB is outside the frustum. Otherwise, it's inside or intersecting.
 *
 * @param frustum The frustum to test against.
 * @param aabbMin Minimum corner of the AABB in world space.
 * @param aabbMax Maximum corner of the AABB in world space.
 * @return true if the AABB is inside or intersecting the frustum, false if completely outside.
 */
bool isAABBVisible(
    const Frustum& frustum,
    const glm::vec3& aabbMin,
    const glm::vec3& aabbMax
);

/**
 * @brief Transform an AABB from model space to world space.
 *
 * Transforms the 8 corners of the AABB by the model matrix and
 * computes a new AABB that encompasses all transformed corners.
 *
 * @param modelMatrix The model transformation matrix.
 * @param localMin Minimum corner of the AABB in model space.
 * @param localMax Maximum corner of the AABB in model space.
 * @param[out] worldMin Output minimum corner in world space.
 * @param[out] worldMax Output maximum corner in world space.
 */
void transformAABB(
    const glm::mat4& modelMatrix,
    const glm::vec3& localMin,
    const glm::vec3& localMax,
    glm::vec3& worldMin,
    glm::vec3& worldMax
);

} // namespace FrustumCuller

} // namespace Engine


#include "frustum_culler.h"

#include <glm/gtc/matrix_access.hpp>

namespace Engine {

namespace FrustumCuller {

Frustum extractFrustum(const glm::mat4& viewProjection) {
    Frustum frustum;

    // Extract rows from the view-projection matrix
    // GLM matrices are column-major, but glm::row() correctly extracts rows
    const glm::vec4 row0 = glm::row(viewProjection, 0);
    const glm::vec4 row1 = glm::row(viewProjection, 1);
    const glm::vec4 row2 = glm::row(viewProjection, 2);
    const glm::vec4 row3 = glm::row(viewProjection, 3);

    // Left plane: row3 + row0
    frustum.planes[0] = row3 + row0;
    // Right plane: row3 - row0
    frustum.planes[1] = row3 - row0;
    // Bottom plane: row3 + row1
    frustum.planes[2] = row3 + row1;
    // Top plane: row3 - row1
    frustum.planes[3] = row3 - row1;
    // Near plane: row3 + row2
    frustum.planes[4] = row3 + row2;
    // Far plane: row3 - row2
    frustum.planes[5] = row3 - row2;

    // Normalize all planes
    for (auto& plane : frustum.planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 0.0f) {
            plane /= length;
        }
    }

    return frustum;
}

bool isAABBVisible(
    const Frustum& frustum,
    const glm::vec3& aabbMin,
    const glm::vec3& aabbMax
) {
    // For each frustum plane, test if the AABB is entirely on the negative side
    for (const auto& plane : frustum.planes) {
        const glm::vec3 normal = glm::vec3(plane);
        const float d = plane.w;

        // Find the "positive vertex" - the corner of the AABB that is furthest
        // in the positive direction of the plane normal
        glm::vec3 positiveVertex;
        positiveVertex.x = (normal.x >= 0.0f) ? aabbMax.x : aabbMin.x;
        positiveVertex.y = (normal.y >= 0.0f) ? aabbMax.y : aabbMin.y;
        positiveVertex.z = (normal.z >= 0.0f) ? aabbMax.z : aabbMin.z;

        // If even the positive vertex is on the negative side, the entire AABB is outside
        const float distance = glm::dot(normal, positiveVertex) + d;
        // AABB is completely outside this plane
        if (distance < 0.0f) {
            return false;
        }
    }

    // AABB is inside or intersecting the frustum
    return true;
}

void transformAABB(
    const glm::mat4& modelMatrix,
    const glm::vec3& localMin,
    const glm::vec3& localMax,
    glm::vec3& worldMin,
    glm::vec3& worldMax
) {
    // Transform all 8 corners of the AABB
    const glm::vec3 corners[8] = {
        glm::vec3(localMin.x, localMin.y, localMin.z),
        glm::vec3(localMax.x, localMin.y, localMin.z),
        glm::vec3(localMin.x, localMax.y, localMin.z),
        glm::vec3(localMax.x, localMax.y, localMin.z),
        glm::vec3(localMin.x, localMin.y, localMax.z),
        glm::vec3(localMax.x, localMin.y, localMax.z),
        glm::vec3(localMin.x, localMax.y, localMax.z),
        glm::vec3(localMax.x, localMax.y, localMax.z)
    };

    // Transform first corner to initialize min/max
    glm::vec3 transformed = glm::vec3(modelMatrix * glm::vec4(corners[0], 1.0f));
    worldMin = transformed;
    worldMax = transformed;

    // Transform remaining corners and expand AABB
    for (int i = 1; i < 8; ++i) {
        transformed = glm::vec3(modelMatrix * glm::vec4(corners[i], 1.0f));
        worldMin = glm::min(worldMin, transformed);
        worldMax = glm::max(worldMax, transformed);
    }
}

} // namespace FrustumCuller

} // namespace Engine


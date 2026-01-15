#pragma once

#include <vector>
#include <cstdint>

#include <glm/glm.hpp>

#include "mesh_asset.h"

namespace Engine {

/**
 * @brief LOD level definition.
 */
struct LODLevel {
    MeshHandle mesh;       ///< Mesh to use at this LOD level
    float maxDistance;     ///< Maximum distance for this LOD (0 = highest detail)
};

/**
 * @brief LOD configuration for a mesh.
 *
 * Contains multiple LOD levels sorted by distance.
 * Level 0 is highest detail (closest), higher levels are lower detail.
 */
struct LODConfig {
    std::vector<LODLevel> levels;

    /**
     * @brief Get the appropriate LOD level for a given distance.
     * @param distance Distance from camera.
     * @return Index of the LOD level to use.
     */
    size_t getLevelForDistance(float distance) const {
        for (size_t i = 0; i < levels.size(); ++i) {
            if (distance <= levels[i].maxDistance) {
                return i;
            }
        }
        return levels.empty() ? 0 : levels.size() - 1;
    }

    /**
     * @brief Get the mesh handle for a given distance.
     */
    MeshHandle getMeshForDistance(float distance) const {
        if (levels.empty()) return MeshHandle{};
        return levels[getLevelForDistance(distance)].mesh;
    }
};

/**
 * @brief LOD component for entities that support level of detail.
 */
struct LOD {
    LODConfig config;
    float bias = 0.0f;  ///< Distance bias (positive = use lower LOD sooner)
};

/**
 * @brief Utility functions for LOD calculations.
 */
namespace LODSystem {

/**
 * @brief Calculate the distance from camera to an object.
 * @param cameraPos Camera world position.
 * @param objectPos Object world position.
 * @return Distance in world units.
 */
inline float calculateDistance(const glm::vec3& cameraPos, const glm::vec3& objectPos) {
    return glm::length(objectPos - cameraPos);
}

/**
 * @brief Calculate screen-space size of an object (for screen-size based LOD).
 * @param distance Distance from camera.
 * @param objectRadius Object bounding sphere radius.
 * @param fovY Vertical field of view in radians.
 * @param screenHeight Screen height in pixels.
 * @return Approximate screen height in pixels.
 */
inline float calculateScreenSize(float distance, float objectRadius, float fovY, float screenHeight) {
    if (distance < 0.001f) return screenHeight;
    float projectedSize = (objectRadius * 2.0f) / (distance * std::tan(fovY * 0.5f));
    return projectedSize * screenHeight * 0.5f;
}

/**
 * @brief Generate LOD distances based on screen-size thresholds.
 * @param objectRadius Bounding sphere radius of the object.
 * @param fovY Vertical FOV in radians.
 * @param screenHeight Screen height in pixels.
 * @param thresholds Screen-size thresholds in pixels (e.g., {100, 50, 25}).
 * @return Distances corresponding to each threshold.
 */
inline std::vector<float> generateDistancesFromScreenSize(
    float objectRadius,
    float fovY,
    float screenHeight,
    const std::vector<float>& thresholds
) {
    std::vector<float> distances;
    distances.reserve(thresholds.size());

    for (float threshold : thresholds) {
        // Solve: threshold = (radius * 2) / (distance * tan(fov/2)) * screenHeight * 0.5
        // distance = (radius * screenHeight) / (threshold * tan(fov/2))
        float distance = (objectRadius * screenHeight) / (threshold * std::tan(fovY * 0.5f));
        distances.push_back(distance);
    }

    return distances;
}

} // namespace LODSystem

} // namespace Engine


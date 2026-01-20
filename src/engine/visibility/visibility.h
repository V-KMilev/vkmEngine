#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "entity.h"

namespace Engine {
    class Scene;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief Result of a visibility pass: entity IDs and model matrices of visible meshes.
 *
 * entities[i] and modelMatrices[i] correspond. Cached world bounds are updated on
 * Mesh during buildVisibility; modelMatrices are stored here for the renderer.
 */
struct Visibility {
    std::vector<EntityId> entities;          ///< Entity IDs that passed all culling tests.
    std::vector<glm::mat4> modelMatrices;    ///< Model matrices for each visible entity (same order as entities).
};

/**
 * @brief Build the visibility list for the current frame.
 *
 * Updates Mesh::boundsMin/boundsMax from MeshAsset + Transform, then runs
 * frustum, distance, and screen-size culling. Entity IDs and model matrices
 * of visible meshes are stored in the returned Visibility.
 *
 * @param scene Scene with Mesh, Transform, Camera. Mesh world bounds are written.
 * @param resources Resource manager for MeshAssets.
 * @param viewportWidth Viewport width in pixels.
 * @param viewportHeight Viewport height in pixels.
 * @return Visibility with entities and modelMatrices of visible meshes.
 */
Visibility buildVisibility(
    Scene& scene,
    const ResourceManager& resources,
    uint32_t viewportWidth,
    uint32_t viewportHeight
);

} // namespace Engine

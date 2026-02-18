#pragma once

#include <glm/glm.hpp>

#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Engine {

/**
 * @brief Represents a single vertex in a mesh.
 *
 * Contains all necessary per-vertex attributes required for rendering and shading,
 * including position, surface normal, UV coordinates, and a tangent vector for normal mapping.
 */
 struct Vertex {
    glm::vec3 position;    ///< 3D position of the vertex in model space (x, y, z).
    glm::vec3 normal;      ///< Surface normal at the vertex (used for lighting).
    glm::vec2 uv;          ///< 2D texture coordinates (u, v) for mapping textures.
    glm::vec4 tangent;     ///< Tangent vector (x, y, z, w), used for normal mapping; w is handedness.
};

struct MeshAsset : public Resource {
    std::vector<Vertex> vertices  = {};     ///< Vertex buffer (geometry)
    std::vector<uint32_t> indices = {};    ///< Index buffer (triangle indices)

    // Optional metadata
    glm::vec3 boundsMin{0};           ///< Minimum AABB point in local space
    glm::vec3 boundsMax{0};           ///< Maximum AABB point in local space

    /**
     * @brief Compute axis-aligned bounding box (AABB) from vertex positions.
     *
     * Calculates the minimum and maximum corners of an AABB that encompasses
     * all vertex positions in the mesh. This is used for frustum culling.
     *
     * @param mesh The mesh asset to compute bounds for.
     * @param[out] boundsMin Output minimum corner of the AABB.
     * @param[out] boundsMax Output maximum corner of the AABB.
     */
    static void computeBounds(const MeshAsset& mesh, glm::vec3& boundsMin, glm::vec3& boundsMax);

    /**
     * @brief Compute and set the AABB bounds for this mesh asset.
     *
     * Calculates bounds from vertex positions and sets them in the mesh asset.
     * This should be called after creating or loading a mesh.
     */
    void computeAndSetBounds();
};

using MeshHandle = Handle<MeshAsset>;

} // namespace Engine

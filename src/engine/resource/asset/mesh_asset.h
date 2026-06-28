#pragma once

#include <cstdint>
#include <vector>

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

/**
 * @brief CPU-side geometry: indexed triangle mesh plus a cached local-space AABB.
 *
 * The backend uploads `vertices`/`indices` to GPU buffers on sync; the bounds
 * feed frustum/size culling and are (re)computed via computeAndSetBounds().
 */
struct MeshAsset : public Resource {
    std::vector<Vertex>   vertices = {};  ///< Vertex buffer (geometry)
    std::vector<uint32_t> indices  = {};  ///< Index buffer (triangle indices)

    // Optional metadata
    glm::vec3 boundsMin{0};           ///< Minimum AABB point in local space
    glm::vec3 boundsMax{0};           ///< Maximum AABB point in local space

    /**
     * @brief True while an async decode is in flight.
     *
     * Flipped false once AsyncLoaderSystem finalises the upload.
     * VisibilitySystem already skips meshes with zero-extent bounds, so
     * a loading mesh stays invisible until its data arrives - no
     * fallback needed.
     */
    bool loading = false;

    /**
     * @brief Compute the local-space AABB from vertex positions and store it in
     * boundsMin / boundsMax (used for frustum/size culling).
     *
     * Call after creating or loading the mesh; an empty mesh yields a
     * zero-extent box at the origin.
     */
    void computeAndSetBounds();
};

using MeshHandle = Handle<MeshAsset>;

} // namespace Engine

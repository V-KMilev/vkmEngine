#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

#include "core/math/bounds.h"
#include "resource/asset/mesh_asset.h"
#include "system/physics/collision/triangle.h"

namespace Vkm::Engine {

/**
 * @brief Where a ray first meets a mesh's triangles, as placed in the world.
 *
 * The ray goes into mesh space and t stays in world-ray terms (an affine map keeps it).
 * Both faces count: a click inside a room hits its wall. Linear in triangles - for a
 * click, not a frame. Free of ImGui so it is tested headlessly.
 *
 * @param ray A world-space ray.
 * @param model The mesh's world matrix; invertible.
 * @param mesh Geometry in its own space.
 * @param[out] t Distance along @p ray to the nearest crossing ahead; written only on true.
 * @return True when the ray crosses a triangle ahead of its origin.
 */
inline bool rayHitsMesh(const Math::Ray& ray, const glm::mat4& model, const MeshAsset& mesh, float& t) {
    const glm::mat4 toLocal = glm::inverse(model);
    const glm::vec3 origin  = glm::vec3(toLocal * glm::vec4(ray.origin, 1.0f));
    const glm::vec3 dir     = glm::vec3(toLocal * glm::vec4(ray.direction, 0.0f));

    const size_t vertexCount = mesh.vertices.size();
    const size_t indexCount  = mesh.indices.size() - mesh.indices.size() % 3;
    float nearest = std::numeric_limits<float>::max();
    for (size_t i = 0; i < indexCount; i += 3) {
        const uint32_t a = mesh.indices[i];
        const uint32_t b = mesh.indices[i + 1];
        const uint32_t c = mesh.indices[i + 2];
        if (a >= vertexCount || b >= vertexCount || c >= vertexCount) continue;

        float hit;
        const bool crossed = rayTriangle(
            origin,
            dir,
            mesh.vertices[a].position,
            mesh.vertices[b].position,
            mesh.vertices[c].position,
            hit
        );
        if (crossed && hit >= 0.0f && hit < nearest) {
            nearest = hit;
        }
    }
    if (nearest == std::numeric_limits<float>::max()) return false;
    t = nearest;
    return true;
}

} // namespace Vkm::Engine

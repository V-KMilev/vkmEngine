#pragma once

#include <glm/glm.hpp>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"

namespace Vkm::Engine {

/**
 * @brief One visible object to draw.
 *
 * Pure handles plus a world matrix. The backend resolves the handles against
 * its own GPU mirror and decides how to sort / batch them - that is an
 * API-specific concern, so it lives below the interface, not here.
 */
struct DrawableData {
    MeshHandle     mesh;         ///< The mesh to render.
    MaterialHandle material;     ///< The material to render.
    glm::mat4      model;        ///< The model matrix to render the mesh with.
    glm::mat3      normalMatrix; ///< transpose(inverse(mat3(model))): correct normals under non-uniform scale. Precomputed so the vertex shader skips a per-vertex matrix inverse.

    /**
     * @brief World-space AABB, as the visibility cull already computed it.
     *
     * Carried because the backend culls too: the GPU occlusion pass tests this
     * box against the frame's depth. Recomputing it below the interface would
     * mean transforming the same bounds twice per frame for the same answer.
     * The shadow-caster list carries its bounds for the same reason.
     */
    glm::vec3 worldMin;
    glm::vec3 worldMax;

    bool castShadows;            ///< Whether the drawable casts shadows.
};

} // namespace Vkm::Engine

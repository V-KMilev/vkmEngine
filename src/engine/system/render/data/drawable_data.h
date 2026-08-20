#pragma once

#include <cstdint>

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
    MeshHandle     mesh;
    MaterialHandle material;
    glm::mat4      model;

    /// transpose(inverse(mat3(model))): correct normals under non-uniform scale.
    /// Precomputed so the vertex shader skips a per-vertex matrix inverse.
    glm::mat3      normalMatrix;

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

    /**
     * @brief This item's bone palette inside RenderView::skinMatrices.
     *
     * A count of 0 means nothing poses this entity, and the backend draws it
     * with the static program. That is also the honest answer for a skinned
     * mesh dragged out of its rig's subtree: the vertices it stored are the
     * bind pose, so drawing them untouched is exactly what "no rig" looks like.
     */
    uint32_t skinFirst = 0;
    uint32_t skinCount = 0;

    bool castShadows;
};

} // namespace Vkm::Engine

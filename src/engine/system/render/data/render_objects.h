#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/math/bounds.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief What one object is drawn with: its geometry, its material and its pose.
 *
 * Pure handles; the backend resolves, sorts and batches them.
 */
struct ObjectDraw {
    /**
     * @brief The Mesh component's handle, or the level an LOD component selected.
     */
    MeshHandle     mesh;
    MaterialHandle material;

    /**
     * @brief Bones in this object's slice of RenderView::skinMatrices.
     *
     * Zero means nothing poses it and it draws unskinned, i.e. in bind pose.
     */
    uint32_t skinCount = 0;
};

/**
 * @brief Every object the frame can draw, written once by the cull, and the lists of what each reader draws.
 *
 * An object index is the Mesh's place in its storage this frame. The per-object
 * arrays are parallel and sized to every Mesh; an index no list names holds
 * stale data. Owned by Visibility, borrowed by RenderView.
 */
struct RenderObjects {
    std::vector<glm::mat4>  models;     ///< World matrices.
    std::vector<Math::AABB> bounds;     ///< World boxes, posed for a posed mesh.
    std::vector<ObjectDraw> draws;

    /**
     * @brief Per object: where its bones start in RenderView::skinMatrices.
     *
     * Zero when ObjectDraw::skinCount is.
     */
    std::vector<uint32_t>   skinFirst;

    /**
     * @brief The objects the camera sees, in object order, unsorted.
     */
    std::vector<uint32_t> visible;

    /**
     * @brief Every drawn object, camera or not, shadow casters first.
     *
     * For drawing that is not the camera's: shadows and captures.
     */
    std::vector<uint32_t> scene;

    /**
     * @brief How many of `scene` cast shadows, which are its prefix.
     */
    uint32_t casterCount = 0;
};

} // namespace Vkm::Engine

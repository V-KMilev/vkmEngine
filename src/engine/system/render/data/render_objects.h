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

    /// Its surface is the scene's ocean: a `waves` material, in a scene with an ocean. A skinned
    /// one is shaded as the water and not moved.
    bool waves = false;

    /// A Viewmodel mesh seen through the eye: drawn through CameraData::viewmodelViewProjection.
    bool viewmodel = false;

    bool operator==(const ObjectDraw& other) const {
        return mesh == other.mesh && material == other.material && skinCount == other.skinCount
            && waves == other.waves && viewmodel == other.viewmodel;
    }

    bool operator!=(const ObjectDraw& other) const { return !(*this == other); }
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

    /**
     * @brief The objects whose matrix, box, draw, first bone or place in the scene-wide list or its
     *        caster prefix differ from the last frame's, in object order.
     *
     * An index that now holds another Mesh - added, or moved there by a removal - is among them. A
     * reader keeping a copy of the per-object arrays - the GPU's - resizes it to theirs, keeping
     * what it held, and refreshes these alone. Empty while allChanged.
     */
    std::vector<uint32_t> changed;

    /**
     * @brief Every object may differ from the last frame's: the world or the asset graph was
     *        replaced, the frame before published none, or a fixed object moved in it.
     *
     * A copy is rebuilt whole.
     */
    bool allChanged = true;

    /**
     * @brief Counts the frames the cull wrote; `changed` is against the one before.
     *
     * A reader that did not see that frame rebuilds its copy whole, as for allChanged.
     */
    uint64_t serial = 0;
};

} // namespace Vkm::Engine

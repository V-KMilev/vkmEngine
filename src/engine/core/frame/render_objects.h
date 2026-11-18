#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "base/math/aabb.h"
#include "ecs/component/landscape/water.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief What one object is drawn with: its geometry, its material and its pose.
 *
 * Pure handles; the backend resolves, sorts and batches them.
 */
struct ObjectDraw {
    static constexpr uint32_t MAX_WATERS = 0xFFFFu;  ///< The most Waters drawn as water: ObjectDraw::water
                                                     ///< names one in 16 bits.

    MeshHandle     mesh;  ///< The Mesh component's handle, or the level an LOD component selected: across a
                          ///< hand-over, the one fading out (ObjectFade).
    MaterialHandle material;

    uint32_t skinCount = 0;  ///< Bones in this object's slice of RenderView::SceneLists::skinMatrices. Zero
                             ///< means nothing poses it and it draws unskinned, i.e. in bind pose.

    uint16_t water = 0;  ///< The water it is: 1 + its Water's place in RenderObjects::waters, 0 for none. A
                         ///< posed mesh is never drawn as water.

    bool viewmodel = false;  ///< A Viewmodel mesh seen through the eye: drawn through
                             ///< CameraData::viewmodelViewProjection.

    bool fixed = false;  ///< A fixed mesh the cull can keep - not movable (Mesh::movable), not skinned, not
                         ///< moved by waves (Water::waves) and not a viewmodel: while
                         ///< RenderObjects::fixedSerial holds, it is as it was.

    bool operator==(const ObjectDraw& other) const {
        return mesh == other.mesh && material == other.material && skinCount == other.skinCount
            && water == other.water && viewmodel == other.viewmodel && fixed == other.fixed;
    }

    bool operator!=(const ObjectDraw& other) const { return !(*this == other); }
};

/**
 * @brief How the eye draws an object on a share of its samples: across a hand-over between two
 *        levels of detail, or as it shrinks toward the screen-size cull.
 *
 * Both levels of a hand-over read one pattern, the next level taking samples ObjectDraw::mesh
 * gives up, so together they cover each sample at most once. Shrinking, an object gives samples up to
 * what lies behind, a hand-over's two levels together, each keeping its share of what still shows.
 */
struct ObjectFade {
    MeshHandle next;           ///< The level fading in; null outside a hand-over.
    float      amount = 0.0f;  ///< The share of its samples ObjectDraw::mesh gives up; 0 draws it whole.
    float      taken  = 0.0f;  ///< The share `next` takes of them: all of `amount` unless it is shrinking.
};

/**
 * @brief A level of detail a fixed object may draw, and the mesh it was given (RenderObjects::bases).
 */
struct ObjectLevel {
    MeshHandle mesh;
    MeshHandle given;
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

    std::vector<Water>      waters;  ///< Every Water, in its storage's order, as ObjectDraw::water names
                                     ///< them.

    std::vector<uint32_t>   skinFirst;  ///< Per object: where its bones start in
                                        ///< RenderView::SceneLists::skinMatrices. Zero when
                                        ///< ObjectDraw::skinCount is.

    std::vector<uint64_t>   keys;  ///< Per object: what names it whatever its index - its entity and the mesh
                                   ///< it was given (not a level of detail), and for a terrain's chunk or
                                   ///< plant which one. An index moves as meshes come and go and a key does
                                   ///< not, but for a plant's: its place among its terrain's plants
                                   ///< (TerrainFrame::plants), which moves as they grow again or a mesh of
                                   ///< theirs loads.

    std::vector<uint64_t>   shapes;  ///< Per object: the version of the mesh it was given (not a level of
                                     ///< detail), so what is baked from its shape can tell the shape changed
                                     ///< under the same mesh.

    std::vector<MeshHandle> bases;  ///< Per object: the mesh it was given, not the level of detail drawn -
                                    ///< the one `shapes` versions: the Mesh component's, a terrain chunk's
                                    ///< finest level, a plant's. What is baked from an object's shape is
                                    ///< baked from this, wherever the eye stands.

    std::vector<ObjectFade> fades;  ///< Per object: how the eye fades it, written every frame for what the
                                    ///< camera sees. Never a change (`changed`): only the eye's draws read it
                                    ///< (MeshOf::Eye), built every frame; anything else
                                    ///< draws ObjectDraw::mesh whole.

    std::vector<uint32_t> visible;  ///< The objects the camera sees, in object order, unsorted.

    std::vector<uint32_t> scene;  ///< Every drawn object, camera or not, shadow casters first. For drawing
                                  ///< that is not the camera's: shadows and captures.

    uint32_t casterCount = 0;  ///< How many of `scene` cast shadows, which are its prefix.

    std::vector<ObjectLevel> levels;  ///< Every level a listed fixed object may draw but its given mesh,
                                      ///< once each, whichever it draws now: an LOD's, a terrain chunk's
                                      ///< coarser ones, a plant's. Written when fixedSerial moves, so a
                                      ///< reader readies them before the eye picks one (a camera cut
                                      ///< picks many).

    std::vector<uint32_t> changed;  ///< The objects whose matrix, box, draw, first bone or place in the
                                    ///< scene-wide list or its caster prefix differ from the last frame's, in
                                    ///< object order, an index now holding another Mesh among them. A reader
                                    ///< keeping a copy of the per-object arrays (the GPU's) resizes it to
                                    ///< theirs, keeping what it held, and refreshes these alone. Empty while
                                    ///< allChanged.

    bool allChanged = true;  ///< Every object may differ from the last frame's: the world or the asset graph
                             ///< was replaced, the frame before published none, or a fixed object moved in
                             ///< it. A copy is rebuilt whole.

    uint64_t fixedSerial = 0;  ///< Moved by every frame the cull writes (serial) in which a fixed object
                               ///< (ObjectDraw::fixed) changed, was listed or unlisted, became or stopped
                               ///< being fixed, or allChanged holds; a frame it does not write leaves it, the
                               ///< next it writes moves it. While it holds every fixed object is as it was
                               ///< but for the eye's level of detail: what they drew into a held picture, a
                               ///< shadow layer's, still stands at the level drawn.

    uint64_t serial = 0;  ///< Counts the frames the cull wrote; `changed` is against the one before. A reader
                          ///< that did not see that frame rebuilds its copy whole, as for allChanged.
};

} // namespace Vkm::Engine

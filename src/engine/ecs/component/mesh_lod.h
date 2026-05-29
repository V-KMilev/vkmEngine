#pragma once

#include <cstdint>

#include "resource/mesh_asset.h"

namespace Engine {

/**
 * @brief Discrete level-of-detail chain for a renderable, chosen per frame by
 *        projected screen size.
 *
 * Optional sibling to Mesh: an entity carrying both renders the LOD level that
 * matches its on-screen size instead of Mesh::mesh. levels[0] is the highest
 * detail (conventionally the same handle as Mesh::mesh) and coarser levels
 * follow. RenderView::build estimates the entity's projected pixel height and
 * picks the coarsest level i whose switchHeights[i] threshold is met
 * (switchHeights descending; index 0 is unused - level 0 is the default).
 *
 * Only the camera-facing drawables use LOD; shadow casters keep Mesh::mesh, so
 * the shadow-caster cache (which keys on a stable mesh id) is unaffected.
 *
 * Data-only. Each level is an ordinary MeshAsset, so a level can be a
 * lower-tessellation procedural mesh today or a decimated copy of the base
 * later - the selection here doesn't care how the geometry was produced.
 */
struct MeshLOD {
    static constexpr int MAX_LEVELS = 4;

    MeshHandle levels[MAX_LEVELS];                ///< Finest (0) to coarsest; invalid handle = unused slot.
    float      switchHeights[MAX_LEVELS] = {};    ///< Switch to level i when projected pixel height < this (index 0 unused).
    uint8_t    count = 0;                         ///< Valid level count; < 2 means no LOD (level 0 always).
};

} // namespace Engine

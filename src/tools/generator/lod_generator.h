#pragma once

#include <cstdint>

#include "ecs/component/lod.h"
#include "resource/asset/mesh_asset.h"

namespace Engine {

class ResourceManager;

/**
 * @brief How many levels to build and how fast they coarsen.
 *
 * The defaults halve the decimation grid and roughly double the range per level,
 * which keeps the on-screen size of a level's triangles about constant as it
 * recedes - the property that makes a switch hard to notice.
 */
struct LODGenSettings {
    uint32_t extraLevels        = 2;      ///< Levels built below the source; the source is always level 0.
    float    firstDistance      = 30.0f;  ///< Range of the source level, in world units.
    float    distanceMultiplier = 2.5f;   ///< Each level reaches this much further than the last.
    uint32_t firstGrid          = 12;     ///< Decimation grid for the first generated level.
    float    gridFalloff        = 0.5f;   ///< Grid multiplier per level; lower = coarser faster.
};

/**
 * @brief Build an LOD component for @p source by decimating it.
 *
 * For geometry the engine generated itself, re-tessellating at a lower
 * resolution beats this - a sphere rebuilt with fewer segments is strictly
 * better than one clustered down. This exists for the case where that is not an
 * option: imported meshes, where the only thing available is the triangles.
 *
 * Generated levels are registered as named assets derived from the source's
 * name, so they serialize and resolve like any other mesh rather than being
 * anonymous runtime data that a scene save would lose.
 *
 * A level that decimation could not usefully coarsen (already small, or it
 * collapsed) is dropped rather than added, so a simple mesh does not gain
 * levels that cost a comparison and change nothing.
 *
 * @param resources Registers the generated meshes; also resolves @p source.
 * @param source    Mesh to build levels from; becomes level 0.
 * @param settings  Level count and coarsening curve.
 * @return The component to attach. Empty levels when @p source is unresolvable.
 */
LOD generateLOD(ResourceManager& resources, MeshHandle source,
                const LODGenSettings& settings = {});

} // namespace Engine

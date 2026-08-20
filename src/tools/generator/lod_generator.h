#pragma once

#include <cstdint>

#include "ecs/component/render/lod.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

class ResourceManager;

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
 * @param resources   Registers the generated meshes; also resolves @p source.
 * @param source      Mesh to build levels from; becomes level 0.
 * @param extraLevels Levels built below the source; the source is always level 0.
 * @return The component to attach. Empty levels when @p source is unresolvable.
 */
LOD generateLOD(ResourceManager& resources, MeshHandle source, uint32_t extraLevels);

} // namespace Vkm::Engine

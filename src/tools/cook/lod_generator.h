#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "ecs/component/render/lod.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Build an LOD component for @p source by decimating it.
 *
 * Levels are named assets derived from the source's name, so they serialize like any mesh. A
 * level decimation could not usefully coarsen is dropped. Each hands over where the next one's
 * simplifier error projects to about a pixel at 1080 lines and 60 degrees; the error is in mesh
 * units, so LOD::bias compensates for scale and a different camera.
 *
 * @param resources   Registers the generated meshes; also resolves @p source.
 * @param source      Mesh to build levels from; becomes level 0.
 * @param extraLevels Levels built below the source; the source is always level 0.
 * @return The component to attach. Empty levels when @p source is unresolvable or has no triangles.
 */
LOD generateLOD(ResourceManager& resources, MeshHandle source, uint32_t extraLevels);

/**
 * @brief The recipe a decimated level is rebuilt from.
 *
 * The one writer of the `decimate` descriptor; a level is kept as this, not as triangles.
 *
 * @param baseName Name of the mesh the level is decimated from.
 * @param ratio    Share of the base's triangles to aim for, as decimateMesh takes it.
 * @return The `kind: decimate` source descriptor.
 */
nlohmann::json decimateRecipe(const std::string& baseName, float ratio);

/**
 * @brief The ratio a `decimate` recipe asks for.
 *
 * Without a numeric `ratio`, the first level generateLOD builds; a number outside (0, 1) is
 * passed through for decimateMesh to refuse.
 *
 * @param source The `kind: decimate` source descriptor.
 * @return The share of the base's triangles the level aims for.
 */
float decimateRatioFromRecipe(const nlohmann::json& source);

} // namespace Vkm::Engine

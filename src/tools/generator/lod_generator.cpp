#define VKM_LOG_CATEGORY "GENERATOR"

#include "generator/lod_generator.h"

#include <algorithm>
#include <string>

#include "logger.h"

#include "generator/mesh_generators.h"
#include "resource/resource_manager.h"

namespace Engine {

namespace {

// Below this a level is not worth keeping: decimation either failed to coarsen
// it or the mesh was already trivial, and an extra level would cost a
// comparison per frame to draw the same thing.
constexpr float MIN_REDUCTION = 0.8f;   // must drop at least 20% of the triangles

} // namespace

LOD generateLOD(ResourceManager& resources, MeshHandle source,
                const LODGenSettings& settings) {
    LOD lod;
    if (!source) {
        LOG_WARNING("generateLOD: unresolved source mesh");
        return lod;
    }

    const MeshAsset& sourceMesh = resources.get(source);
    if (sourceMesh.indices.size() < 3) {
        LOG_WARNING("generateLOD: source '%s' has no triangles", sourceMesh.name.c_str());
        return lod;
    }

    const std::string baseName = sourceMesh.name.empty()
        ? ("mesh:" + std::to_string(source.id()))
        : sourceMesh.name;

    float    distance = std::max(settings.firstDistance, 0.01f);
    float    grid     = static_cast<float>(std::max(settings.firstGrid, 1u));
    size_t   previousTriangles = sourceMesh.indices.size() / 3;

    lod.levels.push_back({source, distance});

    for (uint32_t level = 1; level <= settings.extraLevels; ++level) {
        distance *= std::max(settings.distanceMultiplier, 1.01f);

        // Re-read each iteration: adding an asset can reallocate the storage the
        // previous reference pointed into.
        MeshAsset decimated = decimateMesh(resources.get(source),
                                           static_cast<uint32_t>(std::max(grid, 1.0f)));
        grid *= std::clamp(settings.gridFalloff, 0.05f, 0.99f);

        const size_t triangles = decimated.indices.size() / 3;
        if (triangles < 1 || static_cast<float>(triangles) > previousTriangles * MIN_REDUCTION) {
            LOG_INFO("generateLOD: '%s' level %u dropped (%zu -> %zu triangles is not a reduction)",
                     baseName.c_str(), level, previousTriangles, triangles);
            continue;
        }

        const std::string name = baseName + ":lod" + std::to_string(level);
        decimated.name = name;

        // Reuse an existing level of the same name so regenerating does not
        // accumulate a new asset per press.
        MeshHandle handle = resources.findByName<MeshAsset>(name);
        if (handle) {
            resources.edit(handle) = std::move(decimated);
            resources.commit(handle);
        } else {
            handle = resources.add(std::move(decimated), name);
        }

        lod.levels.push_back({handle, distance});
        previousTriangles = triangles;
    }

    LOG_INFO("generateLOD: '%s' -> %zu level(s)", baseName.c_str(), lod.levels.size());
    return lod;
}

} // namespace Engine

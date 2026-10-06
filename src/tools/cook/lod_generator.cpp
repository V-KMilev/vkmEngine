#define VKM_LOG_CATEGORY "GENERATOR"

#include "cook/lod_generator.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "logger.h"

#include "cook/mesh_processing.h"
#include "resource/resource_manager.h"
#include "resource/asset_source_kind.h"

namespace Vkm::Engine {

namespace {

// A level keeping more than this share of the level above's triangles is not worth a level.
constexpr float MAX_KEPT_SHARE = 0.8f;

// A level takes over where its error covers this many pixels of a 1080-line image at
// LOD::REFERENCE_P11; LOD::bias answers a taller window.
constexpr float SWITCH_ERROR_PIXELS = 1.0f;
constexpr float REFERENCE_HEIGHT_PX = 1080.0f;

constexpr float FIRST_RATIO   = 0.5f;   // share of the source's triangles the first level aims for
constexpr float RATIO_FALLOFF = 0.25f;  // ratio multiplier per level; lower = coarser faster

// How far away @p error, in the mesh's units, projects to SWITCH_ERROR_PIXELS.
float switchDistance(float error) {
    const float pixelsPerUnitAtOne = REFERENCE_HEIGHT_PX * LOD::REFERENCE_P11 * 0.5f;
    return error * pixelsPerUnitAtOne / SWITCH_ERROR_PIXELS;
}

} // namespace

LOD generateLOD(ResourceManager& resources, MeshHandle source, uint32_t extraLevels) {
    LOD lod;
    if (!source) {
        LOG_WARNING("generateLOD: unresolved source mesh");
        return lod;
    }

    const MeshAsset& sourceMesh = resources.get(source);
    if (sourceMesh.indices.size() < 3) {
        LOG_WARNING("generateLOD: source '%s' has no triangles", sourceMesh.name().c_str());
        return lod;
    }

    const std::string baseName = sourceMesh.name();

    float  ratio             = FIRST_RATIO;
    size_t previousTriangles = sourceMesh.indices.size() / 3;

    lod.levels.push_back({source, 0.0f});

    for (uint32_t level = 1; level <= extraLevels; ++level) {
        // Re-read each iteration: adding an asset can reallocate the storage.
        const float levelRatio = ratio;
        float       error      = 0.0f;
        MeshAsset decimated = decimateMesh(resources.get(source), levelRatio, &error);
        ratio *= RATIO_FALLOFF;

        const size_t triangles = decimated.indices.size() / 3;
        if (triangles < 1 || static_cast<float>(triangles) > previousTriangles * MAX_KEPT_SHARE) {
            LOG_INFO(
                "generateLOD: '%s' level %u dropped (%zu -> %zu triangles is not a reduction)",
                baseName.c_str(),
                level,
                previousTriangles,
                triangles
            );
            continue;
        }

        const std::string name = baseName + ":lod" + std::to_string(level);
        // AssetCooker skips a sourceless asset, and the scene could not resolve the level on load.
        decimated.sourceJson() = decimateRecipe(baseName, levelRatio);

        // A taken name is replaced in place, so regenerating does not accumulate assets.
        const MeshHandle handle = resources.add(std::move(decimated), name);

        // Never nearer than the previous hand-over, which a smaller error would put out of order.
        LODLevel& above = lod.levels.back();
        const float floor = lod.levels.size() > 1 ? lod.levels[lod.levels.size() - 2].maxDistance : 0.0f;
        above.maxDistance = std::max(switchDistance(error), floor);

        lod.levels.push_back({handle, 0.0f});
        previousTriangles = triangles;
    }

    // The last level's range selects nothing; twice the hand-over into it is where one more would
    // take over, as quartering the triangles roughly doubles the error.
    if (lod.levels.size() > 1) {
        lod.levels.back().maxDistance = 2.0f * lod.levels[lod.levels.size() - 2].maxDistance;
    }

    LOG_INFO("generateLOD: '%s' -> %zu level(s)", baseName.c_str(), lod.levels.size());
    return lod;
}

nlohmann::json decimateRecipe(const std::string& baseName, float ratio) {
    return {
        {"kind",                AssetSourceKind::DECIMATE},
        {AssetSourceKey::BASE,  baseName},
        {AssetSourceKey::RATIO, ratio},
    };
}

float decimateRatioFromRecipe(const nlohmann::json& source) {
    const auto it = source.find(AssetSourceKey::RATIO);
    if (it == source.end() || !it->is_number()) return FIRST_RATIO;
    return it->get<float>();
}

} // namespace Vkm::Engine

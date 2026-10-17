#include "system/terrain/terrain_system.h"

#include <algorithm>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include "core/fnv1a.h"
#include "debug/profiler.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/render/terrain.h"
#include "ecs/scene.h"
#include "resource/resource_manager.h"
#include "system/physics/authoring/mesh_collider.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Whether grid vertices @p fromI..@p toI by @p fromJ..@p toJ hold the same heights in @p was
 *        as in @p now; clamped to the grid.
 *
 * @param was  The heights a chunk was built from; empty for none.
 * @param now  The heights now.
 * @param side Grid vertices a side.
 * @param fromI First column, which may lie off the grid.
 * @param toI   Last column, which may too.
 * @param fromJ First row.
 * @param toJ   Last row.
 * @return True when nothing in the region moved.
 */
bool sameRegion(
    const std::vector<float>& was,
    const std::vector<float>& now,
    int side,
    int fromI,
    int toI,
    int fromJ,
    int toJ
) {
    if (was.size() != now.size()) return false;
    const int i0 = std::max(fromI, 0);
    const int i1 = std::min(toI, side - 1);
    for (int j = std::max(fromJ, 0); j <= std::min(toJ, side - 1); ++j) {
        const size_t row = static_cast<size_t>(j) * side;
        const auto   at  = was.begin() + row;
        if (!std::equal(at + i0, at + i1 + 1, now.begin() + row + i0)) return false;
    }
    return true;
}

} // namespace

void TerrainSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("TerrainSystem");
    m_frame.chunks.clear();
    ResourceManager& resources = ctx.resources;

    // A replaced asset graph took what was made with it.
    if (resources.epoch() != m_epoch) {
        m_made.clear();
        m_epoch = resources.epoch();
    }
    for (auto& [slot, made] : m_made) made.seen = false;

    ctx.scene.forEach<Terrain, Transform>([&](EntityId id, const Terrain& terrain, const Transform& local) {
        if (!terrain.terrain || !resources.isAlive(terrain.terrain)) {
            // Nothing drawn, so nothing to collide with either, in the editor too.
            if (Collider* collider = ctx.scene.tryGet<Collider>(id)) syncTerrainCollider(*collider, nullptr);
            return;
        }
        Made& made = m_made[id.slot()];
        made.seen = true;
        if (made.terrain != terrain.terrain || made.version != resources.get(terrain.terrain).version()) {
            remake(resources, made, terrain.terrain, id.slot());
        }
        // Collided as drawn, in the editor too, where nothing ticks.
        if (Collider* collider = ctx.scene.tryGet<Collider>(id)) {
            syncTerrainCollider(*collider, &resources.get(terrain.terrain));
        }
        if (made.chunks.empty()) return;

        // Placed and turned as its entity, unscaled, as the solver places its collider.
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), resolvedWorldPosition(ctx.scene, id, local))
            * glm::mat4_cast(resolvedWorldRotation(ctx.scene, id, local));
        made.model       = model;
        made.entity      = id;
        made.castShadows = terrain.castShadows;
        const float width = static_cast<float>(TerrainChunks::CELLS) * made.size
            / static_cast<float>(made.resolution - 1);
        for (size_t c = 0; c < made.chunks.size(); ++c) {
            TerrainChunk chunk;
            chunk.entity      = id;
            chunk.levels      = made.chunks[c];
            chunk.material    = made.material;
            chunk.model       = model;
            chunk.bounds      = Math::transform(model, made.bounds[c]);
            chunk.width       = width;
            chunk.castShadows = terrain.castShadows;
            m_frame.chunks.push_back(chunk);
        }
    });

    for (auto it = m_made.begin(); it != m_made.end();) {
        if (it->second.seen) {
            ++it;
            continue;
        }
        release(resources, it->second);
        it = m_made.erase(it);
    }

    // The plants are listed again only when what they are made from moved.
    const uint64_t key = plantsKey(resources, ctx.scene);
    if (key != m_plantsKey) {
        listPlants(resources, ctx.scene);
        m_plantsKey = key;
        ++m_frame.plantsRevision;
    }
    ctx.terrain = &m_frame;
}

uint64_t TerrainSystem::plantsKey(const ResourceManager& resources, const Scene& scene) const {
    // A sum of each terrain's, so the order the map walks them in does not count.
    uint64_t key = 0;
    for (const auto& [slot, made] : m_made) {
        if (!made.seen || made.plants.empty() || !scene.isAlive(made.entity)) continue;
        uint64_t one = fnv1a64Bytes(&made.model, sizeof(made.model));
        const auto mix = [&](uint64_t value) { one = fnv1a64Bytes(&value, sizeof(value), one); };
        mix(slot);
        mix(made.version);
        mix(made.entity.generation());
        mix(made.castShadows ? 1 : 0);
        for (const TerrainFoliage& foliage : resources.get(made.terrain).foliage) {
            for (const TerrainPlantPart& part : foliage.parts) {
                const MeshAsset* mesh = resources.tryGet(part.mesh);
                mix(mesh && mesh->bounds().valid() ? mesh->version() + 1 : 0);
            }
        }
        key += one;
    }
    return key;
}

void TerrainSystem::listPlants(const ResourceManager& resources, const Scene& scene) {
    PROFILE_SCOPE("TerrainSystem/Plants");
    m_frame.plants.clear();
    m_frame.plantGroups.clear();
    // By slot, so the same world lists its plants in the same order.
    std::vector<const Made*> terrains;
    for (const auto& [slot, made] : m_made) {
        if (made.seen && !made.plants.empty() && scene.isAlive(made.entity)) terrains.push_back(&made);
    }
    std::sort(terrains.begin(), terrains.end(), [](const Made* a, const Made* b) {
        return a->entity.slot() < b->entity.slot();
    });

    for (const Made* made : terrains) {
        const TerrainAsset& asset = resources.get(made->terrain);
        for (size_t run = 0; run + 1 < made->plantRuns.size(); ++run) {
            TerrainPlantGroup group;
            group.first = static_cast<uint32_t>(m_frame.plants.size());
            for (uint32_t p = made->plantRuns[run]; p < made->plantRuns[run + 1]; ++p) {
                const TerrainPlants::Plant& plant = made->plants[p];
                if (plant.layer >= asset.foliage.size()) continue;
                const TerrainFoliage& foliage = asset.foliage[plant.layer];
                for (const TerrainPlantPart& part : foliage.parts) {
                    const MeshAsset* mesh = resources.tryGet(part.mesh);
                    if (!mesh || !mesh->bounds().valid()) continue;
                    TerrainPlantDraw draw;
                    draw.entity      = made->entity;
                    draw.mesh        = part.mesh;
                    draw.material    = part.material;
                    draw.model       = made->model * plant.local * part.local;
                    draw.bounds      = Math::transform(draw.model, mesh->bounds());
                    draw.maxDistance = foliage.maxDistance;
                    draw.castShadows = foliage.castShadows && made->castShadows;
                    if (group.count == 0) group.bounds = draw.bounds;
                    group.bounds.min  = glm::min(group.bounds.min, draw.bounds.min);
                    group.bounds.max  = glm::max(group.bounds.max, draw.bounds.max);
                    group.maxDistance = std::max(group.maxDistance, draw.maxDistance);
                    ++group.count;
                    m_frame.plants.push_back(draw);
                }
            }
            if (group.count > 0) m_frame.plantGroups.push_back(group);
        }
    }
}

void TerrainSystem::remake(ResourceManager& resources, Made& made, TerrainHandle handle, uint32_t slot) {
    PROFILE_SCOPE("TerrainSystem/Remake");
    // Stays where it is while other kinds are added below: each kind has its own storage.
    const TerrainAsset& terrain = resources.get(handle);
    const size_t        side    = terrain.resolution;
    const bool          sound   = TerrainAsset::validResolution(terrain.resolution)
        && terrain.heights.size() == side * side && terrain.weights.size() == side * side * 4;
    const bool regrid = !sound || made.terrain != handle || made.resolution != terrain.resolution
        || made.size != terrain.size;
    if (regrid) {
        release(resources, made);
        made = Made{};
        made.seen = true;
    }
    made.terrain = handle;
    made.version = terrain.version();
    if (!sound) return;
    made.resolution = terrain.resolution;
    made.size       = terrain.size;

    const std::string name = "terrain:" + std::to_string(slot) + ":";
    const uint32_t    per  = TerrainChunks::perSide(terrain);
    const auto        n    = static_cast<int>(TerrainChunks::CELLS);
    made.chunks.resize(static_cast<size_t>(per) * per);
    made.bounds.resize(made.chunks.size());
    for (uint32_t cz = 0; cz < per; ++cz) {
        for (uint32_t cx = 0; cx < per; ++cx) {
            // Its heights, and their neighbours', which its normals read: unchanged, it is as it was.
            const int  i0   = static_cast<int>(cx) * n - 1;
            const int  j0   = static_cast<int>(cz) * n - 1;
            const int  s    = static_cast<int>(side);
            const bool same = sameRegion(made.heights, terrain.heights, s, i0, i0 + n + 2, j0, j0 + n + 2);
            if (!regrid && same) continue;
            const size_t c = static_cast<size_t>(cz) * per + cx;
            for (uint32_t level = 0; level < TerrainChunks::LEVELS; ++level) {
                MeshAsset mesh = TerrainChunks::build(terrain, cx, cz, level);
                if (level == 0) made.bounds[c] = mesh.bounds();
                MeshHandle& held = made.chunks[c][level];
                if (held) {
                    resources.swapValue(held, mesh);
                } else {
                    const std::string chunk = name + "chunk" + std::to_string(cx) + "_" + std::to_string(cz);
                    held = resources.addPrivate(std::move(mesh), chunk + "_" + std::to_string(level));
                }
            }
        }
    }
    made.heights = terrain.heights;

    // Its weights as a map the layered material reads, a texel a grid vertex. Compared with a copy
    // of its own: the map's pixels may be let go once uploaded.
    if (!made.weightMap || made.weights != terrain.weights) {
        TextureAsset weights;
        weights.params.width  = terrain.resolution;
        weights.params.height = terrain.resolution;
        weights.pixelData     = terrain.weights;
        if (made.weightMap) resources.swapValue(made.weightMap, weights);
        else                made.weightMap = resources.addPrivate(std::move(weights), name + "weights");
        made.weights = terrain.weights;
    }

    // Chunk by chunk, so the cull passes over each chunk's plants in one test.
    TerrainPlants::scatter(terrain, made.plants);
    const float toGrid = 1.0f / terrain.spacing();
    const auto  cellOf = [&](float local) {
        const auto cell = static_cast<uint32_t>(std::max((local + 0.5f * terrain.size) * toGrid, 0.0f));
        return std::min(cell / TerrainChunks::CELLS, per - 1);
    };
    const auto chunkOf = [&](const TerrainPlants::Plant& plant) {
        return cellOf(plant.local[3].z) * per + cellOf(plant.local[3].x);
    };
    std::stable_sort(made.plants.begin(), made.plants.end(), [&](const auto& a, const auto& b) {
        return chunkOf(a) < chunkOf(b);
    });
    made.plantRuns.assign(static_cast<size_t>(per) * per + 1, 0);
    for (const TerrainPlants::Plant& plant : made.plants) ++made.plantRuns[chunkOf(plant) + 1];
    for (size_t c = 1; c < made.plantRuns.size(); ++c) made.plantRuns[c] += made.plantRuns[c - 1];

    MaterialAsset material;
    material.layers       = terrain.layers;
    material.layerWeights = made.weightMap;
    for (size_t l = 0; l < material.layerTiling.size(); ++l) {
        material.layerTiling[l] = glm::vec2(terrain.size / std::max(terrain.layerTiling[l], 0.01f));
    }
    if (!made.material) {
        made.material = resources.addPrivate(std::move(material), name + "material");
    } else {
        const MaterialAsset& was = resources.get(made.material);
        if (was.layers != material.layers || was.layerTiling != material.layerTiling) {
            resources.swapValue(made.material, material);
        }
    }
}

void TerrainSystem::release(ResourceManager& resources, Made& made) {
    for (const auto& levels : made.chunks) {
        for (const MeshHandle& level : levels) {
            if (level && resources.isAlive(level)) resources.remove(level);
        }
    }
    if (made.weightMap && resources.isAlive(made.weightMap)) resources.remove(made.weightMap);
    if (made.material && resources.isAlive(made.material)) resources.remove(made.material);
    made.chunks.clear();
    made.weightMap = {};
    made.weights.clear();
    made.material  = {};
}

} // namespace Vkm::Engine

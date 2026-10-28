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

/// Grid vertices i0..i1 by j0..j1, ends included; it may reach off the grid, which clamps it.
struct Span {
    int i0 = 0;
    int i1 = 0;
    int j0 = 0;
    int j1 = 0;
};

/// The vertices chunk @p cx, @p cz is built from: its own and a ring round them, which its normals read.
Span builtFrom(uint32_t cx, uint32_t cz) {
    const auto n = static_cast<int>(TerrainChunks::CELLS);
    const int  i = static_cast<int>(cx) * n;
    const int  j = static_cast<int>(cz) * n;
    return {i - 1, i + n + 1, j - 1, j + n + 1};
}

/// The corners of the squares chunk @p cx, @p cz's plants stand on.
Span grownFrom(uint32_t cx, uint32_t cz) {
    const auto n = static_cast<int>(TerrainChunks::CELLS);
    const int  i = static_cast<int>(cx) * n;
    const int  j = static_cast<int>(cz) * n;
    return {i, i + n, j, j + n};
}

/**
 * @brief Whether @p span holds the same values in @p was as in @p now, clamped to the grid.
 *
 * @param was  What was built from; empty for none.
 * @param now  The values now.
 * @param side Grid vertices a side.
 * @param span The vertices compared.
 * @return True when nothing in it moved.
 */
template<typename T>
bool sameRegion(const std::vector<T>& was, const std::vector<T>& now, int side, const Span& span) {
    if (was.size() != now.size()) return false;
    // Not a grid - a plant layer never painted is empty - so nothing in it to compare.
    if (now.size() != static_cast<size_t>(side) * side) return true;
    const int i0 = std::max(span.i0, 0);
    const int i1 = std::min(span.i1, side - 1);
    for (int j = std::max(span.j0, 0); j <= std::min(span.j1, side - 1); ++j) {
        const size_t row = static_cast<size_t>(j) * side;
        const auto   at  = was.begin() + row;
        if (!std::equal(at + i0, at + i1 + 1, now.begin() + row + i0)) return false;
    }
    return true;
}

/// Copy @p span of @p now over @p was, clamped to the grid; the whole of it when they differ in size.
template<typename T>
void copyRegion(std::vector<T>& was, const std::vector<T>& now, int side, const Span& span) {
    const size_t grid = static_cast<size_t>(side) * side;
    if (was.size() != grid || now.size() != grid) {
        was = now;
        return;
    }
    const int i0 = std::max(span.i0, 0);
    const int i1 = std::min(span.i1, side - 1);
    for (int j = std::max(span.j0, 0); j <= std::min(span.j1, side - 1); ++j) {
        const size_t row = static_cast<size_t>(j) * side;
        std::copy(now.begin() + row + i0, now.begin() + row + i1 + 1, was.begin() + row + i0);
    }
}

} // namespace

glm::mat4 terrainModel(const Scene& scene, EntityId entity, const Transform& local) {
    return glm::translate(glm::mat4(1.0f), resolvedWorldPosition(scene, entity, local))
        * glm::mat4_cast(resolvedWorldRotation(scene, entity, local));
}

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

        const glm::mat4 model = terrainModel(ctx.scene, id, local);
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
                    group.bounds.expand(draw.bounds);
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
    const auto        s    = static_cast<int>(side);
    const size_t      all  = static_cast<size_t>(per) * per;
    made.chunks.resize(all);
    made.bounds.resize(all);
    // A chunk whose heights are unchanged is as it was.
    std::vector<uint8_t> moved(all, regrid ? 1 : 0);
    for (uint32_t cz = 0; cz < per; ++cz) {
        for (uint32_t cx = 0; cx < per; ++cx) {
            const size_t c = static_cast<size_t>(cz) * per + cx;
            if (!regrid && sameRegion(made.heights, terrain.heights, s, builtFrom(cx, cz))) continue;
            moved[c] = 1;
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
    // What they were built from, kept: the regions that moved, or all of it anew.
    if (made.heights.size() != terrain.heights.size()) {
        made.heights = terrain.heights;
    } else {
        for (uint32_t cz = 0; cz < per; ++cz) {
            for (uint32_t cx = 0; cx < per; ++cx) {
                if (moved[static_cast<size_t>(cz) * per + cx]) {
                    copyRegion(made.heights, terrain.heights, s, builtFrom(cx, cz));
                }
            }
        }
    }

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

    // Its plants chunk by chunk, so the cull passes over each chunk's in one test: grown again
    // where a chunk's heights or density moved, or everywhere once a layer scatters otherwise.
    const std::vector<TerrainFoliage>& foliage = terrain.foliage;
    bool everywhere = regrid || made.plantRuns.size() != all + 1 || made.foliage.size() != foliage.size();
    for (size_t l = 0; !everywhere && l < foliage.size(); ++l) {
        everywhere = !TerrainPlants::sameScatter(made.foliage[l], foliage[l]);
    }
    std::vector<uint8_t> grows(all, everywhere ? 1 : 0);
    bool                 any = everywhere;
    for (uint32_t cz = 0; cz < per; ++cz) {
        for (uint32_t cx = 0; cx < per; ++cx) {
            const size_t c = static_cast<size_t>(cz) * per + cx;
            for (size_t l = 0; !grows[c] && l < foliage.size(); ++l) {
                grows[c] = !sameRegion(made.foliage[l].density, foliage[l].density, s, grownFrom(cx, cz));
            }
            grows[c] = grows[c] || moved[c];
            any      = any || grows[c];
        }
    }
    if (any) {
        std::vector<TerrainPlants::Plant> plants;
        plants.reserve(made.plants.size());
        std::vector<uint32_t> runs(all + 1, 0);
        for (uint32_t cz = 0; cz < per; ++cz) {
            for (uint32_t cx = 0; cx < per; ++cx) {
                const size_t c = static_cast<size_t>(cz) * per + cx;
                if (grows[c]) {
                    // Its squares: the corners' span short of its last row and column.
                    const Span on = grownFrom(cx, cz);
                    TerrainPlants::grow(terrain, on.i0, on.i1, on.j0, on.j1, plants);
                } else {
                    const auto held = made.plants.begin();
                    plants.insert(plants.end(), held + made.plantRuns[c], held + made.plantRuns[c + 1]);
                }
                runs[c + 1] = static_cast<uint32_t>(plants.size());
            }
        }
        made.plants.swap(plants);
        made.plantRuns.swap(runs);
    }
    // What they were grown from, kept as the heights are.
    if (everywhere) {
        made.foliage = foliage;
    } else if (any) {
        for (uint32_t cz = 0; cz < per; ++cz) {
            for (uint32_t cx = 0; cx < per; ++cx) {
                if (!grows[static_cast<size_t>(cz) * per + cx]) continue;
                for (size_t l = 0; l < foliage.size(); ++l) {
                    copyRegion(made.foliage[l].density, foliage[l].density, s, grownFrom(cx, cz));
                }
            }
        }
    }

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

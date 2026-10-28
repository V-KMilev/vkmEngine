#include "system/terrain/terrain_chunks.h"

#include <algorithm>
#include <functional>

namespace Vkm::Engine::TerrainChunks {

namespace {

/// How far a level's skirt hangs below its rim at least, in its own grid steps.
constexpr float SKIRT_STEPS = 4.0f;

} // namespace

uint32_t perSide(const TerrainAsset& terrain) {
    return (terrain.resolution - 1) / CELLS;
}

MeshAsset build(const TerrainAsset& terrain, uint32_t cx, uint32_t cz, uint32_t level) {
    const uint32_t step    = 1u << level;
    const uint32_t n       = CELLS / step + 1;  // vertices a side at this level
    const uint32_t fromI   = cx * CELLS;
    const uint32_t fromJ   = cz * CELLS;
    const float    spacing = terrain.spacing();
    const float    half    = 0.5f * terrain.size;
    const float    texels  = static_cast<float>(terrain.resolution);

    MeshAsset mesh;
    mesh.vertices.reserve(static_cast<size_t>(n) * n + 4 * n);
    mesh.lightmapUVs.reserve(static_cast<size_t>(n) * n + 4 * n);
    for (uint32_t b = 0; b < n; ++b) {
        for (uint32_t a = 0; a < n; ++a) {
            const uint32_t i = fromI + a * step;
            const uint32_t j = fromJ + b * step;
            Vertex v;
            v.position = {
                static_cast<float>(i) * spacing - half,
                terrain.heights[static_cast<size_t>(j) * terrain.resolution + i],
                static_cast<float>(j) * spacing - half
            };
            v.normal = terrain.normalAt(static_cast<int>(i), static_cast<int>(j));
            v.uv     = {(static_cast<float>(i) + 0.5f) / texels, (static_cast<float>(j) + 0.5f) / texels};
            // Along +U, which runs along +X; V runs along +Z, which cross(normal, tangent) points away
            // from, so the handedness turns it.
            v.tangent = glm::vec4(glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f) - v.normal * v.normal.x), -1.0f);
            mesh.vertices.push_back(v);
            // The chunk's grid as its lightmap, alike at every level.
            mesh.lightmapUVs.emplace_back(glm::vec2(a * step, b * step) / static_cast<float>(CELLS));
        }
    }

    // Two triangles a square, facing up, split along the diagonal heightAt reads.
    mesh.indices.reserve(static_cast<size_t>(n - 1) * (n - 1) * 6 + 4 * (n - 1) * 12);
    for (uint32_t b = 0; b + 1 < n; ++b) {
        for (uint32_t a = 0; a + 1 < n; ++a) {
            const uint32_t v00 = b * n + a;
            const uint32_t v10 = v00 + 1;
            const uint32_t v01 = v00 + n;
            const uint32_t v11 = v01 + 1;
            mesh.indices.insert(mesh.indices.end(), {v00, v11, v10, v00, v01, v11});
        }
    }

    // Each rim another chunk meets again, dropped, joined to the rim by a strip facing both ways:
    // either side of it may be the one a gap shows. The terrain's own edge meets none. A coarser
    // neighbour's edge strays from this rim by no more than the rim rises along it, so the skirt
    // hangs at least that far.
    const float least = SKIRT_STEPS * spacing * static_cast<float>(step);
    const auto  skirt = [&](const std::function<uint32_t(uint32_t)>& rim) {
        float low  = mesh.vertices[rim(0)].position.y;
        float high = low;
        for (uint32_t k = 1; k < n; ++k) {
            low  = std::min(low, mesh.vertices[rim(k)].position.y);
            high = std::max(high, mesh.vertices[rim(k)].position.y);
        }
        const float drop  = std::max(least, high - low);
        const auto  below = static_cast<uint32_t>(mesh.vertices.size());
        for (uint32_t k = 0; k < n; ++k) {
            Vertex v = mesh.vertices[rim(k)];
            v.position.y -= drop;
            mesh.vertices.push_back(v);
            mesh.lightmapUVs.push_back(mesh.lightmapUVs[rim(k)]);
        }
        for (uint32_t k = 0; k + 1 < n; ++k) {
            const uint32_t top0 = rim(k);
            const uint32_t top1 = rim(k + 1);
            const uint32_t low0 = below + k;
            const uint32_t low1 = below + k + 1;
            mesh.indices.insert(mesh.indices.end(), {top0, top1, low1, top0, low1, low0});
            mesh.indices.insert(mesh.indices.end(), {top0, low1, top1, top0, low0, low1});
        }
    };
    const uint32_t last = perSide(terrain) - 1;
    if (cz > 0)    skirt([&](uint32_t k) { return k; });
    if (cz < last) skirt([&](uint32_t k) { return (n - 1) * n + k; });
    if (cx > 0)    skirt([&](uint32_t k) { return k * n; });
    if (cx < last) skirt([&](uint32_t k) { return k * n + n - 1; });

    mesh.computeAndSetBounds();
    return mesh;
}

uint32_t levelAt(float distance, float chunkWidth) {
    uint32_t level = 0;
    float    reach = REACH * chunkWidth;
    while (level + 1 < LEVELS && distance > reach) {
        ++level;
        reach *= 2.0f;
    }
    return level;
}

} // namespace Vkm::Engine::TerrainChunks

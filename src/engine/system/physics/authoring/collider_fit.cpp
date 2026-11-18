#include "system/physics/authoring/collider_fit.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

#include "vkm_physics/geometry/triangle.h"

#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

namespace {

// A crossing at the scan's origin is the surface it started on, counted again.
constexpr float MIN_ADVANCE = 1e-6f;

// Crossings closer than this fraction of a cell are one surface hit twice. A fraction, since the mesh
// may be any size.
constexpr float CROSSING_MERGE_FRACTION = 1e-3f;

// A center takes the signed scale (a mirrored mesh's box sits mirrored), a half-extent its magnitude.
// The floor applies after the scale, so its millimeter is a world millimeter.
ColliderPart boxSpanning(const glm::vec3& lo, const glm::vec3& hi, const glm::vec3& scale) {
    ColliderPart box;
    const glm::vec3 half = (hi - lo) * 0.5f * glm::abs(scale);

    box.center      = (lo + hi) * 0.5f * scale;
    box.halfExtents = glm::max(half, glm::vec3(MIN_HALF_EXTENT));
    return box;
}

} // namespace

std::vector<ColliderPart> fitBoxesToMesh(const MeshAsset& mesh, int detail, const glm::vec3& scale) {
    detail = std::clamp(detail, 1, COLLIDER_FIT_MAX_DETAIL);

    const glm::vec3 bmin = mesh.boundsMin;
    const glm::vec3 bmax = mesh.boundsMax;
    const glm::vec3 ext  = bmax - bmin;

    if (detail <= 1 || mesh.indices.size() < 3
        || ext.x <= 0.0f || ext.y <= 0.0f || ext.z <= 0.0f) {
        return { boxSpanning(bmin, bmax, scale) };
    }

    // A malformed mesh (index >= vertex count) falls back to the bounds box rather than reading out of
    // bounds in the hot loop.
    const uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t idx : mesh.indices) {
        if (idx >= vertexCount) return { boxSpanning(bmin, bmax, scale) };
    }

    const int R = detail;
    const glm::vec3 cell = ext / static_cast<float>(R);
    const glm::vec3 dir(1.0f, 0.0f, 0.0f);
    const float xStart   = bmin.x - cell.x;  // ray origin: outside on -X
    const float mergeEps = cell.x * CROSSING_MERGE_FRACTION;

    std::vector<ColliderPart> boxes;
    std::vector<float>        xs;   // surface crossing x-coords, reused per column

    for (int iy = 0; iy < R; ++iy) {
        for (int iz = 0; iz < R; ++iz) {
            const glm::vec3 o(
                xStart,
                bmin.y + (static_cast<float>(iy) + 0.5f) * cell.y,
                bmin.z + (static_cast<float>(iz) + 0.5f) * cell.z
            );
            xs.clear();
            for (std::size_t i = 0; i + 3 <= mesh.indices.size(); i += 3) {
                const glm::vec3& a = mesh.vertices[mesh.indices[i + 0]].position;
                const glm::vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
                const glm::vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
                float t = 0.0f;
                if (Physics::rayTriangle(o, dir, a, b, c, t) && t > MIN_ADVANCE) xs.push_back(o.x + t);
            }
            if (xs.size() < 2) continue;
            std::sort(xs.begin(), xs.end());

            // A shared edge is hit by both triangles; collapsing keeps inside/outside parity.
            std::size_t w = 0;
            for (std::size_t r = 0; r < xs.size(); ++r) {
                if (w == 0 || xs[r] - xs[w - 1] > mergeEps) xs[w++] = xs[r];
            }
            xs.resize(w);

            for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
                const float x0 = xs[k];
                const float x1 = xs[k + 1];
                if (x1 - x0 < mergeEps) continue;
                const glm::vec3 lo(
                    x0,
                    bmin.y + static_cast<float>(iy)     * cell.y,
                    bmin.z + static_cast<float>(iz)     * cell.z
                );
                const glm::vec3 hi(
                    x1,
                    bmin.y + static_cast<float>(iy + 1) * cell.y,
                    bmin.z + static_cast<float>(iz + 1) * cell.z
                );
                boxes.push_back(boxSpanning(lo, hi, scale));
            }
        }
    }

    if (boxes.empty()) {
        return { boxSpanning(bmin, bmax, scale) };
    }
    return boxes;
}

namespace {

/// Each axis of @p scale at least @p least in size, its sign kept: a mirrored mesh stays mirrored.
glm::vec3 signedAtLeast(const glm::vec3& scale, float least) {
    glm::vec3 held;
    for (int axis = 0; axis < 3; ++axis) {
        held[axis] = std::copysign(std::max(std::abs(scale[axis]), least), scale[axis]);
    }
    return held;
}

} // namespace

std::vector<ColliderPart> rescaleParts(
    std::vector<ColliderPart> parts,
    const glm::vec3& from,
    const glm::vec3& to
) {
    glm::vec3 change(1.0f);
    for (int axis = 0; axis < 3; ++axis) {
        if (from[axis] != 0.0f) change[axis] = to[axis] / from[axis];
    }
    if (change == glm::vec3(1.0f)) return parts;

    const glm::vec3 size  = glm::abs(change);
    const float     girth = std::sqrt(size.x * size.z);
    const glm::vec3 least(MIN_HALF_EXTENT);

    for (ColliderPart& part : parts) {
        if (part.shape == ColliderShape::Terrain) continue;
        // Height takes Y's change and the radius the girth's; what the radius gives up or takes goes to
        // the half height, nothing at all when Y and the girth change alike.
        const float radius = std::max(part.radius * girth, MIN_HALF_EXTENT);
        const float rest   = part.radius * size.y - radius;

        part.center      *= change;
        part.halfExtents  = glm::max(part.halfExtents * size, least);
        part.meshScale    = signedAtLeast(part.meshScale * change, MIN_HALF_EXTENT);
        part.halfHeight   = std::max(part.halfHeight * size.y + rest, 0.0f);
        part.radius       = radius;
    }
    return parts;
}

} // namespace Vkm::Engine

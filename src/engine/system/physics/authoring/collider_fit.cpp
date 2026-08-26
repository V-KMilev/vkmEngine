#include "system/physics/authoring/collider_fit.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "resource/asset/mesh_asset.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Moller-Trumbore ray-triangle test.
 *
 * True, with a positive @p t, when the ray crosses the triangle in front of
 * its origin; a crossing behind the origin does not count as a hit.
 */
bool rayHitsTriangle(
    const glm::vec3& o, const glm::vec3& d,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
    float& t
) {
    const glm::vec3 e1 = v1 - v0;
    const glm::vec3 e2 = v2 - v0;
    const glm::vec3 p  = glm::cross(d, e2);
    const float det = glm::dot(e1, p);
    // Parallel to the triangle: the reciprocal below would be meaningless, and
    // this is a question about the division rather than about the geometry.
    if (std::fabs(det) < glm::epsilon<float>()) return false;

    const float inv = 1.0f / det;
    const glm::vec3 tv = o - v0;
    const float u = glm::dot(tv, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;

    const glm::vec3 q = glm::cross(tv, e1);
    const float v = glm::dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;

    // Ahead of the origin rather than at it. The casts below start on a cell
    // boundary, and a crossing reported at zero is the surface they started
    // from being counted again.
    constexpr float MIN_ADVANCE = 1e-6f;
    t = glm::dot(e2, q) * inv;
    return t > MIN_ADVANCE;
}

// Two crossings closer together than this fraction of a cell are the same
// surface hit twice, not a span with a thickness. A fraction rather than a
// length: the mesh may be authored at any size, and what makes two crossings
// the same is that the column found nothing between them.
constexpr float CROSSING_MERGE_FRACTION = 1e-3f;

// A centre scales by the signed scale - a mirrored mesh's box sits on the
// mirrored side - while a half-extent has no side and takes the magnitude.
// The floor is applied after the scale, so the millimetre it promises is a
// millimetre in the world rather than in whatever units the mesh was drawn in.
ColliderPart boundsBox(const glm::vec3& bmin, const glm::vec3& bmax, const glm::vec3& scale) {
    ColliderPart box;
    box.center      = (bmin + bmax) * 0.5f * scale;
    const glm::vec3 half = (bmax - bmin) * 0.5f * glm::abs(scale);
    box.halfExtents = glm::max(half, glm::vec3(Physics::MIN_HALF_EXTENT));
    return box;
}

} // namespace

std::vector<ColliderPart> fitBoxesToMesh(const MeshAsset& mesh, int detail, const glm::vec3& scale) {
    detail = std::clamp(detail, 1, COLLIDER_FIT_MAX_DETAIL);

    const glm::vec3 bmin     = mesh.boundsMin;
    const glm::vec3 bmax     = mesh.boundsMax;
    const glm::vec3 ext      = bmax - bmin;
    const glm::vec3 absScale = glm::abs(scale);

    // detail 1 (or no usable geometry) -> a single box enclosing the bounds.
    if (detail <= 1 || mesh.indices.size() < 3
        || ext.x <= 0.0f || ext.y <= 0.0f || ext.z <= 0.0f) {
        return { boundsBox(bmin, bmax, scale) };
    }

    // The raycast path below indexes vertices by index-buffer values. Validate
    // the buffer once up front so a malformed mesh (index >= vertex count) falls
    // back to the bounds box instead of reading out of bounds in the hot loop.
    const uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t idx : mesh.indices) {
        if (idx >= vertexCount) return { boundsBox(bmin, bmax, scale) };
    }

    const int       R    = detail;
    const glm::vec3  cell = ext / static_cast<float>(R);
    const glm::vec3  dir(1.0f, 0.0f, 0.0f);     // scan along +X
    const float      xStart   = bmin.x - cell.x;   // ray origin: outside on -X
    // Collapses crossings that landed on the same point.
    const float      mergeEps = cell.x * CROSSING_MERGE_FRACTION;

    std::vector<ColliderPart> boxes;
    std::vector<float>        xs;   // surface crossing x-coords, reused per column

    // One +X ray per (y, z) cell, its crossings paired into inside spans. Each
    // span is one box - exact along X, one cell thick in Y/Z - which keeps the
    // count to ~R^2 instead of a per-cell R^3 sweep.
    for (int iy = 0; iy < R; ++iy) {
        for (int iz = 0; iz < R; ++iz) {
            const glm::vec3 o(xStart,
                              bmin.y + (static_cast<float>(iy) + 0.5f) * cell.y,
                              bmin.z + (static_cast<float>(iz) + 0.5f) * cell.z);
            xs.clear();
            for (std::size_t i = 0; i + 3 <= mesh.indices.size(); i += 3) {
                const glm::vec3& a = mesh.vertices[mesh.indices[i + 0]].position;
                const glm::vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
                const glm::vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
                float t;
                if (rayHitsTriangle(o, dir, a, b, c, t)) xs.push_back(o.x + t);
            }
            if (xs.size() < 2) continue;
            std::sort(xs.begin(), xs.end());

            // Collapse near-coincident crossings (a shared edge gets hit by both
            // adjacent triangles) so inside/outside parity stays correct.
            std::size_t w = 0;
            for (std::size_t r = 0; r < xs.size(); ++r) {
                if (w == 0 || xs[r] - xs[w - 1] > mergeEps) xs[w++] = xs[r];
            }
            xs.resize(w);

            for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
                const float x0 = xs[k];
                const float x1 = xs[k + 1];
                if (x1 - x0 < mergeEps) continue;
                const glm::vec3 lo(x0, bmin.y + static_cast<float>(iy)     * cell.y,
                                       bmin.z + static_cast<float>(iz)     * cell.z);
                const glm::vec3 hi(x1, bmin.y + static_cast<float>(iy + 1) * cell.y,
                                       bmin.z + static_cast<float>(iz + 1) * cell.z);
                ColliderPart box;
                box.center      = (lo + hi) * 0.5f * scale;
                box.halfExtents = glm::max((hi - lo) * 0.5f * absScale,
                                           glm::vec3(Physics::MIN_HALF_EXTENT));
                boxes.push_back(box);
            }
        }
    }

    // A non-watertight or paper-thin mesh can resolve to nothing; fall back to
    // the bounds box so the entity is never left without a collider.
    if (boxes.empty()) {
        return { boundsBox(bmin, bmax, scale) };
    }
    return boxes;
}

} // namespace Vkm::Engine

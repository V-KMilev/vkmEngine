#include "mesh_generators.h"

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <nlohmann/json.hpp>

#include "resource/asset_database.h"

namespace Engine {

namespace {
/// Build a JSON source descriptor for a procedural mesh. Stored on the
/// MeshAsset so SceneSerializer can recreate it on cold-start load.
nlohmann::json meshGeneratorSource(const char* type, nlohmann::json params = nlohmann::json::object()) {
    nlohmann::json j;
    j["kind"] = "generator";
    j["type"] = type;
    if (!params.empty()) j["params"] = std::move(params);
    return j;
}

/**
 * @brief Stamp source + AssetId on a freshly-generated mesh in one call.
 *
 * The AssetDatabase key is "mesh:generator:<type>:<param>:<param>..."
 * derived deterministically from the same params so identical generator
 * calls map to the same GUID across runs.
 */
void stampGenerated(MeshAsset& mesh, const char* type, const nlohmann::json& params = {}) {
    mesh.sourceJson() = meshGeneratorSource(type, params);
    std::string key = std::string("mesh:generator:") + type;
    if (params.is_object()) {
        for (auto it = params.begin(); it != params.end(); ++it) {
            key += ':';
            key += it.value().dump();
        }
    }
    mesh.name    = key;
    mesh.assetId = AssetDatabase::get().registerOrGet(key, AssetKind::Mesh);
}
}

MeshAsset generateTriangle(float size) {
    MeshAsset mesh;

    const glm::vec3 normal(0.0f, 1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);

    // Canonical unit triangle (size 1.0), scaled uniformly by size.
    mesh.vertices = {
        Vertex{ glm::vec3( 0.0f, 0.0f,  0.433f) * size, normal, glm::vec2(0.5f, 1.0f), tangent },  // Top
        Vertex{ glm::vec3(-0.5f, 0.0f, -0.25f) * size, normal, glm::vec2(0.0f, 0.0f), tangent },  // Bottom-left
        Vertex{ glm::vec3( 0.5f, 0.0f, -0.25f) * size, normal, glm::vec2(1.0f, 0.0f), tangent }   // Bottom-right
    };

    mesh.indices = { 0, 1, 2 };
    mesh.computeAndSetBounds();
    stampGenerated(mesh, "triangle", {{"size", size}});
    return mesh;
}

MeshAsset generatePlane(float width, float height, uint32_t widthSegments, uint32_t heightSegments) {
    MeshAsset mesh;

    const glm::vec3 normal(0.0f, 1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);

    // Tessellated grid: width x height (full dimensions, centred on origin),
    // subdivided into widthSegments x heightSegments quads. u runs along x,
    // v along z; per-cell winding matches the original single-quad plane.
    const uint32_t nx = widthSegments  > 0 ? widthSegments  : 1;
    const uint32_t nz = heightSegments > 0 ? heightSegments : 1;
    const float halfW = width  * 0.5f;
    const float halfH = height * 0.5f;

    mesh.vertices.reserve((nx + 1) * (nz + 1));
    for (uint32_t j = 0; j <= nz; ++j) {
        const float v = static_cast<float>(j) / static_cast<float>(nz);
        const float z = -halfH + v * height;
        for (uint32_t i = 0; i <= nx; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(nx);
            const float x = -halfW + u * width;
            mesh.vertices.push_back(Vertex{ glm::vec3(x, 0.0f, z), normal, glm::vec2(u, v), tangent });
        }
    }

    const uint32_t stride = nx + 1;
    mesh.indices.reserve(nx * nz * 6);
    for (uint32_t j = 0; j < nz; ++j) {
        for (uint32_t i = 0; i < nx; ++i) {
            const uint32_t v00 =  j      * stride + i;
            const uint32_t v10 =  j      * stride + (i + 1);
            const uint32_t v11 = (j + 1) * stride + (i + 1);
            const uint32_t v01 = (j + 1) * stride + i;
            mesh.indices.push_back(v00);
            mesh.indices.push_back(v10);
            mesh.indices.push_back(v11);
            mesh.indices.push_back(v11);
            mesh.indices.push_back(v01);
            mesh.indices.push_back(v00);
        }
    }

    mesh.computeAndSetBounds();
    stampGenerated(mesh, "plane", {
        {"width", width}, {"height", height},
        {"widthSegments", widthSegments}, {"heightSegments", heightSegments}
    });
    return mesh;
}

MeshAsset generateCube() {
    MeshAsset mesh;

    const glm::vec3 nFront ( 0.0f,  0.0f, -1.0f);
    const glm::vec3 nBack  ( 0.0f,  0.0f,  1.0f);
    const glm::vec3 nLeft  (-1.0f,  0.0f,  0.0f);
    const glm::vec3 nRight ( 1.0f,  0.0f,  0.0f);
    const glm::vec3 nTop   ( 0.0f,  1.0f,  0.0f);
    const glm::vec3 nBottom( 0.0f, -1.0f,  0.0f);

    const glm::vec4 tRight   ( 1.0f,  0.0f,  0.0f, 1.0f);
    const glm::vec4 tLeft    (-1.0f,  0.0f,  0.0f, 1.0f);
    const glm::vec4 tForward ( 0.0f,  0.0f,  1.0f, 1.0f);
    const glm::vec4 tBack    ( 0.0f,  0.0f, -1.0f, 1.0f);

    mesh.vertices = {
        // Front (-Z)
        { {-0.5f, -0.5f, -0.5f}, nFront,  {0, 0}, tRight },
        { { 0.5f, -0.5f, -0.5f}, nFront,  {1, 0}, tRight },
        { { 0.5f,  0.5f, -0.5f}, nFront,  {1, 1}, tRight },
        { {-0.5f,  0.5f, -0.5f}, nFront,  {0, 1}, tRight },

        // Back (+Z)
        { { 0.5f, -0.5f,  0.5f}, nBack,   {0, 0}, tLeft },
        { {-0.5f, -0.5f,  0.5f}, nBack,   {1, 0}, tLeft },
        { {-0.5f,  0.5f,  0.5f}, nBack,   {1, 1}, tLeft },
        { { 0.5f,  0.5f,  0.5f}, nBack,   {0, 1}, tLeft },

        // Left (-X)
        { {-0.5f, -0.5f,  0.5f}, nLeft,   {0, 0}, tBack },
        { {-0.5f, -0.5f, -0.5f}, nLeft,   {1, 0}, tBack },
        { {-0.5f,  0.5f, -0.5f}, nLeft,   {1, 1}, tBack },
        { {-0.5f,  0.5f,  0.5f}, nLeft,   {0, 1}, tBack },

        // Right (+X)
        { { 0.5f, -0.5f, -0.5f}, nRight,  {0, 0}, tForward },
        { { 0.5f, -0.5f,  0.5f}, nRight,  {1, 0}, tForward },
        { { 0.5f,  0.5f,  0.5f}, nRight,  {1, 1}, tForward },
        { { 0.5f,  0.5f, -0.5f}, nRight,  {0, 1}, tForward },

        // Top (+Y)
        { {-0.5f,  0.5f, -0.5f}, nTop,    {0, 0}, tRight },
        { { 0.5f,  0.5f, -0.5f}, nTop,    {1, 0}, tRight },
        { { 0.5f,  0.5f,  0.5f}, nTop,    {1, 1}, tRight },
        { {-0.5f,  0.5f,  0.5f}, nTop,    {0, 1}, tRight },

        // Bottom (-Y)
        { {-0.5f, -0.5f,  0.5f}, nBottom, {0, 0}, tRight },
        { { 0.5f, -0.5f,  0.5f}, nBottom, {1, 0}, tRight },
        { { 0.5f, -0.5f, -0.5f}, nBottom, {1, 1}, tRight },
        { {-0.5f, -0.5f, -0.5f}, nBottom, {0, 1}, tRight }
    };

    // CCW winding for all faces (outside view)
    mesh.indices = {
        0, 2, 1,  0, 3, 2,      // Front
        4, 6, 5,  4, 7, 6,      // Back
        8,10, 9,  8,11,10,      // Left
        12,14,13, 12,15,14,    // Right
        16,18,17, 16,19,18,    // Top
        20,22,21, 20,23,22     // Bottom
    };

    mesh.computeAndSetBounds();
    stampGenerated(mesh, "cube");
    return mesh;
}

MeshAsset generateSphere(uint32_t xSegments, uint32_t ySegments) {
    MeshAsset mesh;
    const float radius = 0.5f;  // Unit sphere (-0.5 to 0.5)

    // Cube-sphere (quad-sphere): six subdivided cube faces pushed onto the
    // sphere. Unlike a lat-long sphere it has NO poles, so there is no vertex
    // collapse, no degenerate (zero) tangent -> NaN, and no texture pinching at
    // the top/bottom (the old "bald spot"). Each face maps its own [0,1] UVs.
    const uint32_t res = std::max(2u, std::max(xSegments, ySegments) / 2u);  // per-face grid

    // Emit one triangle oriented outward: flip it if its geometric normal
    // disagrees with the outward surface direction. Keeps winding correct
    // regardless of each face basis's handedness, so back-face culling holds.
    auto emitTri = [&mesh](uint32_t a, uint32_t b, uint32_t c) {
        const glm::vec3& pa = mesh.vertices[a].position;
        const glm::vec3  gn = glm::cross(mesh.vertices[b].position - pa,
                                         mesh.vertices[c].position - pa);
        if (glm::dot(gn, pa) < 0.0f) std::swap(b, c);
        mesh.indices.push_back(a);
        mesh.indices.push_back(b);
        mesh.indices.push_back(c);
    };

    const glm::vec3 faceN[6] = {
        { 1, 0, 0}, {-1, 0, 0},
        { 0, 1, 0}, { 0,-1, 0},
        { 0, 0, 1}, { 0, 0,-1},
    };

    for (int f = 0; f < 6; ++f) {
        const glm::vec3 n = faceN[f];
        // Any orthonormal in-plane basis for this face.
        const glm::vec3 helper = (std::abs(n.y) < 0.99f) ? glm::vec3(0, 1, 0)
                                                         : glm::vec3(1, 0, 0);
        const glm::vec3 uAxis = glm::normalize(glm::cross(helper, n));
        const glm::vec3 vAxis = glm::cross(n, uAxis);

        const uint32_t base   = static_cast<uint32_t>(mesh.vertices.size());
        const uint32_t stride = res + 1;

        for (uint32_t j = 0; j <= res; ++j) {
            for (uint32_t i = 0; i <= res; ++i) {
                const float s = static_cast<float>(i) / static_cast<float>(res) * 2.0f - 1.0f;
                const float t = static_cast<float>(j) / static_cast<float>(res) * 2.0f - 1.0f;

                const glm::vec3 dir      = glm::normalize(n + uAxis * s + vAxis * t);
                const glm::vec3 position = dir * radius;
                const glm::vec3 normal   = dir;
                const glm::vec2 uv(static_cast<float>(i) / static_cast<float>(res),
                                   static_cast<float>(j) / static_cast<float>(res));

                // Tangent = face u-axis projected into the surface tangent plane.
                // uAxis is never parallel to dir (dir always keeps a +n
                // component), so this never degenerates to a zero vector.
                const glm::vec3 tDir = glm::normalize(uAxis - dir * glm::dot(uAxis, dir));
                const glm::vec4 tangent(tDir, 1.0f);

                mesh.vertices.push_back(Vertex{ position, normal, uv, tangent });
            }
        }

        for (uint32_t j = 0; j < res; ++j) {
            for (uint32_t i = 0; i < res; ++i) {
                const uint32_t i0 = base + j * stride + i;
                const uint32_t i1 = base + j * stride + (i + 1);
                const uint32_t i2 = base + (j + 1) * stride + (i + 1);
                const uint32_t i3 = base + (j + 1) * stride + i;
                emitTri(i0, i1, i2);
                emitTri(i0, i2, i3);
            }
        }
    }

    mesh.computeAndSetBounds();
    stampGenerated(mesh, "sphere", {{"xSegments", xSegments}, {"ySegments", ySegments}});
    return mesh;
}

MeshAsset generatePyramid(float baseSize, float height) {
    MeshAsset mesh;

    const float h = baseSize * 0.5f;
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);

    // Square base on the y=0 plane (edge = baseSize), apex at y = height.
    const glm::vec3 bl(-h, 0.0f, -h);
    const glm::vec3 br( h, 0.0f, -h);
    const glm::vec3 fr( h, 0.0f,  h);
    const glm::vec3 fl(-h, 0.0f,  h);
    const glm::vec3 apex(0.0f, height, 0.0f);
    const glm::vec3 nDown(0.0f, -1.0f, 0.0f);

    // Side normals derived from the actual slope so they stay correct for any
    // baseSize/height (a fixed normal would only suit one set of proportions).
    // cross(c-a, b-a) points outward for the winding used in mesh.indices below.
    auto faceNormal = [](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        return glm::normalize(glm::cross(c - a, b - a));
    };
    const glm::vec3 nBack  = faceNormal(bl, br, apex);
    const glm::vec3 nRight = faceNormal(br, fr, apex);
    const glm::vec3 nFront = faceNormal(fr, fl, apex);
    const glm::vec3 nLeft  = faceNormal(fl, bl, apex);

    mesh.vertices = {
        // Base face
        Vertex{ bl, nDown, glm::vec2(0.0f, 0.0f), tangent },
        Vertex{ br, nDown, glm::vec2(1.0f, 0.0f), tangent },
        Vertex{ fr, nDown, glm::vec2(1.0f, 1.0f), tangent },
        Vertex{ fl, nDown, glm::vec2(0.0f, 1.0f), tangent },

        // Back face
        Vertex{ bl,   nBack, glm::vec2(0.0f, 0.0f), tangent },
        Vertex{ br,   nBack, glm::vec2(1.0f, 0.0f), tangent },
        Vertex{ apex, nBack, glm::vec2(0.5f, 1.0f), tangent },

        // Right face
        Vertex{ br,   nRight, glm::vec2(0.0f, 0.0f), tangent },
        Vertex{ fr,   nRight, glm::vec2(1.0f, 0.0f), tangent },
        Vertex{ apex, nRight, glm::vec2(0.5f, 1.0f), tangent },

        // Front face
        Vertex{ fr,   nFront, glm::vec2(0.0f, 0.0f), tangent },
        Vertex{ fl,   nFront, glm::vec2(1.0f, 0.0f), tangent },
        Vertex{ apex, nFront, glm::vec2(0.5f, 1.0f), tangent },

        // Left face
        Vertex{ fl,   nLeft, glm::vec2(0.0f, 0.0f), tangent },
        Vertex{ bl,   nLeft, glm::vec2(1.0f, 0.0f), tangent },
        Vertex{ apex, nLeft, glm::vec2(0.5f, 1.0f), tangent }
    };

    mesh.indices = {
        0, 1, 2,  2, 3, 0,      // Base
        4, 5, 6,                // Back
        7, 8, 9,                // Right
        10, 11, 12,             // Front
        13, 14, 15              // Left
    };

    mesh.computeAndSetBounds();
    stampGenerated(mesh, "pyramid", {{"baseSize", baseSize}, {"height", height}});
    return mesh;
}

MeshAsset generateCone(
    float radius, float height, uint32_t segments
) {
    MeshAsset mesh;

    const float halfHeight = height * 0.5f;
    const glm::vec3 tip(0.0f, halfHeight, 0.0f);
    const glm::vec3 baseCenter(0.0f, -halfHeight, 0.0f);
    const glm::vec3 nDown(0.0f, -1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);

    // Add tip vertex
    glm::vec3 tipNormal = glm::normalize(glm::vec3(0.0f, radius, height));
    mesh.vertices.push_back(Vertex{ tip, tipNormal, glm::vec2(0.5f, 1.0f), tangent });
    uint32_t tipIndex = 0;

    // Add base circle vertices
    uint32_t baseStartIndex = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t i = 0; i < segments; ++i) {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;
        glm::vec3 position(x, -halfHeight, z);
        
        // Normal for cone side (pointing outward from center)
        glm::vec3 sideNormal = glm::normalize(glm::vec3(x, 0.0f, z));
        glm::vec3 coneNormal = glm::normalize(glm::vec3(sideNormal.x, radius / height, sideNormal.z));
        
        float u = static_cast<float>(i) / static_cast<float>(segments);
        mesh.vertices.push_back(Vertex{ position, coneNormal, glm::vec2(u, 0.0f), tangent });
    }

    // Add base center vertex
    mesh.vertices.push_back(Vertex{ baseCenter, nDown, glm::vec2(0.5f, 0.5f), tangent });
    uint32_t baseCenterIndex = static_cast<uint32_t>(mesh.vertices.size()) - 1;

    // Create cone side triangles (tip to base circle)
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t nextI = (i + 1) % segments;
        mesh.indices.push_back(tipIndex);
        mesh.indices.push_back(baseStartIndex + i);
        mesh.indices.push_back(baseStartIndex + nextI);
    }

    // Create base cap triangles
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t nextI = (i + 1) % segments;
        mesh.indices.push_back(baseCenterIndex);
        mesh.indices.push_back(baseStartIndex + nextI);
        mesh.indices.push_back(baseStartIndex + i);
    }

    mesh.computeAndSetBounds();
    stampGenerated(mesh, "cone", {
        {"radius", radius}, {"height", height}, {"segments", segments}
    });
    return mesh;
}

MeshAsset decimateMesh(const MeshAsset& src, uint32_t gridResolution) {
    if (src.vertices.empty() || src.indices.size() < 3 || gridResolution < 1) {
        return src;
    }

    // Mesh AABB from positions (don't rely on stored bounds - the source may be
    // freshly built and not have them set yet).
    glm::vec3 lo = src.vertices[0].position;
    glm::vec3 hi = lo;
    for (const Vertex& v : src.vertices) {
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    const glm::vec3 extent = glm::max(hi - lo, glm::vec3(1e-6f));
    const float res = static_cast<float>(gridResolution);

    auto cellOf = [&](const glm::vec3& p) -> uint64_t {
        const glm::vec3 c = (p - lo) / extent * res;
        const auto cx = static_cast<uint64_t>(glm::clamp(c.x, 0.0f, res - 1.0f));
        const auto cy = static_cast<uint64_t>(glm::clamp(c.y, 0.0f, res - 1.0f));
        const auto cz = static_cast<uint64_t>(glm::clamp(c.z, 0.0f, res - 1.0f));
        return (cz * gridResolution + cy) * gridResolution + cx;
    };

    // Accumulate each occupied cell's averaged attributes into one representative.
    struct Cell {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f};
        glm::vec2 uv{0.0f};
        glm::vec4 tangent{0.0f};
        uint32_t  count    = 0;
        uint32_t  outIndex = 0;
    };
    std::unordered_map<uint64_t, Cell> cells;
    cells.reserve(src.vertices.size());

    std::vector<uint64_t> vertexCell(src.vertices.size());
    for (std::size_t i = 0; i < src.vertices.size(); ++i) {
        const Vertex& v = src.vertices[i];
        const uint64_t key = cellOf(v.position);
        vertexCell[i] = key;
        Cell& cell = cells[key];
        cell.position += v.position;
        cell.normal   += v.normal;
        cell.uv       += v.uv;
        cell.tangent  += v.tangent;
        ++cell.count;
    }

    MeshAsset out;
    out.vertices.reserve(cells.size());
    for (auto& [key, cell] : cells) {
        (void)key;
        cell.outIndex = static_cast<uint32_t>(out.vertices.size());
        const float inv = 1.0f / static_cast<float>(cell.count);
        Vertex v;
        v.position = cell.position * inv;
        v.normal   = glm::length(cell.normal) > 1e-6f ? glm::normalize(cell.normal) : glm::vec3(0.0f, 1.0f, 0.0f);
        v.uv       = cell.uv * inv;
        const glm::vec3 t3 = glm::vec3(cell.tangent);
        v.tangent  = glm::vec4(glm::length(t3) > 1e-6f ? glm::normalize(t3) : glm::vec3(1.0f, 0.0f, 0.0f),
                               cell.tangent.w >= 0.0f ? 1.0f : -1.0f);
        out.vertices.push_back(v);
    }

    // Remap triangles to the representatives; drop any whose corners collapsed
    // into the same cell (now a degenerate sliver).
    out.indices.reserve(src.indices.size());
    for (std::size_t i = 0; i + 2 < src.indices.size(); i += 3) {
        const uint64_t k0 = vertexCell[src.indices[i]];
        const uint64_t k1 = vertexCell[src.indices[i + 1]];
        const uint64_t k2 = vertexCell[src.indices[i + 2]];
        if (k0 == k1 || k1 == k2 || k0 == k2) continue;
        out.indices.push_back(cells[k0].outIndex);
        out.indices.push_back(cells[k1].outIndex);
        out.indices.push_back(cells[k2].outIndex);
    }

    // If clustering collapsed the whole mesh the coarse level is useless - hand
    // back the source so the LOD simply doesn't get coarser at this level.
    if (out.indices.empty()) return src;

    out.computeAndSetBounds();
    return out;
}

MeshAsset decimateMeshTracked(const MeshAsset& base, const AssetId& baseId, uint32_t gridResolution) {
    MeshAsset out = decimateMesh(base, gridResolution);
    // Stamp the reload recipe so the level re-decimates on cold-start load.
    nlohmann::json src;
    src["kind"] = "decimate";
    src["base"] = baseId.toString();
    src["grid"] = gridResolution;
    out.sourceJson() = std::move(src);
    // Deterministic id keyed on (base, grid) so identical decimations map to
    // the same GUID across runs (mirrors stampGenerated).
    const std::string key = "mesh:decimate:" + baseId.toString() + ":"
                          + std::to_string(gridResolution);
    out.name    = key;
    out.assetId = AssetDatabase::get().registerOrGet(key, AssetKind::Mesh);
    return out;
}

} // namespace Engine


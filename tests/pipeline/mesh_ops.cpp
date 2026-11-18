// What the cook and the import do to a mesh: reorder it for the GPU, unwrap it for a lightmap and
// simplify it into levels of detail, each leaving the surface it draws as it was.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "cook/mesh_gpu_order.h"
#include "import/recipe_registration.h"
#include "io/asset/generate/mesh_generators.h"
#include "io/asset/recipe/asset_factory.h"
#include "mesh/lightmap_unwrap.h"
#include "mesh/lod_generator.h"
#include "mesh/mesh_decimate.h"
#include "mesh/xatlas_portable.h"
#include "portable_math.h"
#include "resource/resource_manager.h"

#include "harness/check.h"
#include "harness/scratch.h"

using namespace Vkm::Engine;
using Vkm::Test::countInLog;

namespace {

/**
 * @brief A vertex's every stream, rounded, so two orders of one mesh compare.
 */
using Corner = std::tuple<long, long, long, long, long, long, long, long, int, int, long, long>;

Corner cornerOf(const MeshAsset& mesh, uint32_t index) {
    const auto r = [](float v) { return std::lround(v * 1e4f); };
    const Vertex& v   = mesh.vertices[index];
    const int bone    = mesh.skin.empty() ? -1 : mesh.skin[index].bones[0];
    const int weight  = mesh.skin.empty() ? -1 : mesh.skin[index].weights[0];
    const glm::vec2 l = mesh.lightmapUVs.empty() ? glm::vec2(-1.0f) : mesh.lightmapUVs[index];
    return {r(v.position.x), r(v.position.y), r(v.position.z), r(v.normal.x), r(v.normal.y), r(v.normal.z),
            r(v.uv.x),       r(v.uv.y),       bone,            weight,        r(l.x),        r(l.y)};
}

/// The triangles as their corners, each started at its least corner so a rotation compares equal
/// and a flipped winding does not.
std::multiset<std::tuple<Corner, Corner, Corner>> trianglesOf(const MeshAsset& mesh) {
    std::multiset<std::tuple<Corner, Corner, Corner>> out;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Corner c[3] = {cornerOf(mesh, mesh.indices[i]), cornerOf(mesh, mesh.indices[i + 1]),
                       cornerOf(mesh, mesh.indices[i + 2])};
        const int first = static_cast<int>(std::min_element(c, c + 3) - c);
        out.emplace(c[first], c[(first + 1) % 3], c[(first + 2) % 3]);
    }
    return out;
}

/// A sphere with every stream filled, its triangles shuffled and one vertex nothing uses.
MeshAsset scrambled() {
    MeshAsset mesh = generateSphere({6});
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        mesh.skin.push_back(SkinVertex{{static_cast<uint16_t>(i % 7), 0, 0, 0}, {255, 0, 0, 0}});
        mesh.lightmapUVs.push_back(glm::vec2(static_cast<float>(i) / mesh.vertices.size(), 0.25f));
    }
    std::vector<uint32_t> shuffled;
    const size_t triangles = mesh.indices.size() / 3;
    for (size_t t = 0; t < triangles; ++t) {
        const size_t from = (t * 37) % triangles;
        shuffled.insert(shuffled.end(), mesh.indices.begin() + from * 3, mesh.indices.begin() + from * 3 + 3);
    }
    mesh.indices = shuffled;
    mesh.vertices.push_back(mesh.vertices[0]);
    mesh.vertices.back().position = glm::vec3(9.0f);
    mesh.skin.push_back(mesh.skin[0]);
    mesh.lightmapUVs.push_back(glm::vec2(0.0f));
    return mesh;
}

/// Texels of a @p side square covered by more than one triangle of @p uvs, by their centers.
int overlapped(const MeshAsset& mesh, int side) {
    std::vector<int> covered(static_cast<size_t>(side * side), 0);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::vec2 a = mesh.lightmapUVs[mesh.indices[i]] * float(side);
        const glm::vec2 b = mesh.lightmapUVs[mesh.indices[i + 1]] * float(side);
        const glm::vec2 c = mesh.lightmapUVs[mesh.indices[i + 2]] * float(side);
        const float area  = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (std::fabs(area) < 1e-9f) continue;
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                const glm::vec2 p(x + 0.5f, y + 0.5f);
                const float u = ((b.x - p.x) * (c.y - p.y) - (c.x - p.x) * (b.y - p.y)) / area;
                const float v = ((c.x - p.x) * (a.y - p.y) - (a.x - p.x) * (c.y - p.y)) / area;
                const float w = 1.0f - u - v;
                // Strictly inside: a center on an edge two triangles share is neither's alone.
                if (u > 1e-4f && v > 1e-4f && w > 1e-4f) ++covered[static_cast<size_t>(y * side + x)];
            }
        }
    }
    return static_cast<int>(std::count_if(covered.begin(), covered.end(), [](int n) { return n > 1; }));
}

} // namespace

TEST("a mesh reordered for the GPU draws the same triangles, its streams moved with its vertices") {
    const MeshAsset before = scrambled();
    MeshAsset mesh = before;
    optimizeMeshForGpu(mesh);
    CHECK(mesh.indices.size() == before.indices.size());
    CHECK(mesh.vertices.size() == before.vertices.size() - 1);  // the unused one dropped
    CHECK(mesh.skin.size() == mesh.vertices.size());
    CHECK(mesh.lightmapUVs.size() == mesh.vertices.size());
    CHECK(findMeshFault(mesh).empty());
    CHECK(trianglesOf(mesh) == trianglesOf(before));
    CHECK(mesh.boundsMin == before.boundsMin);
    CHECK(mesh.boundsMax == before.boundsMax);
    CHECK(mesh.indices != before.indices);
}

TEST("a mesh pass given a faulty mesh leaves it as it is and says why") {
    MeshAsset faulty = generateCube();
    faulty.indices[7] = 999;
    faulty.lightmapUVs.clear();

    MeshAsset reordered = faulty;
    optimizeMeshForGpu(reordered);
    CHECK(reordered.indices == faulty.indices);
    CHECK(reordered.vertices.size() == faulty.vertices.size());

    MeshAsset unwrapped = faulty;
    unwrapLightmap(unwrapped, "faulty");
    CHECK(unwrapped.lightmapUVs.empty());
    CHECK(unwrapped.indices == faulty.indices);

    CHECK(decimateMesh(faulty, 0.5f).vertices.empty());
    CHECK(countInLog("index 999 is past its 24 vertices") == 3);
}

TEST("an unwrapped mesh's lightmap coordinates lie in the unit square, no two triangles on one texel") {
    for (const MeshAsset& made : {generateCube(), generateSphere({8}), generateCylinder({0.5f, 2.0f, 16}),
                                  generatePlane({4.0f, 1.0f, 4, 1})}) {
        const std::string name = made.sourceJson().value("type", std::string());
        std::printf("  %s\n", name.c_str());
        MeshAsset mesh = made;
        unwrapLightmap(mesh, name);
        REQUIRE(mesh.lightmapUVs.size() == mesh.vertices.size());
        CHECK(findMeshFault(mesh).empty());
        for (const glm::vec2& uv : mesh.lightmapUVs) {
            CHECK(uv.x >= 0.0f);
            CHECK(uv.x <= 1.0f);
            CHECK(uv.y >= 0.0f);
            CHECK(uv.y <= 1.0f);
        }
        CHECK(overlapped(mesh, static_cast<int>(MeshAsset::LIGHTMAP_PACK_TEXELS)) == 0);
        // A seam splits a vertex: what it was is carried to both halves.
        CHECK(mesh.indices.size() == made.indices.size());
        std::multiset<std::tuple<long, long, long>> surface, kept;
        for (const uint32_t i : made.indices) {
            const glm::vec3 p = made.vertices[i].position;
            surface.emplace(std::lround(p.x * 1e4f), std::lround(p.y * 1e4f), std::lround(p.z * 1e4f));
        }
        for (const uint32_t i : mesh.indices) {
            const glm::vec3 p = mesh.vertices[i].position;
            kept.emplace(std::lround(p.x * 1e4f), std::lround(p.y * 1e4f), std::lround(p.z * 1e4f));
        }
        CHECK(kept == surface);
    }

    // A skinned mesh is lit live, never baked: it is left as it was.
    MeshAsset skinned = generateCube();
    skinned.skin.assign(skinned.vertices.size(), SkinVertex{{0, 0, 0, 0}, {255, 0, 0, 0}});
    unwrapLightmap(skinned, "skinned");
    CHECK(skinned.lightmapUVs.empty());
    CHECK(skinned.vertices.size() == 24);
}

TEST("the unwrap's own sine, cosine, arc cosine and arc tangent are within a float's last bit") {
    // The unwrap reads them in place of the C library's float ones, which differ between platforms in
    // that bit; held to the library's double ones rounded to float, the unwrap is as good, and the same
    // everywhere.
    const auto close = [](double ours, double exact) {
        const float rounded = static_cast<float>(exact);
        return std::fabs(static_cast<float>(ours) - rounded)
            <= std::nextafter(std::fabs(rounded), INFINITY) - std::fabs(rounded);
    };
    int wrong = 0;
    for (int i = -4000; i <= 4000; ++i) {
        // Past two turns each way, in float steps, as an unwrap's angles are.
        const double angle = static_cast<double>(static_cast<float>(i) * 0.00157f);
        wrong += !close(PortableMath::sin(angle), std::sin(angle));
        wrong += !close(PortableMath::cos(angle), std::cos(angle));
        const double c = static_cast<double>(static_cast<float>(i) / 4000.0f);
        wrong += !close(PortableMath::acos(c), std::acos(c));
        for (const double x : {-1.5, -0.3, 0.0, 0.7, 2.0}) {
            wrong += !close(PortableMath::atan2(c, x), std::atan2(c, x));
        }
    }
    CHECK(wrong == 0);
}

TEST("the cook's own root is within a float's last bit of the C library's power, as sRGB mips take it") {
    // An sRGB mip's decode takes x^2.4 as x^2 times the fifth root of x^2, over the curve's range.
    const auto close = [](double ours, double exact) {
        const float rounded = static_cast<float>(exact);
        return std::fabs(static_cast<float>(ours) - rounded)
            <= std::nextafter(std::fabs(rounded), INFINITY) - std::fabs(rounded);
    };
    int wrong = 0;
    for (int i = 0; i <= 4000; ++i) {
        const double x = 0.05 + 0.95 * i / 4000.0;
        wrong += !close(PortableMath::root(x * x, 5), std::pow(x * x, 0.2));
        wrong += !close(x * x * PortableMath::root(x * x, 5), std::pow(x, 2.4));
    }
    for (const int n : {1, 2, 3, 12}) wrong += !close(PortableMath::root(0.3, n), std::pow(0.3, 1.0 / n));
    CHECK(wrong == 0);
}

TEST("the unwrap's sort puts a row in order with a compare that answers 1 or 0, as its solver's does") {
    // A C library's qsort may read the 0 as "equal" and leave a row out of order, one way on one
    // platform and another on the next.
    struct Coefficient {
        uint32_t index;
        double   value;
    };
    std::vector<Coefficient> row;
    for (const uint32_t index : {7u, 2u, 9u, 0u, 5u, 3u, 8u, 1u}) row.push_back({index, index * 0.5});
    // As the solver's: 1 when the first comes after, else 0.
    const auto after = [](const void* a, const void* b) {
        const auto& first  = *static_cast<const Coefficient*>(a);
        const auto& second = *static_cast<const Coefficient*>(b);
        return static_cast<int>(first.index > second.index);
    };
    xatlas::internal::qsort(row.data(), row.size(), sizeof(Coefficient), after);
    CHECK(std::is_sorted(row.begin(), row.end(), [](const Coefficient& a, const Coefficient& b) {
        return a.index < b.index;
    }));
    CHECK(std::all_of(row.begin(), row.end(), [](const Coefficient& c) { return c.value == c.index * 0.5; }));
}

TEST("generateLOD builds coarser named levels, each kept as a recipe that rebuilds it") {
    registerRecipeAssetFactories();
    ResourceManager resources;
    MeshAsset rock = generateSphere({16});
    const MeshHandle source = resources.add(std::move(rock), "rock");
    const size_t sourceTriangles = resources.get(source).indices.size() / 3;

    const LOD lod = generateLOD(resources, source, 2);
    REQUIRE(lod.levels.size() == 3);
    CHECK(lod.levels[0].mesh == source);
    size_t above = sourceTriangles;
    for (size_t i = 1; i < lod.levels.size(); ++i) {
        REQUIRE(resources.isAlive(lod.levels[i].mesh));
        const MeshAsset& level = resources.get(lod.levels[i].mesh);
        std::printf(
            "  level %zu: %zu triangles, handed over at %.2f\n",
            i,
            level.indices.size() / 3,
            static_cast<double>(lod.levels[i - 1].maxDistance)
        );
        CHECK(level.name() == LOD::levelName("rock", static_cast<uint32_t>(i)));
        CHECK(resources.findByName<MeshAsset>(level.name()) == lod.levels[i].mesh);
        CHECK(level.indices.size() / 3 < above);
        CHECK(level.indices.size() / 3 > 0);
        CHECK(findMeshFault(level).empty());
        // Its corners are the base's, unmoved, so it stays inside the base's box.
        CHECK(glm::all(glm::greaterThanEqual(level.boundsMin, resources.get(source).boundsMin)));
        CHECK(glm::all(glm::lessThanEqual(level.boundsMax, resources.get(source).boundsMax)));
        CHECK(lod.levels[i].maxDistance > lod.levels[i - 1].maxDistance);
        above = level.indices.size() / 3;

        // Saved as a recipe, rebuilt from it with the base alone in the graph.
        REQUIRE(level.hasSource());
        CHECK(level.sourceJson().value("kind", std::string()) == "decimate");
        ResourceManager other;
        other.add(generateSphere({16}), "rock");
        const MeshHandle rebuilt = recipeImport<MeshAsset>()(level.sourceJson(), other);
        REQUIRE(rebuilt);
        CHECK(other.get(rebuilt).indices.size() == level.indices.size());
        CHECK(other.get(rebuilt).vertices.size() == level.vertices.size());
    }
}

TEST("a mesh with nothing to simplify gains no level, and decimation never hands back a copy") {
    ResourceManager resources;
    const MeshHandle triangle = resources.add(generateTriangle(), "flat");
    CHECK(generateLOD(resources, triangle, 3).levels.size() <= 1);
    CHECK(generateLOD(resources, MeshHandle{}, 2).levels.empty());

    // A copy would carry the source's name, and adding it would replace the source.
    MeshAsset skinned = generateCube();
    skinned.skin.assign(skinned.vertices.size(), SkinVertex{{0, 0, 0, 0}, {255, 0, 0, 0}});
    const MeshAsset sphere = generateSphere({8});
    for (const auto& [mesh, ratio] : std::vector<std::pair<MeshAsset, float>>{
             {skinned, 0.5f}, {generateTriangle(), 0.5f}, {sphere, 0.0f}, {sphere, 1.0f}, {sphere, 1.5f}}) {
        const MeshAsset out = decimateMesh(mesh, ratio);
        CHECK(out.vertices.empty());
        CHECK(out.indices.empty());
        CHECK(out.name().empty());
    }
    float error = -1.0f;
    const MeshAsset half = decimateMesh(sphere, 0.5f, &error);
    CHECK(!half.indices.empty());
    CHECK(half.indices.size() < sphere.indices.size());
    CHECK(error > 0.0f);

    CHECK_NEAR(decimateRatioFromRecipe(decimateRecipe("rock", 0.3f)), 0.3f, 1e-6f);
}

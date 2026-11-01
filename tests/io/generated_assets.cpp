// The engine's own primitives, solid textures and default materials, as a scene names them: built
// to the size they say, rebuilt the same from the recipe they carry.

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "io/asset/generate/material_generators.h"
#include "io/asset/generate/mesh_generators.h"
#include "io/asset/generate/texture_generators.h"
#include "resource/resource_manager.h"

#include "harness/check.h"
#include "harness/scratch.h"

using namespace Vkm::Engine;
using Vkm::Test::countInLog;

namespace {

struct Shape {
    const char* name;
    MeshAsset   mesh;
    glm::vec3   boundsMin;
    glm::vec3   boundsMax;
    float       volume;  ///< Of the solid it stands for; 0 for an open surface facing +Y.
};

constexpr float PI = 3.14159265f;

std::vector<Shape> shapes() {
    return {
        {"cube", generateCube(), glm::vec3(-0.5f), glm::vec3(0.5f), 1.0f},
        // A cube-sphere of radius 0.5: flat facets make it a little smaller than the ball.
        {"sphere", generateSphere({12}), glm::vec3(-0.5f), glm::vec3(0.5f), 4.0f / 3.0f * PI * 0.125f},
        {"cylinder", generateCylinder({0.5f, 2.0f, 32}), {-0.5f, -1.0f, -0.5f}, {0.5f, 1.0f, 0.5f},
         PI * 0.25f * 2.0f},
        {"cone", generateCone({1.0f, 3.0f, 32}), {-1.0f, -1.5f, -1.0f}, {1.0f, 1.5f, 1.0f}, PI * 3.0f / 3.0f},
        {"pyramid", generatePyramid({2.0f, 3.0f}), {-1.0f, 0.0f, -1.0f}, {1.0f, 3.0f, 1.0f},
         4.0f * 3.0f / 3.0f},
        {"plane", generatePlane({2.0f, 4.0f, 3, 2}), {-1.0f, 0.0f, -2.0f}, {1.0f, 0.0f, 2.0f}, 0.0f},
        {"ring", generateRing({0.5f, 1.0f, 24}), {-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f}, 0.0f},
        {"triangle", generateTriangle({2.0f}), {-1.0f, 0.0f, -0.5f}, {1.0f, 0.0f, 0.866f}, 0.0f},
    };
}

glm::vec3 corner(const MeshAsset& mesh, size_t index) { return mesh.vertices[mesh.indices[index]].position; }

/// Triangle @p i's winding as a vector along its face normal.
glm::vec3 faceOf(const MeshAsset& mesh, size_t i) {
    return glm::cross(corner(mesh, i + 1) - corner(mesh, i), corner(mesh, i + 2) - corner(mesh, i));
}

/// Signed volume the triangles enclose: positive when they wind counter-clockwise seen from outside.
float signedVolume(const MeshAsset& mesh) {
    double volume = 0.0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        volume += glm::dot(corner(mesh, i), glm::cross(corner(mesh, i + 1), corner(mesh, i + 2)));
    }
    return static_cast<float>(volume / 6.0);
}

/// Edges, by their corners' positions, that are not met once each way: a closed surface has none.
int openEdges(const MeshAsset& mesh) {
    using Point = std::tuple<long, long, long>;
    const auto point = [](const glm::vec3& p) {
        return Point{std::lround(p.x * 1e4f), std::lround(p.y * 1e4f), std::lround(p.z * 1e4f)};
    };
    std::map<std::pair<Point, Point>, int> edges;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        for (int k = 0; k < 3; ++k) {
            const Point from = point(corner(mesh, i + k));
            const Point to   = point(corner(mesh, i + (k + 1) % 3));
            if (from == to) continue;  // a pole's sliver
            ++edges[{from, to}];
            --edges[{to, from}];
        }
    }
    int open = 0;
    for (const auto& [edge, balance] : edges) open += balance != 0;
    return open;
}

/// Triangles whose winding faces away from their own corners' normals.
int turnedAway(const MeshAsset& mesh) {
    int away = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::vec3 face = faceOf(mesh, i);
        if (glm::length(face) < 1e-8f) continue;
        for (int k = 0; k < 3; ++k) away += glm::dot(face, mesh.vertices[mesh.indices[i + k]].normal) <= 0.0f;
    }
    return away;
}

bool sameGeometry(const MeshAsset& a, const MeshAsset& b) {
    if (a.indices != b.indices || a.vertices.size() != b.vertices.size()) return false;
    for (size_t i = 0; i < a.vertices.size(); ++i) {
        const Vertex& u = a.vertices[i];
        const Vertex& v = b.vertices[i];
        if (u.position != v.position || u.normal != v.normal) return false;
        if (u.uv != v.uv || u.tangent != v.tangent) return false;
    }
    return true;
}

} // namespace

TEST("every generated mesh is the size it names, closed where it is a solid and facing outward") {
    for (const Shape& shape : shapes()) {
        const MeshAsset& mesh = shape.mesh;
        std::printf("  %s\n", shape.name);
        REQUIRE(!mesh.indices.empty());
        CHECK(findMeshFault(mesh).empty());
        CHECK_NEAR(mesh.boundsMin, shape.boundsMin, 1e-3f);
        CHECK_NEAR(mesh.boundsMax, shape.boundsMax, 1e-3f);
        CHECK(turnedAway(mesh) == 0);
        for (const Vertex& v : mesh.vertices) {
            CHECK_NEAR(glm::length(v.normal), 1.0f, 1e-3f);
            CHECK_NEAR(glm::length(glm::vec3(v.tangent)), 1.0f, 1e-3f);
            CHECK_NEAR(glm::dot(v.normal, glm::vec3(v.tangent)), 0.0f, 1e-3f);
            CHECK(std::fabs(v.tangent.w) == 1.0f);
        }
        if (shape.volume > 0.0f) {
            CHECK(openEdges(mesh) == 0);
            // Facets fall inside the curve they follow: a few percent under.
            CHECK(signedVolume(mesh) > shape.volume * 0.95f);
            CHECK(signedVolume(mesh) <= shape.volume * 1.0001f);
        } else {
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                CHECK(faceOf(mesh, i).y > 0.0f);
            }
        }
    }
}

TEST("a generated mesh's recipe rebuilds the same mesh, and a solid texture's recipe the same texels") {
    const std::vector<MeshAsset> made = {
        generateSphere({5}),
        generateCylinder({0.25f, 3.0f, 7}),
        generateCone({2.0f, 0.5f, 5}),
        generateRing({0.5f, 2.0f, 9}),
        generateRing({0.0f, 1.0f, 6}),
        generatePyramid({0.5f, 4.0f}),
        generatePlane({3.0f, 1.0f, 4, 2}),
        generateTriangle({0.5f}),
        generateCube(),
    };
    ResourceManager resources;
    for (const MeshAsset& mesh : made) {
        REQUIRE(mesh.hasSource());
        const MeshHandle rebuilt = createGeneratedMesh(mesh.sourceJson(), resources);
        REQUIRE(rebuilt);
        CHECK(sameGeometry(resources.get(rebuilt), mesh));
        CHECK(resources.get(rebuilt).sourceJson() == mesh.sourceJson());
    }

    const glm::vec4 tint = {0.2f, 0.4f, 0.6f, 0.8f};
    const TextureHandle solid = createSolidColorTexture(tint, resources, TextureUsage::Color);
    const TextureAsset& made2 = resources.get(solid);
    ResourceManager other;
    const TextureHandle again = createGeneratedTexture(made2.sourceJson(), other);
    REQUIRE(again);
    CHECK(other.get(again).pixelData == made2.pixelData);
    CHECK(other.get(again).usage() == TextureUsage::Color);
    CHECK(other.get(again).name() == made2.name());
    CHECK(made2.pixelData == std::vector<uint8_t>({51, 102, 153, 204}));
}

TEST("the same generator call is one shared asset, and another parameter is another asset") {
    ResourceManager resources;
    const MeshHandle sphere = addGeneratedMesh(resources, generateSphere({8}));
    const MeshHandle again  = addGeneratedMesh(resources, generateSphere({8}));
    const MeshHandle finer  = addGeneratedMesh(resources, generateSphere({9}));
    const MeshHandle cube   = addGeneratedMesh(resources, generateCube());
    CHECK(sphere == again);
    CHECK(sphere != finer);
    CHECK(sphere != cube);
    CHECK(resources.countOfType<MeshAsset>() == 3);
    CHECK(resources.get(sphere).name() != resources.get(finer).name());
    // The name is what a scene stores, so it is the same in every session.
    ResourceManager next;
    CHECK(next.get(addGeneratedMesh(next, generateSphere({8}))).name() == resources.get(sphere).name());

    const glm::vec4     scarlet = {1.0f, 0.0f, 0.0f, 1.0f};
    const TextureHandle red     = createSolidColorTexture(scarlet, resources);
    const TextureHandle red2    = createSolidColorTexture(scarlet, resources);
    const TextureHandle srgb    = createSolidColorTexture(scarlet, resources, TextureUsage::Color);
    CHECK(red == red2);
    CHECK(red != srgb);
}

TEST("a generator recipe asking for what cannot be made builds nothing and says so") {
    ResourceManager resources;
    const auto recipe = [](const char* type, nlohmann::json params) {
        return nlohmann::json{{"kind", "generator"}, {"type", type}, {"params", std::move(params)}};
    };
    CHECK(!createGeneratedMesh(recipe("dodecahedron", nlohmann::json::object()), resources));
    // Read before anything is built: a count a file got wrong would build until memory ran out.
    CHECK(!createGeneratedMesh(recipe("cylinder", {{"segments", 100000}}), resources));
    CHECK(!createGeneratedMesh(recipe("sphere", {{"resolution", -3}}), resources));
    CHECK(!createGeneratedMesh(recipe("sphere", {{"resolution", "16"}}), resources));
    CHECK(!createGeneratedMesh(recipe("cone", {{"radius", -1.0}}), resources));
    CHECK(!createGeneratedMesh(recipe("plane", {{"width", 0.0}}), resources));
    CHECK(!createGeneratedMesh(recipe("ring", {{"innerRadius", 1.0}, {"outerRadius", 1.0}}), resources));
    CHECK(!createGeneratedMesh(recipe("ring", {{"innerRadius", -0.5}}), resources));
    CHECK(resources.countOfType<MeshAsset>() == 0);
    CHECK(countInLog("cannot be made") == 7);

    // A recipe that leaves a parameter out means the generator's default.
    const MeshHandle plain = createGeneratedMesh(recipe("sphere", nlohmann::json::object()), resources);
    REQUIRE(plain);
    CHECK(resources.get(plain).indices == generateSphere().indices);
    CHECK(!createGeneratedTexture({{"kind", "file"}, {"path", "a.png"}}, resources));
}

TEST("the default, water and sea materials and the solid maps are made once a graph, as they say") {
    ResourceManager resources;
    const MaterialHandle plain = generateDefaultMaterial(resources);
    CHECK(generateDefaultMaterial(resources) == plain);
    CHECK(resources.findByName<MaterialAsset>("material:default") == plain);
    const MaterialHandle water = generateWaterMaterial(resources);
    const MaterialHandle sea   = generateSeaMaterial(resources);
    CHECK(generateWaterMaterial(resources) == water);
    CHECK(water != plain);
    CHECK(sea != water);
    CHECK(resources.countOfType<MaterialAsset>() == 3);
    CHECK(!resources.get(plain).albedoTexture);

    const auto texels = [&](TextureHandle handle) { return resources.get(handle).pixelData; };
    CHECK(texels(generateWhiteTexture(resources)) == std::vector<uint8_t>({255, 255, 255, 255}));
    CHECK(texels(generateBlackTexture(resources)) == std::vector<uint8_t>({0, 0, 0, 255}));
    CHECK(texels(generateGrayTexture(resources)) == std::vector<uint8_t>({128, 128, 128, 255}));
    const TextureHandle flat = generateNormalTexture(resources);
    CHECK(texels(flat) == std::vector<uint8_t>({128, 128}));
    CHECK(resources.get(flat).usage() == TextureUsage::Normal);
    CHECK(generateWhiteTexture(resources) == generateWhiteTexture(resources));
}

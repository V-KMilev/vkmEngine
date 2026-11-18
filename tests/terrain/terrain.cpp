// Terrain: the asset and its file, the brushes that shape it, regridding, and finding it with a ray.

#include <cmath>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "core/frame/terrain_frame.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/scene/transform.h"
#include "io/asset/generate/mesh_generators.h"
#include "io/asset/terrain_file.h"
#include "system/terrain/terrain_brush.h"

#include "harness/check.h"
#include "harness/watch.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

/// A flat terrain @p size meters a side of @p resolution vertices, all painted with its first layer.
TerrainAsset flat(uint32_t resolution, float size) {
    TerrainAsset terrain;
    terrain.resolution = resolution;
    terrain.size       = size;
    const size_t vertices = static_cast<size_t>(resolution) * resolution;
    terrain.heights.assign(vertices, 0.0f);
    terrain.weights.assign(vertices * TerrainAsset::WEIGHTS_PER_VERTEX, 0);
    for (size_t v = 0; v < vertices; ++v) terrain.weights[v * TerrainAsset::WEIGHTS_PER_VERTEX] = 255;
    return terrain;
}

/// Grid vertex (i, j)'s index: rows along X.
size_t vertex(
    const TerrainAsset& terrain,
    int i,
    int j
) { return static_cast<size_t>(j) * terrain.resolution + i; }

uint8_t share(const TerrainAsset& terrain, int i, int j, uint32_t layer) {
    return terrain.weights[vertex(terrain, i, j) * TerrainAsset::WEIGHTS_PER_VERTEX + layer];
}

int shares(const TerrainAsset& terrain, int i, int j) {
    int sum = 0;
    for (uint32_t layer = 0; layer < TerrainAsset::WEIGHTS_PER_VERTEX; ++layer) sum += share(
        terrain,
        i,
        j,
        layer
    );
    return sum;
}

TerrainBrush::Dab dabAt(const glm::vec2& center, float radius, float hardness = 0.5f) {
    TerrainBrush::Dab dab;
    dab.center   = center;
    dab.radius   = radius;
    dab.strength = 1.0f;
    dab.hardness = hardness;
    return dab;
}

/// A terrain body for the solver, and the Terrain it draws.
EntityId addGround(Scene& scene, TerrainHandle terrain) {
    const EntityId id = scene.createEntity();
    scene.add(id, Transform{});
    scene.add(id, Terrain{terrain, true});
    Rigidbody body;
    body.motion = RigidbodyMotion::Static;
    scene.add(id, body);
    ColliderPart part;
    part.shape = ColliderShape::Terrain;
    Collider collider;
    collider.parts = {part};
    scene.add(id, collider);
    return id;
}

EntityId addCrate(Scene& scene, const glm::vec3& at) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.position = at;
    scene.add(id, pose);
    scene.add(id, Rigidbody{});
    ColliderPart part;
    part.shape       = ColliderShape::Box;
    part.halfExtents = glm::vec3(0.5f);
    Collider collider;
    collider.parts = {part};
    scene.add(id, collider);
    return id;
}

} // namespace

TEST("a terrain file keeps its heights, paint, layers and plants, and one of a size no terrain has is refused") {
    ResourceManager resources;
    const MaterialHandle grass = resources.add(MaterialAsset{}, "test:grass");
    const MaterialHandle rock  = resources.add(MaterialAsset{}, "test:rock");
    const MaterialHandle leaf  = resources.add(MaterialAsset{}, "test:leaf");
    const MeshHandle bush = resources.add(generateCube(), "test:bush");

    TerrainAsset terrain = flat(65, 50.0f);
    for (size_t v = 0; v < terrain.heights.size(); ++v) terrain.heights[v] = std::sin(
        0.37f * static_cast<float>(v)
    );
    for (size_t s = 0; s < terrain.weights.size(); ++s) terrain.weights[s] = static_cast<uint8_t>(s * 7);
    terrain.layers[0]   = grass;
    terrain.layers[2]   = rock;
    terrain.layerTiling = {3.0f, 4.0f, 5.0f, 6.0f};
    TerrainFoliage plants;
    plants.parts.push_back(
        {bush, leaf, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)), {}, {}}
    );
    plants.density.assign(65 * 65, 0);
    for (size_t v = 0; v < plants.density.size(); v += 3) plants.density[v] = 200;
    plants.perSquareMeter = 2.5f;
    plants.minScale       = 0.5f;
    plants.maxScale       = 1.5f;
    plants.maxDistance    = 40.0f;
    plants.seed           = 7;
    plants.castShadows    = true;
    terrain.foliage.push_back(plants);

    const std::filesystem::path path = scratch() / "hills.terrain";
    REQUIRE(TerrainFile::write(path, terrain, resources));
    std::vector<uint8_t> body;
    std::string error;
    REQUIRE(TerrainFile::readBody(path, body, error));
    TerrainAsset read;
    TerrainFile::Names names;
    REQUIRE(TerrainFile::decode(body.data(), body.size(), read, names, error));
    TerrainFile::resolve(read, names, resources);

    CHECK(read.resolution == 65);
    CHECK(read.size == 50.0f);
    CHECK(read.heights == terrain.heights);
    CHECK(read.weights == terrain.weights);
    CHECK(read.layers[0] == grass);
    CHECK(!read.layers[1]);
    CHECK(read.layers[2] == rock);
    CHECK(read.layerTiling == terrain.layerTiling);
    REQUIRE(read.foliage.size() == 1);
    const TerrainFoliage& kept = read.foliage[0];
    REQUIRE(kept.parts.size() == 1);
    CHECK(kept.parts[0].mesh == bush);
    CHECK(kept.parts[0].material == leaf);
    CHECK(kept.parts[0].local == plants.parts[0].local);
    CHECK(kept.density == plants.density);
    CHECK(kept.perSquareMeter == 2.5f);
    CHECK(kept.minScale == 0.5f);
    CHECK(kept.maxScale == 1.5f);
    CHECK(kept.maxDistance == 40.0f);
    CHECK(kept.seed == 7u);
    CHECK(kept.castShadows);

    // 33 and 1025 vertices a side are the bounds, and only 2^n + 1 between them.
    for (const uint32_t side : {17u, 64u, 66u, 2049u}) {
        const std::vector<uint8_t> odd = TerrainFile::encode(flat(side, 50.0f), resources);
        TerrainAsset refused = terrain;
        error.clear();
        CHECK(!TerrainFile::decode(odd.data(), odd.size(), refused, names, error));
        CHECK(!error.empty());
        CHECK(refused.heights == terrain.heights);
    }
    for (const uint32_t side : {33u, 1025u}) {
        const std::vector<uint8_t> bound = TerrainFile::encode(flat(side, 50.0f), resources);
        CHECK(TerrainFile::decode(bound.data(), bound.size(), read, names, error));
    }
    // Cut short, or with a byte too many, it is refused too.
    CHECK(!TerrainFile::decode(body.data(), body.size() - 1, read, names, error));
    body.push_back(0);
    CHECK(!TerrainFile::decode(body.data(), body.size(), read, names, error));
}

TEST("each brush raises, lowers, smooths, flattens, paints and plants under its dab, fading to its rim") {
    // 1 m between vertices; vertex (32, 32) is the middle.
    TerrainAsset terrain = flat(65, 64.0f);
    const TerrainBrush::Dab middle = dabAt({0.0f, 0.0f}, 8.0f);

    const TerrainBrush::Region raised = TerrainBrush::raise(terrain, middle, 2.0f);
    CHECK(!raised.empty());
    CHECK(raised.i0 >= 24);
    CHECK(raised.i1 <= 40);
    CHECK_NEAR(terrain.heightAt(0.0f, 0.0f), 2.0f, 1e-5f);
    CHECK(terrain.heightAt(6.0f, 0.0f) > 0.0f);
    CHECK(terrain.heightAt(6.0f, 0.0f) < 2.0f);
    CHECK(terrain.heightAt(9.0f, 0.0f) == 0.0f);
    TerrainBrush::raise(terrain, middle, -0.5f);
    CHECK_NEAR(terrain.heightAt(0.0f, 0.0f), 1.5f, 1e-5f);

    // A spike, smoothed toward its neighbors.
    TerrainBrush::raise(terrain, dabAt({20.0f, 20.0f}, 0.5f, 1.0f), 3.0f);
    const float spike = terrain.heightAt(20.0f, 20.0f);
    CHECK(spike == 3.0f);
    TerrainBrush::smooth(terrain, dabAt({20.0f, 20.0f}, 3.0f, 1.0f));
    // Each height goes to the mean of the nine round it: the spike's ninth, its neighbors' too.
    CHECK_NEAR(terrain.heightAt(20.0f, 20.0f), spike / 9.0f, 1e-5f);
    CHECK_NEAR(terrain.heightAt(21.0f, 20.0f), spike / 9.0f, 1e-5f);

    TerrainBrush::flatten(terrain, dabAt({-20.0f, -20.0f}, 6.0f), 3.0f);
    CHECK_NEAR(terrain.heightAt(-20.0f, -20.0f), 3.0f, 1e-5f);
    CHECK(terrain.heightAt(-20.0f, -10.0f) == 0.0f);

    TerrainBrush::paint(terrain, middle, 2);
    CHECK(share(terrain, 32, 32, 2) == 255);
    CHECK(share(terrain, 32, 32, 0) == 0);
    CHECK(share(terrain, 37, 32, 2) > 0);
    CHECK(share(terrain, 37, 32, 0) > 0);
    CHECK(share(terrain, 50, 32, 0) == 255);
    for (int i = 20; i <= 44; ++i) CHECK(shares(terrain, i, 32) == 255);

    terrain.foliage.emplace_back();
    TerrainBrush::grow(terrain, middle, 0, 1.0f);
    REQUIRE(terrain.foliage[0].density.size() == terrain.heights.size());
    CHECK(terrain.foliage[0].density[vertex(terrain, 32, 32)] == 255);
    CHECK(terrain.foliage[0].density[vertex(terrain, 50, 32)] == 0);
    TerrainBrush::grow(terrain, dabAt({0.0f, 0.0f}, 2.0f, 1.0f), 0, 0.0f);
    CHECK(terrain.foliage[0].density[vertex(terrain, 32, 32)] == 0);

    // A dab off the grid touches nothing.
    CHECK(TerrainBrush::raise(terrain, dabAt({200.0f, 0.0f}, 8.0f), 1.0f).empty());
}

TEST("resample regrids a terrain over the same ground, keeping its shape, its paint and its plants") {
    TerrainAsset terrain = flat(33, 32.0f);
    // A slope along X, the east half painted with layer 1 and planted.
    terrain.foliage.emplace_back();
    terrain.foliage[0].density.assign(33 * 33, 0);
    for (int j = 0; j < 33; ++j) {
        for (int i = 0; i < 33; ++i) {
            terrain.heights[vertex(terrain, i, j)] = 0.25f * static_cast<float>(i);
            if (i < 16) continue;
            terrain.weights[vertex(terrain, i, j) * 4]     = 0;
            terrain.weights[vertex(terrain, i, j) * 4 + 1] = 255;
            terrain.foliage[0].density[vertex(terrain, i, j)] = 255;
        }
    }
    TerrainAsset finer = terrain;

    REQUIRE(finer.resample(65));
    CHECK(finer.resolution == 65);
    CHECK(finer.size == 32.0f);
    CHECK(finer.holdsGrid());
    for (const float x : {-15.0f, -7.3f, 0.2f, 9.9f, 15.5f}) {
        CHECK_NEAR(finer.heightAt(x, 3.1f), terrain.heightAt(x, 3.1f), 1e-4f);
    }
    CHECK(share(finer, 50, 10, 1) == 255);
    CHECK(share(finer, 10, 10, 0) == 255);
    CHECK(finer.foliage[0].density[vertex(finer, 50, 10)] == 255);
    CHECK(finer.foliage[0].density[vertex(finer, 10, 10)] == 0);
    for (int i = 0; i < 65; ++i) CHECK(shares(finer, i, 30) == 255);

    // Back to the coarse grid: every vertex it still has is the one it had.
    REQUIRE(finer.resample(33));
    CHECK(finer.heights == terrain.heights);

    // No grid of that side: refused, nothing changed.
    for (const uint32_t side : {40u, 17u, 2049u}) CHECK(!finer.resample(side));
    CHECK(finer.resolution == 33);
    CHECK(finer.heights == terrain.heights);
}

TEST("TerrainBrush::hit finds where a ray meets the surface, and misses what it does not reach") {
    TerrainAsset terrain = flat(65, 64.0f);
    TerrainBrush::raise(terrain, dabAt({0.0f, 0.0f}, 12.0f), 4.0f);

    float t = 0.0f;
    const glm::vec3 above(5.0f, 50.0f, 5.0f);
    REQUIRE(TerrainBrush::hit(terrain, above, {0.0f, -1.0f, 0.0f}, 100.0f, t));
    CHECK_NEAR(t, 50.0f - terrain.heightAt(5.0f, 5.0f), 0.01f);

    // Low across the hill from its foot: it strikes the near slope, not the far one.
    const glm::vec3 from(-30.0f, 2.0f, 0.0f);
    const glm::vec3 across = glm::normalize(glm::vec3(1.0f, 0.02f, 0.0f));
    REQUIRE(TerrainBrush::hit(terrain, from, across, 100.0f, t));
    const glm::vec3 struck = from + across * t;
    CHECK(struck.x < 0.0f);
    CHECK_NEAR(struck.y, terrain.heightAt(struck.x, struck.z), 0.02f);

    CHECK(!TerrainBrush::hit(terrain, above, {0.0f, 1.0f, 0.0f}, 100.0f, t));
    CHECK(!TerrainBrush::hit(terrain, above, {0.0f, -1.0f, 0.0f}, 20.0f, t));
    CHECK(!TerrainBrush::hit(terrain, {100.0f, 50.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f, t));
}

TEST("a terrain whose grid is not whole is refused alike by its brushes, its regrid, its drawing and its collider") {
    TerrainAsset broken = flat(33, 32.0f);
    broken.weights.pop_back();
    broken.foliage.emplace_back();
    CHECK(!broken.holdsGrid());
    const std::vector<float> heights = broken.heights;
    CHECK(TerrainBrush::raise(broken, dabAt({0.0f, 0.0f}, 8.0f), 1.0f).empty());
    CHECK(TerrainBrush::paint(broken, dabAt({0.0f, 0.0f}, 8.0f), 1).empty());
    CHECK(TerrainBrush::grow(broken, dabAt({0.0f, 0.0f}, 8.0f), 0, 1.0f).empty());
    CHECK(broken.heights == heights);
    CHECK(!broken.resample(65));
    TerrainAsset uneven = flat(33, 32.0f);
    uneven.resolution = 34;
    CHECK(TerrainBrush::raise(uneven, dabAt({0.0f, 0.0f}, 8.0f), 1.0f).empty());

    // Its file cannot be read back.
    ResourceManager resources;
    const std::vector<uint8_t> written = TerrainFile::encode(broken, resources);
    TerrainAsset read;
    TerrainFile::Names names;
    std::string error;
    CHECK(!TerrainFile::decode(written.data(), written.size(), read, names, error));

    // In a world: a sound terrain is drawn and stood on; the broken one is neither.
    Game game;
    Scene& scene = game.scene();
    const EntityId good = addGround(scene, game.resources().add(flat(33, 32.0f), "test:good"));
    const EntityId bad  = addGround(scene, game.resources().add(std::move(broken), "test:broken"));
    scene.get<Transform>(bad).position = {100.0f, 0.0f, 0.0f};
    const EntityId onGood = addCrate(scene, {0.0f, 2.0f, 0.0f});
    const EntityId onBad  = addCrate(scene, {100.0f, 2.0f, 0.0f});
    Watch& seen = watch(game);
    game.run(60);

    std::vector<EntityId> drawn;
    for (const TerrainChunk& chunk : seen.frame->terrain->chunks) drawn.push_back(chunk.entity);
    CHECK(drawn == std::vector<EntityId>({good}));
    CHECK_NEAR(scene.get<Transform>(onGood).position.y, 0.5f, 0.02f);
    CHECK(scene.get<Transform>(onBad).position.y < -1.0f);
}

TEST("a terrain grows its plants on its surface where their density is painted, as many as it asks, the same every time") {
    // 1 m squares; the east half painted full at two plants a square meter, on a hill.
    auto grow = [](std::vector<glm::mat4>& out) {
        Game game;
        TerrainAsset terrain = flat(65, 64.0f);
        TerrainBrush::raise(terrain, dabAt({10.0f, 0.0f}, 12.0f), 3.0f);
        TerrainFoliage grass;
        grass.parts.push_back({
            game.resources().add(generateCube(), "test:tuft"),
            game.resources().add(MaterialAsset{}, "test:green"),
            glm::mat4(1.0f),
            {},
            {},
        });
        grass.density.assign(65 * 65, 0);
        for (int j = 0; j < 65; ++j) {
            for (int i = 32; i < 65; ++i) grass.density[vertex(terrain, i, j)] = 255;
        }
        grass.perSquareMeter = 2.0f;
        grass.minScale       = 0.5f;
        grass.maxScale       = 1.5f;
        grass.seed           = 3;
        terrain.foliage.push_back(grass);
        const TerrainAsset kept = terrain;
        addGround(game.scene(), game.resources().add(std::move(terrain), "test:meadow"));
        Watch& seen = watch(game);
        game.run(2);
        for (const TerrainPlantDraw& plant : seen.frame->terrain->plants) out.push_back(plant.model);
        return kept;
    };
    std::vector<glm::mat4> first, second;
    const TerrainAsset terrain = grow(first);
    grow(second);

    // 32 columns of 64 squares painted at every corner hold two each; the column beside them, half
    // painted, one.
    CHECK(first.size() == 32u * 64u * 2u + 64u);
    bool painted = true, onGround = true, sized = true;
    for (const glm::mat4& model : first) {
        const glm::vec3 at(model[3]);
        const float size = glm::length(glm::vec3(model[0]));
        painted  = painted && at.x >= -1.0f;
        onGround = onGround && std::fabs(at.y - terrain.heightAt(at.x, at.z)) < 1e-4f;
        sized    = sized && size >= 0.5f - 1e-4f && size <= 1.5f + 1e-4f;
    }
    CHECK(painted);
    CHECK(onGround);
    CHECK(sized);
    CHECK(first == second);
}

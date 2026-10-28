#include "support.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "ecs/component/core/transform.h"
#include "ecs/component/render/terrain.h"
#include "ecs/scene.h"
#include "resource/asset/terrain_asset.h"
#include "resource/generate/mesh_generators.h"
#include "resource/resource_manager.h"
#include "system/terrain/terrain_brush.h"
#include "system/terrain/terrain_chunks.h"
#include "system/terrain/terrain_frame.h"
#include "system/terrain/terrain_plants.h"
#include "system/terrain/terrain_system.h"

namespace {

// A flat terrain of @p resolution vertices a side over @p size metres, all its first layer.
TerrainAsset flatTerrain(uint32_t resolution, float size) {
    TerrainAsset terrain;
    terrain.resolution = resolution;
    terrain.size       = size;
    const size_t vertices = static_cast<size_t>(resolution) * resolution;
    terrain.heights.assign(vertices, 0.0f);
    terrain.weights.assign(vertices * 4, 0);
    for (size_t v = 0; v < vertices; ++v) terrain.weights[v * 4] = 255;
    return terrain;
}

float heightOf(const TerrainAsset& terrain, uint32_t i, uint32_t j) {
    return terrain.heights[static_cast<size_t>(j) * terrain.resolution + i];
}

// A chunk spans its squares and hangs a skirt only where another chunk meets it, and the levels
// thin it as they go.
void testAChunkIsItsSquaresAndItsSkirts() {
    std::printf("A terrain's chunks:\n");
    const TerrainAsset terrain = flatTerrain(65, 64.0f);
    check("a grid of 64 squares a side is cut into two chunks a side", TerrainChunks::perSide(terrain) == 2);

    const uint32_t  side   = TerrainChunks::CELLS + 1;
    const MeshAsset corner = TerrainChunks::build(terrain, 0, 0, 0);
    // The corner chunk meets others on two of its rims, each of which it hangs again dropped.
    check("a corner chunk is its squares and two skirts", corner.vertices.size() == side * side + 2 * side);
    const auto surface = corner.vertices.begin();
    const bool facing  = std::all_of(surface, surface + side * side, [](const Vertex& v) {
        return v.position.y == 0.0f && v.normal.y > 0.999f;
    });
    check("  its surface where the grid is, facing up", facing);
    check("  its skirt hanging below it", corner.bounds().min.y < 0.0f);

    // A rim that climbs 16 m along its length: a coarser neighbour's edge can stray from it by as
    // much, so the skirt hangs that far below the rim's foot.
    TerrainAsset slope = flatTerrain(65, 64.0f);
    for (uint32_t j = 0; j < 65; ++j) {
        for (uint32_t i = 0; i < 65; ++i) slope.heights[j * 65 + i] = 0.5f * static_cast<float>(i);
    }
    const MeshAsset steep = TerrainChunks::build(slope, 0, 0, 0);
    check("  as far below a climbing rim as the rim climbs", steep.bounds().min.y <= -16.0f + 1e-4f);

    const MeshAsset coarse = TerrainChunks::build(terrain, 1, 1, 2);
    const uint32_t  thin   = TerrainChunks::CELLS / 4 + 1;
    check("a coarser level takes every fourth vertex", coarse.vertices.size() == thin * thin + 2 * thin);

    const float width = 32.0f;
    bool        rises = true;
    for (float d = 0.0f; d < 1000.0f; d += 5.0f) {
        rises = rises && TerrainChunks::levelAt(d, width) <= TerrainChunks::levelAt(d + 5.0f, width);
    }
    check("the level never falls back as the eye moves away", rises);
    const bool ends = TerrainChunks::levelAt(0.0f, width) == 0
        && TerrainChunks::levelAt(1e4f, width) == TerrainChunks::LEVELS - 1;
    check("  the finest near, the coarsest far", ends);
}

// Each brush moves what is under it, by what it says, and nothing past its rim.
void testABrushMovesWhatIsUnderIt() {
    std::printf("Terrain brushes:\n");
    TerrainAsset terrain = flatTerrain(65, 64.0f);

    TerrainBrush::Dab dab;
    dab.centre   = {0.0f, 0.0f};
    dab.radius   = 4.0f;
    dab.hardness = 0.5f;
    const TerrainBrush::Region raised = TerrainBrush::raise(terrain, dab, 2.0f);
    check("a raise lifts its centre by what it asks", std::abs(heightOf(terrain, 32, 32) - 2.0f) < 1e-5f);
    check("  its hard core as far", std::abs(heightOf(terrain, 34, 32) - 2.0f) < 1e-5f);
    const float fading = heightOf(terrain, 35, 32);
    check("  past it less", fading > 0.0f && fading < 2.0f);
    check("  past its rim nothing", heightOf(terrain, 36, 32) == 0.0f && heightOf(terrain, 40, 40) == 0.0f);
    const bool square = raised.i0 == 28 && raised.i1 == 36 && raised.j0 == 28 && raised.j1 == 36;
    check("  and says what it touched", square);

    TerrainBrush::Dab whole = dab;
    whole.hardness = 1.0f;
    whole.radius   = 2.5f;
    for (int k = 0; k < 40; ++k) TerrainBrush::flatten(terrain, whole, 0.5f);
    const float level = heightOf(terrain, 32, 32);
    check("a flatten held brings what it covers to its height", std::abs(level - 0.5f) < 1e-3f);

    TerrainAsset spike = flatTerrain(65, 64.0f);
    spike.heights[static_cast<size_t>(32) * 65 + 32] = 9.0f;
    TerrainBrush::smooth(spike, whole);
    check("a smooth pulls a spike down toward its neighbours", heightOf(spike, 32, 32) < 2.0f);
    check("  and its neighbours up toward it", heightOf(spike, 33, 32) > 0.0f);

    // A light dab a frame still paints: each moves a share at least a step.
    TerrainBrush::Dab light = whole;
    light.strength = 0.01f;
    for (int k = 0; k < 400; ++k) TerrainBrush::paint(terrain, light, 2);
    const uint8_t* shares = &terrain.weights[(static_cast<size_t>(32) * 65 + 32) * 4];
    check("light strokes held long enough cover a vertex in their layer", shares[2] == 255 && shares[0] == 0);
    bool whole255 = true;
    for (size_t v = 0; v < terrain.heights.size(); ++v) {
        const uint8_t* s = &terrain.weights[v * 4];
        whole255 = whole255 && s[0] + s[1] + s[2] + s[3] == 255;
    }
    check("  and every vertex's shares still make the whole", whole255);

    terrain.foliage.emplace_back();
    TerrainBrush::grow(terrain, whole, 0, 1.0f);
    const std::vector<uint8_t>& density = terrain.foliage[0].density;
    check("a grow gives a layer with no map one", density.size() == terrain.heights.size());
    check("  full where it grew", density[static_cast<size_t>(32) * 65 + 32] == 255);

    float t = 0.0f;
    const bool met = TerrainBrush::hit(terrain, {0.3f, 20.0f, -0.4f}, {0.0f, -1.0f, 0.0f}, 100.0f, t);
    const float surface = terrain.heightAt(0.3f, -0.4f);
    check("a ray down meets the surface", met && std::abs(20.0f - t - surface) < 1e-3f);
    const glm::vec3 slant = glm::normalize(glm::vec3(1.0f, -0.4f, 0.2f));
    check("  one past its edge does not", !TerrainBrush::hit(terrain, {40.0f, 5.0f, 0.0f}, slant, 100.0f, t));
}

// Plants are a function of the terrain: the same each time, where painted, on the surface.
void testPlantsGrowWherePainted() {
    std::printf("A terrain's plants:\n");
    ResourceManager resources;
    TerrainAsset terrain = flatTerrain(65, 64.0f);
    for (uint32_t j = 0; j < 65; ++j) {
        for (uint32_t i = 0; i < 65; ++i) terrain.heights[static_cast<size_t>(j) * 65 + i] = 0.1f * i;
    }
    TerrainFoliage grass;
    grass.parts.push_back({resources.add(generateCube(), "test:blade"), {}, glm::mat4(1.0f), {}, {}});
    grass.perSquareMetre = 2.0f;
    grass.density.assign(terrain.heights.size(), 0);
    terrain.foliage.push_back(grass);

    std::vector<TerrainPlants::Plant> plants;
    TerrainPlants::scatter(terrain, plants);
    check("a layer painted nowhere grows nothing", plants.empty());

    // Painted over the -X half, vertices 0 to 32: squares 0 to 31 in full, square 32 by half.
    for (uint32_t j = 0; j < 65; ++j) {
        for (uint32_t i = 0; i <= 32; ++i) terrain.foliage[0].density[static_cast<size_t>(j) * 65 + i] = 255;
    }
    TerrainPlants::scatter(terrain, plants);
    const float expected = 2.0f * (32.0f + 0.5f) * 64.0f;
    std::printf("      %zu plants where %.0f are asked for\n", plants.size(), expected);
    const float grown = static_cast<float>(plants.size());
    check("full paint grows about what it asks a square metre", std::abs(grown - expected) < expected / 20);
    bool painted = true;
    bool standing = true;
    for (const TerrainPlants::Plant& plant : plants) {
        const glm::vec3 at(plant.local[3]);
        painted  = painted && at.x <= 1.0f;
        standing = standing && std::abs(at.y - terrain.heightAt(at.x, at.z)) < 1e-4f;
    }
    check("  only where painted", painted);
    check("  each standing on the surface", standing);

    std::vector<TerrainPlants::Plant> again;
    TerrainPlants::scatter(terrain, again);
    bool same = again.size() == plants.size();
    for (size_t p = 0; same && p < plants.size(); ++p) same = again[p].local == plants[p].local;
    check("the same terrain grows the same plants", same);

    terrain.foliage[0].seed = 9;
    TerrainPlants::scatter(terrain, again);
    check("  another seed other ones", again.empty() || again[0].local != plants[0].local);
}

// TerrainSystem draws each chunk at its level and remakes, after a stroke, only the chunks the
// stroke reached.
void testTheSystemRemakesWhatAStrokeTouched() {
    std::printf("TerrainSystem:\n");
    Scene     scene;
    TestFrame frame(scene);
    ResourceManager& resources = frame.resources;
    const TerrainHandle handle = resources.add(flatTerrain(129, 128.0f), "test:terrain");
    const EntityId      id     = scene.createEntity();
    scene.add(id, Transform{});
    scene.add(id, Terrain{handle});

    TerrainSystem system;
    system.update(frame.ctx);
    const TerrainFrame* made = frame.ctx.terrain;
    check("a terrain of four chunks a side lists sixteen", made && made->chunks.size() == 16);
    if (!made || made->chunks.size() != 16) return;
    const TerrainChunk first = made->chunks[0];
    const TerrainChunk last  = made->chunks[15];
    const uint64_t     away  = resources.get(last.levels[0]).version();

    // A stroke in the first chunk, well inside it.
    TerrainBrush::Dab dab;
    dab.centre = {-56.0f, -56.0f};
    dab.radius = 3.0f;
    TerrainBrush::raise(resources.edit(handle), dab, 1.0f);
    resources.commit(handle);
    system.update(frame.ctx);
    check("  a stroke remakes the chunk it reached", resources.get(first.levels[0]).bounds().max.y > 0.5f);
    check("  and leaves one far from it as it was", resources.get(last.levels[0]).version() == away);
    check("  each the same meshes as before", frame.ctx.terrain->chunks[0].levels[0] == first.levels[0]);

    scene.destroyEntity(id);
    system.update(frame.ctx);
    check("a terrain gone takes what was made for it", !resources.isAlive(first.levels[0]));
}

// The plants are listed again only when the terrain changes, a group a chunk, each group's box
// holding its plants.
void testPlantsAreListedInGroupsWhenTheyChange() {
    std::printf("A terrain's plants as the cull is handed them:\n");
    Scene     scene;
    TestFrame frame(scene);
    ResourceManager& resources = frame.resources;
    TerrainAsset     terrain   = flatTerrain(65, 64.0f);
    TerrainFoliage   grass;
    grass.parts.push_back({resources.add(generateCube(), "test:blade"), {}, glm::mat4(1.0f), {}, {}});
    grass.perSquareMetre = 0.5f;
    grass.density.assign(terrain.heights.size(), 255);
    terrain.foliage.push_back(grass);
    const TerrainHandle handle = resources.add(std::move(terrain), "test:terrain");
    const EntityId      id     = scene.createEntity();
    scene.add(id, Transform{});
    scene.add(id, Terrain{handle});

    TerrainSystem system;
    system.update(frame.ctx);
    const TerrainFrame& made     = *frame.ctx.terrain;
    const uint64_t      revision = made.plantsRevision;
    uint32_t covered = 0;
    bool     inside  = true;
    for (const TerrainPlantGroup& group : made.plantGroups) {
        check("  a group's plants follow the last group's", group.first == covered);
        for (uint32_t p = group.first; p < group.first + group.count; ++p) {
            const Math::AABB& box = made.plants[p].bounds;
            inside = inside && glm::all(glm::greaterThanEqual(box.min, group.bounds.min))
                && glm::all(glm::lessThanEqual(box.max, group.bounds.max));
        }
        covered += group.count;
    }
    check("every plant is in a group", !made.plants.empty() && covered == made.plants.size());
    check("  a group a chunk at most", made.plantGroups.size() <= 4);
    check("  each inside its group's box", inside);

    system.update(frame.ctx);
    check("a frame that changed nothing lists them as they were", made.plantsRevision == revision);
    scene.get<Transform>(id).position.x = 3.0f;
    system.update(frame.ctx);
    check("  a terrain moved lists them again", made.plantsRevision != revision);

    resources.edit(handle).foliage[0].castShadows = true;
    resources.commit(handle);
    system.update(frame.ctx);
    const auto casts = [](const TerrainPlantDraw& plant) { return plant.castShadows; };
    const bool all = std::all_of(made.plants.begin(), made.plants.end(), casts);
    check("a layer that casts casts", !made.plants.empty() && all);
    scene.get<Terrain>(id).castShadows = false;
    system.update(frame.ctx);
    check("  until its Terrain casts none", std::none_of(made.plants.begin(), made.plants.end(), casts));
}

// After strokes, a terrain's plants are those a fresh system grows: grown again where the heights
// or the density moved, the rest as they stood.
void testAStrokeRegrowsThePlantsAFreshSystemWould() {
    std::printf("A terrain's plants after strokes:\n");
    Scene     scene;
    TestFrame frame(scene);
    ResourceManager& resources = frame.resources;
    TerrainAsset     terrain   = flatTerrain(129, 128.0f);
    TerrainFoliage   grass;
    grass.parts.push_back({resources.add(generateCube(), "test:blade"), {}, glm::mat4(1.0f), {}, {}});
    grass.perSquareMetre = 0.3f;
    grass.seed           = 5;
    grass.density.assign(terrain.heights.size(), 0);
    for (size_t v = 0; v < grass.density.size(); v += 3) grass.density[v] = 200;
    terrain.foliage.push_back(grass);
    const TerrainHandle handle = resources.add(std::move(terrain), "test:terrain");
    const EntityId      id     = scene.createEntity();
    scene.add(id, Transform{});
    scene.add(id, Terrain{handle});
    TerrainSystem system;
    system.update(frame.ctx);

    // A raise in the first chunk, the density painted full in the last, the layers painted between.
    TerrainBrush::Dab dab;
    dab.centre = {-56.0f, -56.0f};
    dab.radius = 4.0f;
    TerrainAsset& edited = resources.edit(handle);
    TerrainBrush::raise(edited, dab, 1.0f);
    for (uint32_t j = 110; j < 120; ++j) {
        for (uint32_t i = 110; i < 120; ++i) edited.foliage[0].density[size_t(j) * 129 + i] = 255;
    }
    dab.centre = {0.0f, 0.0f};
    TerrainBrush::paint(edited, dab, 1);
    resources.commit(handle);
    system.update(frame.ctx);

    Scene     fresh;
    TestFrame freshFrame(fresh);
    const TerrainHandle copy  = freshFrame.resources.add(TerrainAsset(resources.get(handle)), "test:copy");
    const EntityId      again = fresh.createEntity();
    fresh.add(again, Transform{});
    fresh.add(again, Terrain{copy});
    TerrainSystem anew;
    anew.update(freshFrame.ctx);

    const std::vector<TerrainPlantDraw>& grown = frame.ctx.terrain->plants;
    const std::vector<TerrainPlantDraw>& want  = freshFrame.ctx.terrain->plants;
    bool same = !want.empty() && grown.size() == want.size();
    for (size_t p = 0; same && p < want.size(); ++p) same = grown[p].model == want[p].model;
    std::printf("      %zu plants, a fresh system's %zu\n", grown.size(), want.size());
    check("the plants are a fresh system's, in its order", same);
}

// A plant layer added and not yet painted holds no density at all; a stroke beside it compares and
// copies the layers it holds, and must take that one as unchanged, then as painted once it is.
void testAnUnpaintedPlantLayerSurvivesAStroke() {
    std::printf("A terrain with a plant layer never painted:\n");
    Scene     scene;
    TestFrame frame(scene);
    ResourceManager& resources = frame.resources;
    TerrainAsset     terrain   = flatTerrain(65, 64.0f);
    TerrainFoliage   grass;
    grass.parts.push_back({resources.add(generateCube(), "test:blade"), {}, glm::mat4(1.0f), {}, {}});
    grass.perSquareMetre = 0.3f;
    grass.density.assign(terrain.heights.size(), 200);
    terrain.foliage.push_back(grass);
    TerrainFoliage unpainted = grass;
    unpainted.density.clear();
    terrain.foliage.push_back(unpainted);
    const TerrainHandle handle = resources.add(std::move(terrain), "test:terrain");
    const EntityId      id     = scene.createEntity();
    scene.add(id, Transform{});
    scene.add(id, Terrain{handle});
    TerrainSystem system;
    system.update(frame.ctx);

    TerrainBrush::Dab dab;
    dab.radius = 4.0f;
    TerrainBrush::raise(resources.edit(handle), dab, 1.0f);
    resources.commit(handle);
    system.update(frame.ctx);
    check("a stroke beside it regrows without reading it", !frame.ctx.terrain->plants.empty());

    TerrainAsset& painted = resources.edit(handle);
    painted.foliage[1].density.assign(painted.heights.size(), 0);
    for (uint32_t j = 40; j < 50; ++j) {
        for (uint32_t i = 40; i < 50; ++i) painted.foliage[1].density[size_t(j) * 65 + i] = 255;
    }
    resources.commit(handle);
    system.update(frame.ctx);

    Scene     fresh;
    TestFrame freshFrame(fresh);
    const TerrainHandle copy  = freshFrame.resources.add(TerrainAsset(resources.get(handle)), "test:copy");
    const EntityId      again = fresh.createEntity();
    fresh.add(again, Transform{});
    fresh.add(again, Terrain{copy});
    TerrainSystem anew;
    anew.update(freshFrame.ctx);
    const std::vector<TerrainPlantDraw>& grown = frame.ctx.terrain->plants;
    const std::vector<TerrainPlantDraw>& want  = freshFrame.ctx.terrain->plants;
    bool same = grown.size() == want.size();
    for (size_t p = 0; same && p < want.size(); ++p) same = grown[p].model == want[p].model;
    check("  and once painted grows as a fresh system would", same);
}

// Regridding keeps the shape where both grids have a vertex, the paint summing to full and the plants
// where they were painted.
void testRegriddingKeepsTheShapeThePaintAndThePlants() {
    std::printf("A terrain regridded:\n");
    TerrainAsset terrain = flatTerrain(33, 32.0f);
    for (uint32_t j = 0; j < 33; ++j) {
        for (uint32_t i = 0; i < 33; ++i) {
            const float wave = std::sin(0.4f * static_cast<float>(i));
            terrain.heights[j * 33 + i] = wave * static_cast<float>(j) * 0.1f;
        }
    }
    TerrainBrush::Dab dab;
    dab.radius = 6.0f;
    TerrainBrush::paint(terrain, dab, 2);
    terrain.foliage.emplace_back();
    TerrainBrush::grow(terrain, dab, 0, 1.0f);
    const TerrainAsset coarse(terrain);

    check("an invalid resolution changes nothing", !terrain.resample(40) && terrain.resolution == 33);
    check("a finer grid is taken", terrain.resample(65) && terrain.resolution == 65);
    bool kept = true;
    for (uint32_t j = 0; j < 33; ++j) {
        for (uint32_t i = 0; i < 33; ++i) {
            kept = kept && std::abs(heightOf(terrain, 2 * i, 2 * j) - heightOf(coarse, i, j)) < 1e-5f;
        }
    }
    check("  every old vertex keeps its height", kept);
    bool full = true;
    for (size_t v = 0; v < terrain.heights.size(); ++v) {
        int sum = 0;
        for (size_t c = 0; c < 4; ++c) sum += terrain.weights[v * 4 + c];
        full = full && sum == 255;
    }
    check("  every vertex's shares sum to full", full);
    const size_t centre = 32 * 65 + 32;
    check("  the paint is where it was", terrain.weights[centre * 4 + 2] == 255);
    const std::vector<uint8_t>& density = terrain.foliage[0].density;
    check("  and the plants", density.size() == 65u * 65u && density[centre] == 255);

    check("back to the coarser grid", terrain.resample(33));
    bool same = true;
    for (size_t v = 0; v < coarse.heights.size(); ++v) {
        same = same && std::abs(terrain.heights[v] - coarse.heights[v]) < 1e-5f;
    }
    check("  it is the grid it was", same);
}

} // namespace

void runTerrainTests() {
    testAChunkIsItsSquaresAndItsSkirts();
    testABrushMovesWhatIsUnderIt();
    testPlantsGrowWherePainted();
    testTheSystemRemakesWhatAStrokeTouched();
    testPlantsAreListedInGroupsWhenTheyChange();
    testAStrokeRegrowsThePlantsAFreshSystemWould();
    testAnUnpaintedPlantLayerSurvivesAStroke();
    testRegriddingKeepsTheShapeThePaintAndThePlants();
}

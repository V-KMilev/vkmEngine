// The cull: what the camera sees, which level of detail it draws, what it writes of every object, and
// what it keeps of a mesh that promises never to move.

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "core/frame/terrain_frame.h"
#include "ecs/component/landscape/water.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/scene/transform.h"
#include "io/asset/generate/mesh_generators.h"
#include "system/visibility/visibility_system.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

/// A frame seen through a camera at the origin looking down -Z, its field of view the one LOD ranges
/// are set against.
struct Seen {
    Frame            frame;
    VisibilitySystem cull;
    MeshHandle       cube;
    MaterialHandle   stone;
    EntityId         eye;

    Seen() {
        frame.chrome.viewport = {0, 0, 320, 180};
        cube                  = frame.resources.add(generateCube(), "test:cube");
        stone                 = frame.resources.add(MaterialAsset{}, "test:stone");
        eye                   = frame.scene.createEntity();
        frame.scene.add(eye, Transform{});
        Camera lens;
        lens.fovY = glm::radians(60.0f);
        frame.scene.add(eye, lens);
    }

    EntityId mesh(const glm::vec3& at, bool movable = true) {
        const EntityId id = frame.scene.createEntity();
        Transform pose;
        pose.position = at;
        frame.scene.add(id, pose);
        Mesh drawn;
        drawn.mesh     = cube;
        drawn.material = stone;
        drawn.movable  = movable;
        frame.scene.add(id, drawn);
        return id;
    }

    const Visibility& look() {
        cull.update(frame.ctx);
        return *frame.ctx.visibility;
    }

    /// The object @p entity is this frame; -1 when it is none.
    static int objectOf(const Visibility& seen, EntityId entity) {
        const auto found = std::find(seen.entities.begin(), seen.entities.end(), entity);
        return found == seen.entities.end() ? -1 : static_cast<int>(found - seen.entities.begin());
    }

    static std::vector<EntityId> entitiesOf(const Visibility& seen, const std::vector<uint32_t>& list) {
        std::vector<EntityId> out;
        for (const uint32_t i : list) out.push_back(seen.entities[i]);
        return out;
    }
};

bool listed(const std::vector<uint32_t>& list, int object) {
    return object >= 0 && std::find(list.begin(), list.end(), static_cast<uint32_t>(object)) != list.end();
}

} // namespace

TEST("the camera sees what lies in its view, and every drawn mesh is in the scene's list, casters first") {
    Seen seen;
    Scene& scene = seen.frame.scene;
    const EntityId ahead  = seen.mesh({0.0f, 0.0f, -10.0f});
    const EntityId behind = seen.mesh({0.0f, 0.0f, 10.0f});
    const EntityId aside  = seen.mesh({100.0f, 0.0f, -10.0f});
    const EntityId faint  = seen.mesh({1.0f, 0.0f, -10.0f});
    scene.get<Mesh>(faint).castShadows = false;
    const EntityId hidden = seen.mesh({-1.0f, 0.0f, -10.0f});
    scene.get<Mesh>(hidden).visible = false;
    const EntityId bare = seen.mesh({-2.0f, 0.0f, -10.0f});
    scene.get<Mesh>(bare).material = {};
    // A meter's cube 2 km off covers well under the screen-size cull's few pixels.
    const EntityId speck = seen.mesh({0.0f, 0.0f, -2000.0f});
    // Seen by all but the eye: a first-person body, casting its shadow.
    const EntityId body = seen.mesh({0.0f, -1.0f, -3.0f});
    scene.get<Mesh>(body).seenBy = SeenBy::AllButEye;

    const Visibility& v = seen.look();

    REQUIRE(v.hasCamera);
    CHECK(v.cameraEntity == seen.eye);
    // In object order: the Meshes' as they were added.
    CHECK(Seen::entitiesOf(v, v.objects.visible) == std::vector<EntityId>({ahead, faint}));
    std::vector<EntityId> listedAll = Seen::entitiesOf(v, v.objects.scene);
    CHECK(v.objects.casterCount == 5);
    CHECK(listedAll.size() == 6);
    CHECK(listedAll.back() == faint);
    for (const EntityId id : {ahead, behind, aside, speck, body}) {
        CHECK(std::find(listedAll.begin(), listedAll.begin() + 5, id) != listedAll.begin() + 5);
    }
    CHECK(!listed(v.objects.scene, Seen::objectOf(v, hidden)));
    CHECK(!listed(v.objects.scene, Seen::objectOf(v, bare)));
}

TEST("a LOD draws its level by distance, fading the next one in across the band past each range") {
    Seen seen;
    Scene& scene = seen.frame.scene;
    const MeshHandle mid = seen.frame.resources.add(generateCube(), "test:mid");
    const MeshHandle far = seen.frame.resources.add(generateCube(), "test:far");
    const EntityId tree = seen.mesh({0.0f, 0.0f, -5.0f});
    LOD lod;
    lod.levels = {{seen.cube, 10.0f}, {mid, 30.0f}, {far, 60.0f}};
    scene.add(tree, lod);
    const auto at = [&](float distance) {
        scene.get<Transform>(tree).position.z = -distance;
        const Visibility& v = seen.look();
        const int i = Seen::objectOf(v, tree);
        return std::pair{v.objects.draws[i].mesh, v.objects.fades[i]};
    };

    CHECK(at(5.0f).first == seen.cube);
    CHECK(!at(5.0f).second.next);
    // Past 10 m, for 7.5% of it, the next level fades in on the samples this one gives up.
    const auto [handing, fade] = at(10.5f);
    CHECK(handing == seen.cube);
    CHECK(fade.next == mid);
    CHECK_NEAR(fade.amount, 0.5f / 0.75f, 1e-3f);
    CHECK(at(20.0f).first == mid);
    CHECK(at(50.0f).first == far);
    CHECK(at(500.0f).first == far);
    // A bias of 2 keeps each level twice as far.
    scene.get<LOD>(tree).bias = 2.0f;
    CHECK(at(18.0f).first == seen.cube);
}

TEST("a mesh that promises never to move is kept, and one moved anyway is drawn where it went within the stride, said once") {
    Seen seen;
    Scene& scene = seen.frame.scene;
    const EntityId rock  = seen.mesh({0.0f, 0.0f, -10.0f}, false);
    const EntityId other = seen.mesh({3.0f, 0.0f, -10.0f}, false);
    for (int i = 0; i < 3; ++i) seen.look();
    const int index = Seen::objectOf(seen.look(), rock);
    CHECK(seen.frame.ctx.visibility->objects.draws[index].fixed);

    scene.get<Transform>(rock).position.x = 5.0f;
    int frames     = 0;
    bool keptFirst = false;
    while (frames <= static_cast<int>(VisibilitySystem::FIXED_CHECK_STRIDE) + 1) {
        const Visibility& v = seen.look();
        ++frames;
        const float x = v.objects.models[index][3].x;
        if (frames == 1) keptFirst = x == 0.0f;
        if (x == 5.0f) break;
    }
    CHECK(keptFirst);
    CHECK(frames <= static_cast<int>(VisibilitySystem::FIXED_CHECK_STRIDE) + 1);
    CHECK(countInLog("movable off moved") == 1);

    // Another broken promise in the same world is drawn right too, and not said again.
    scene.get<Transform>(other).position.x = -3.0f;
    for (uint32_t i = 0; i <= VisibilitySystem::FIXED_CHECK_STRIDE + 1; ++i) seen.look();
    CHECK(seen.frame.ctx.visibility->objects.models[Seen::objectOf(
        *seen.frame.ctx.visibility,
        other
    )][3].x == -3.0f);
    CHECK(countInLog("movable off moved") == 1);
}

TEST("a fixed mesh given another material is drawn with it within the stride, and given another mesh or made water at once") {
    Seen seen;
    Scene& scene = seen.frame.scene;
    const MaterialHandle brass = seen.frame.resources.add(MaterialAsset{}, "test:brass");
    const MeshHandle ball = seen.frame.resources.add(generateSphere(), "test:ball");
    const EntityId rock = seen.mesh({0.0f, 0.0f, -10.0f}, false);
    for (int i = 0; i < 3; ++i) seen.look();
    const int index = Seen::objectOf(*seen.frame.ctx.visibility, rock);

    scene.get<Mesh>(rock).material = brass;
    int frames = 0;
    while (frames <= static_cast<int>(VisibilitySystem::FIXED_CHECK_STRIDE) + 1) {
        ++frames;
        if (seen.look().objects.draws[index].material == brass) break;
    }
    CHECK(frames > 1);
    CHECK(frames <= static_cast<int>(VisibilitySystem::FIXED_CHECK_STRIDE) + 1);

    // Another mesh given is another object: worked out whole, the frame it is given.
    for (int i = 0; i < 3; ++i) seen.look();
    scene.get<Mesh>(rock).mesh = ball;
    CHECK(seen.look().objects.draws[index].mesh == ball);
    for (int i = 0; i < 3; ++i) seen.look();
    scene.add(rock, Water{});
    CHECK(seen.look().objects.draws[index].water == 1);
}

TEST("every level a fixed object may draw is named before the eye picks it: an LOD's, a chunk's and a plant's") {
    Seen seen;
    Scene& scene = seen.frame.scene;
    ResourceManager& resources = seen.frame.resources;
    const MeshHandle mid = resources.add(generateCube(), "test:mid");
    const MeshHandle far = resources.add(generateCube(), "test:far");
    LOD lod;
    lod.levels = {{seen.cube, 10.0f}, {mid, 30.0f}, {far, 60.0f}};
    scene.add(seen.mesh({0.0f, 0.0f, -5.0f}, false), lod);
    // A movable one draws no bake, so has nothing to ready.
    const MeshHandle moving = resources.add(generateCube(), "test:moving");
    scene.add(seen.mesh({2.0f, 0.0f, -5.0f}), LOD{{{seen.cube, 10.0f}, {moving, 30.0f}}});

    const EntityId ground = scene.createEntity();
    TerrainFrame terrain;
    TerrainChunk chunk;
    chunk.entity = ground;
    for (size_t level = 0; level < TerrainChunks::LEVELS; ++level) {
        chunk.levels[level] = resources.add(generateCube(), "test:chunk" + std::to_string(level));
    }
    chunk.material = seen.stone;
    chunk.bounds   = {{-1.0f, -1.0f, -6.0f}, {1.0f, 1.0f, -4.0f}};
    chunk.width    = 32.0f;
    terrain.chunks.push_back(chunk);
    TerrainPlantDraw plant;
    plant.entity       = ground;
    plant.mesh         = resources.add(generateCube(), "test:bush");
    plant.levels[0]    = resources.add(generateCube(), "test:bush:lod1");
    plant.levelFrom[0] = 20.0f;
    plant.material     = seen.stone;
    plant.bounds       = {{0.5f, -1.0f, -5.5f}, {1.5f, 0.0f, -4.5f}};
    plant.maxDistance  = 50.0f;
    terrain.plants.push_back(plant);
    terrain.plantGroups.push_back({0, 1, plant.bounds, 50.0f});
    terrain.plantsRevision = 1;
    seen.frame.ctx.terrain = &terrain;

    // Near, so each draws its finest level.
    const RenderObjects& o = seen.look().objects;
    const auto names = [&](const MeshHandle& level, const MeshHandle& given) {
        return std::any_of(o.levels.begin(), o.levels.end(), [&](const ObjectLevel& listed) {
            return listed.mesh == level && listed.given == given;
        });
    };
    CHECK(names(mid, seen.cube));
    CHECK(names(far, seen.cube));
    for (size_t level = 1; level < TerrainChunks::LEVELS; ++level) {
        CHECK(names(chunk.levels[level], chunk.levels[0]));
    }
    CHECK(names(plant.levels[0], plant.mesh));
    CHECK(o.levels.size() == 2 + (TerrainChunks::LEVELS - 1) + 1);
}

TEST("the cull writes every column of each object as it stands: a mesh, a fixed mesh, a terrain chunk and a plant") {
    Seen seen;
    Scene& scene = seen.frame.scene;
    ResourceManager& resources = seen.frame.resources;
    const EntityId moving = seen.mesh({0.0f, 1.0f, -10.0f});
    scene.get<Transform>(moving).scale = {2.0f, 1.0f, 1.0f};
    const EntityId fixed = seen.mesh({3.0f, 0.0f, -10.0f}, false);

    // A terrain's chunk and its plant, as TerrainSystem publishes them.
    const EntityId ground = scene.createEntity();
    TerrainFrame terrain;
    TerrainChunk chunk;
    chunk.entity = ground;
    for (size_t level = 0; level < TerrainChunks::LEVELS; ++level) {
        chunk.levels[level] = resources.add(generateCube(), "test:chunk" + std::to_string(level));
    }
    chunk.material = resources.add(MaterialAsset{}, "test:ground");
    chunk.model    = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.0f, -10.0f));
    chunk.bounds   = {{-16.0f, -3.0f, -26.0f}, {16.0f, -1.0f, 6.0f}};
    chunk.width    = 32.0f;
    terrain.chunks.push_back(chunk);
    TerrainPlantDraw plant;
    plant.entity      = ground;
    plant.mesh        = resources.add(generateCube(), "test:bush");
    plant.material    = resources.add(MaterialAsset{}, "test:leaf");
    plant.model       = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, -1.5f, -8.0f));
    plant.bounds      = {{0.5f, -2.0f, -8.5f}, {1.5f, -1.0f, -7.5f}};
    plant.maxDistance = 50.0f;
    terrain.plants.push_back(plant);
    terrain.plantGroups.push_back({0, 1, plant.bounds, 50.0f});
    terrain.plantsRevision = 1;
    seen.frame.ctx.terrain = &terrain;

    const Visibility& v    = seen.look();
    const RenderObjects& o = v.objects;
    REQUIRE(o.models.size() == 4);
    const auto sizes = {o.bounds.size(), o.draws.size(), o.skinFirst.size(), o.keys.size(), o.shapes.size(),
                        o.bases.size(), o.fades.size(), v.entities.size()};
    for (const size_t size : sizes) CHECK(size == 4);

    for (const EntityId id : {moving, fixed}) {
        const int i = Seen::objectOf(v, id);
        REQUIRE(i >= 0);
        const glm::mat4 model = Transform::computeModelMatrix(scene.get<Transform>(id));
        const Math::AABB box  = Math::transform(model, resources.get(seen.cube).bounds());
        CHECK(o.models[i] == model);
        CHECK(o.bounds[i].min == box.min);
        CHECK(o.bounds[i].max == box.max);
        CHECK(o.draws[i] == ObjectDraw{seen.cube, seen.stone, 0, 0, false, id == fixed});
        CHECK(o.skinFirst[i] == 0u);
        CHECK(o.bases[i] == seen.cube);
        CHECK(o.shapes[i] == resources.get(seen.cube).version());
    }
    // The terrain's after the meshes: its chunk, then its plant.
    CHECK(v.entities[2] == ground);
    CHECK(v.entities[3] == ground);
    CHECK(o.models[2] == chunk.model);
    CHECK(o.bounds[2].min == chunk.bounds.min);
    CHECK(o.bounds[2].max == chunk.bounds.max);
    CHECK(o.draws[2] == ObjectDraw{chunk.levels[0], chunk.material, 0, 0, false, true});
    CHECK(o.bases[2] == chunk.levels[0]);
    CHECK(o.shapes[2] == resources.get(chunk.levels[0]).version());
    CHECK(o.models[3] == plant.model);
    CHECK(o.bounds[3].min == plant.bounds.min);
    CHECK(o.draws[3] == ObjectDraw{plant.mesh, plant.material, 0, 0, false, true});
    CHECK(o.bases[3] == plant.mesh);
    for (int i = 0; i < 4; ++i) CHECK(listed(o.visible, i));
    // Each named apart, and by the same name the next frame.
    const std::vector<uint64_t> keys = o.keys;
    CHECK(std::set<uint64_t>(keys.begin(), keys.end()).size() == 4);
    seen.look();
    CHECK(seen.frame.ctx.visibility->objects.keys == keys);
}

TEST("a changed mesh is listed as changed, a steady frame lists nothing, and a frame that compares nothing says all changed") {
    Seen seen;
    Scene& scene = seen.frame.scene;
    const EntityId crate = seen.mesh({0.0f, 0.0f, -10.0f});
    const EntityId still = seen.mesh({3.0f, 0.0f, -10.0f});
    const MaterialHandle brass = seen.frame.resources.add(MaterialAsset{}, "test:brass");
    const MeshHandle ball = seen.frame.resources.add(generateSphere(), "test:ball");

    CHECK(seen.look().objects.allChanged);
    const Visibility& steady = seen.look();
    CHECK(!steady.objects.allChanged);
    CHECK(steady.objects.changed.empty());

    const auto changedAfter = [&](auto&& change) {
        change();
        const Visibility& v = seen.look();
        return Seen::entitiesOf(v, v.objects.changed);
    };
    const std::vector<EntityId> justCrate = {crate};
    CHECK(changedAfter([&] { scene.get<Transform>(crate).position.x = 1.0f; }) == justCrate);
    CHECK(changedAfter([&] { scene.get<Transform>(crate).scale = glm::vec3(2.0f); }) == justCrate);
    CHECK(changedAfter([&] { scene.get<Mesh>(crate).material = brass; }) == justCrate);
    CHECK(changedAfter([&] { scene.get<Mesh>(crate).mesh = ball; }) == justCrate);
    CHECK(changedAfter([&] { scene.get<Mesh>(crate).castShadows = false; }) == justCrate);
    CHECK(changedAfter([] {}).empty());
    (void)still;

    // No camera for a frame: the one after compares nothing with it.
    scene.get<Camera>(seen.eye).active = false;
    CHECK(!seen.look().hasCamera);
    scene.get<Camera>(seen.eye).active = true;
    CHECK(seen.look().objects.allChanged);
    CHECK(!seen.look().objects.allChanged);
}

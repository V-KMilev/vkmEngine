// Colliders made of triangles: a mesh's, a terrain's, and the boxes Fit to Mesh makes of one; and a
// collider's parts carried to a new scale.

#include <cmath>
#include <cstddef>
#include <vector>

#include "ecs/component/landscape/terrain.h"
#include "ecs/component/physics/mesh_collider.h"
#include "io/asset/generate/mesh_generators.h"
#include "system/physics/authoring/collider_fit.h"
#include "system/terrain/terrain_brush.h"

#include "harness/check.h"
#include "harness/world.h"
#include "physics/bodies.h"

using namespace Vkm::Test;

namespace {

ColliderPart meshPart(MeshHandle mesh) {
    ColliderPart part;
    part.shape = ColliderShape::Mesh;
    part.mesh  = mesh;
    return part;
}

/// A flat terrain @p size meters a side, @p height up, under a static body as a level holds one.
EntityId addTerrain(Game& game, float height, TerrainHandle& handle) {
    TerrainAsset terrain;
    terrain.resolution = 33;
    terrain.size       = 32.0f;
    terrain.heights.assign(33 * 33, height);
    terrain.weights.assign(33 * 33 * TerrainAsset::WEIGHTS_PER_VERTEX, 0);
    handle = game.resources().add(std::move(terrain), "test:ground");

    const EntityId ground = game.scene().createEntity();
    game.scene().add(ground, Transform{});
    game.scene().add(ground, Terrain{handle});
    Rigidbody body;
    body.motion = RigidbodyMotion::Static;
    game.scene().add(ground, body);
    Collider collider;
    ColliderPart part;
    part.shape     = ColliderShape::Terrain;
    collider.parts = {part};
    game.scene().add(ground, collider);
    return ground;
}

} // namespace

TEST("a body comes to rest on a mesh collider's triangles") {
    Game game;
    Scene& scene = game.scene();
    const MeshHandle plane = addGeneratedMesh(game.resources(), generatePlane({20.0f, 20.0f, 4, 4}));
    addBody(scene, {0.0f, 0.0f, 0.0f}, meshPart(plane), RigidbodyMotion::Static);
    const EntityId crate = addCrate(scene, {0.3f, 3.0f, -0.2f});
    const EntityId ball  = addBody(scene, {-4.0f, 3.0f, 2.0f}, capsule(0.3f, 0.0f));

    game.run(180);

    CHECK_NEAR(positionOf(scene, crate), glm::vec3(0.3f, 0.5f, -0.2f), 0.01f);
    CHECK(tiltDegrees(scene.get<Transform>(crate).rotation) < 0.5f);
    CHECK_NEAR(positionOf(scene, ball).y, 0.3f, 0.01f);
}

TEST("a mesh collider takes its part's scale") {
    Game game;
    Scene& scene = game.scene();
    const MeshHandle cube = addGeneratedMesh(game.resources(), generateCube());
    ColliderPart part = meshPart(cube);
    part.meshScale = {10.0f, 2.0f, 10.0f};
    addBody(scene, {0.0f, 0.0f, 0.0f}, part, RigidbodyMotion::Static);
    const EntityId crate = addCrate(scene, {0.0f, 3.0f, 0.0f});

    game.run(120);
    CHECK_NEAR(positionOf(scene, crate).y, 1.5f, 0.01f);
}

TEST("a body asleep on a mesh collider follows it down when its triangles are built again") {
    Game game;
    Scene& scene = game.scene();
    const MeshHandle cube = addGeneratedMesh(game.resources(), generateCube());
    ColliderPart part = meshPart(cube);
    part.meshScale = {10.0f, 2.0f, 10.0f};
    const EntityId floor = addBody(scene, {0.0f, 0.0f, 0.0f}, part, RigidbodyMotion::Static);
    const EntityId crate = addCrate(scene, {0.0f, 1.52f, 0.0f});

    bool asleep = false;
    game.run(240, [&](int frame) {
        if (frame != 120) return;
        asleep = bodyOf(scene, crate).sleeping;
        // Half as tall: the same mesh, its triangles built again in place.
        scene.get<Collider>(floor).parts[0].meshScale.y = 1.0f;
    });

    CHECK(asleep);
    CHECK_NEAR(positionOf(scene, crate).y, 1.0f, 0.02f);
}

TEST("a mesh body falls through another mesh but lands on a box") {
    Game game;
    Scene& scene = game.scene();
    const MeshHandle plane = addGeneratedMesh(game.resources(), generatePlane({10.0f, 10.0f}));
    const MeshHandle cube  = addGeneratedMesh(game.resources(), generateCube());
    addBody(scene, {0.0f, 0.0f, 0.0f}, meshPart(plane), RigidbodyMotion::Static);
    addBody(scene, {0.0f, -3.5f, 0.0f}, box({5.0f, 0.5f, 5.0f}), RigidbodyMotion::Static);
    const EntityId dropped = addBody(scene, {0.0f, 2.0f, 0.0f}, meshPart(cube));

    game.run(180);

    CHECK_NEAR(positionOf(scene, dropped).y, -2.5f, 0.02f);
}

TEST("a body sunk into a triangle floor is pushed out of its front") {
    Game game;
    Scene& scene = game.scene();
    const MeshHandle plane = addGeneratedMesh(game.resources(), generatePlane({10.0f, 10.0f}));
    addBody(scene, {0.0f, 0.0f, 0.0f}, meshPart(plane), RigidbodyMotion::Static);
    const EntityId sunk = addCrate(scene, {0.0f, 0.3f, 0.0f});

    game.run(120);

    CHECK_NEAR(positionOf(scene, sunk).y, 0.5f, 0.01f);
}

TEST("a body lands on a terrain as drawn, and follows it down after an edit") {
    Game game;
    Scene& scene = game.scene();
    TerrainHandle ground;
    addTerrain(game, 2.0f, ground);
    // Asleep on the first ground by the edit: only the rebuilt triangles' revision wakes it.
    const EntityId crate = addCrate(scene, {3.0f, 2.52f, 3.0f});

    glm::vec3 restedFirst(0.0f);
    game.run(240, [&](int frame) {
        if (frame == 120) {
            restedFirst = positionOf(scene, crate);
            TerrainAsset& terrain = game.resources().edit(ground);
            TerrainBrush::Dab dab;
            dab.center   = {3.0f, 3.0f};
            dab.radius   = 6.0f;
            dab.hardness = 1.0f;
            TerrainBrush::raise(terrain, dab, -1.0f);
            game.resources().commit(ground);
        }
    });

    CHECK_NEAR(restedFirst, glm::vec3(3.0f, 2.5f, 3.0f), 0.01f);
    CHECK_NEAR(positionOf(scene, crate).y, 1.5f, 0.02f);
}

TEST("a terrain is collided from the first tick: a crate thrown at it then stops on it") {
    Game game;
    Scene& scene = game.scene();
    TerrainHandle ground;
    addTerrain(game, 2.0f, ground);
    // Two meters a tick: the first would carry it whole through a ground not there yet.
    const EntityId crate = addCrate(scene, {3.0f, 2.52f, 3.0f});
    bodyOf(scene, crate).linearVelocity = {0.0f, -120.0f, 0.0f};

    game.run(60);
    CHECK_NEAR(positionOf(scene, crate).y, 2.5f, 0.01f);
}

TEST("Fit to Mesh covers a mesh's solid with boxes inside its bounds") {
    MeshAsset pyramid = generatePyramid({2.0f, 2.0f});
    const std::vector<ColliderPart> parts = fitBoxesToMesh(pyramid, 8, glm::vec3(1.0f));
    REQUIRE(parts.size() > 4);

    float volume = 0.0f;
    for (const ColliderPart& part : parts) {
        CHECK(part.shape == ColliderShape::Box);
        const glm::vec3 low = part.center - part.halfExtents, high = part.center + part.halfExtents;
        CHECK((low.x >= -1.0001f && low.y >= -0.0001f && low.z >= -1.0001f));
        CHECK((high.x <= 1.0001f && high.y <= 2.0001f && high.z <= 1.0001f));
        volume += 8.0f * part.halfExtents.x * part.halfExtents.y * part.halfExtents.z;
    }
    // The pyramid holds 8/3 m^3; eight cells a side come within a tenth of it.
    CHECK_NEAR(volume, 8.0f / 3.0f, 0.27f);

    // The Transform's scale is baked in; one cell is the scaled bounds.
    const std::vector<ColliderPart> one = fitBoxesToMesh(pyramid, 1, glm::vec3(2.0f));
    REQUIRE(one.size() == 1);
    CHECK_NEAR(one[0].halfExtents, glm::vec3(2.0f, 2.0f, 2.0f), 1e-4f);

    // Nothing to fit still gives a box.
    MeshAsset empty;
    CHECK(fitBoxesToMesh(empty, 8, glm::vec3(1.0f)).size() == 1);
}

TEST("a collider carried to a new scale grows as its entity does, and back is the collider it was") {
    ColliderPart box;
    box.center      = {0.1f, 0.2f, -0.3f};
    box.halfExtents = {0.5f, 0.25f, 1.0f};
    ColliderPart capsule;
    capsule.shape      = ColliderShape::Capsule;
    capsule.radius     = 0.3f;
    capsule.halfHeight = 0.6f;
    ColliderPart mesh;
    mesh.shape = ColliderShape::Mesh;
    ColliderPart ground;
    ground.shape = ColliderShape::Terrain;
    const std::vector<ColliderPart> parts = {box, capsule, mesh, ground};

    // Twice as wide, three times as tall, mirrored in Z.
    const std::vector<ColliderPart> grown = rescaleParts(parts, glm::vec3(1.0f), {2.0f, 3.0f, -1.0f});
    REQUIRE(grown.size() == 4);
    CHECK_NEAR(grown[0].center, glm::vec3(0.2f, 0.6f, 0.3f), 1e-6f);
    CHECK_NEAR(grown[0].halfExtents, glm::vec3(1.0f, 0.75f, 1.0f), 1e-6f);
    // The capsule: three times as tall overall, and as round as X and Z together make it.
    CHECK_NEAR(grown[1].radius, 0.3f * std::sqrt(2.0f), 1e-6f);
    CHECK_NEAR((grown[1].halfHeight + grown[1].radius) * 2.0f, (0.6f + 0.3f) * 2.0f * 3.0f, 1e-5f);
    // The mesh mirrors with its entity, as Make Mesh Collider would build it at the new scale.
    CHECK_NEAR(grown[2].meshScale, glm::vec3(2.0f, 3.0f, -1.0f), 1e-6f);
    CHECK(grown[3].center == ground.center);

    // A uniform scale keeps a capsule's shape, and one way and back is where it started.
    const std::vector<ColliderPart> doubled = rescaleParts(parts, glm::vec3(0.5f), glm::vec3(1.0f));
    CHECK_NEAR(doubled[1].radius, 0.6f, 1e-6f);
    CHECK_NEAR(doubled[1].halfHeight, 1.2f, 1e-6f);
    const std::vector<ColliderPart> back = rescaleParts(grown, {2.0f, 3.0f, -1.0f}, glm::vec3(1.0f));
    for (size_t i = 0; i < parts.size(); ++i) {
        CHECK_NEAR(back[i].center, parts[i].center, 1e-6f);
        CHECK_NEAR(back[i].halfExtents, parts[i].halfExtents, 1e-6f);
        CHECK_NEAR(back[i].radius, parts[i].radius, 1e-6f);
        CHECK_NEAR(back[i].halfHeight, parts[i].halfHeight, 1e-5f);
        CHECK_NEAR(back[i].meshScale, parts[i].meshScale, 1e-6f);
    }

    // An axis the old scale had at zero says nothing of how big the part was, so it keeps its size.
    const std::vector<ColliderPart> flat = rescaleParts(parts, {0.0f, 1.0f, 1.0f}, glm::vec3(4.0f));
    CHECK_NEAR(flat[0].halfExtents, glm::vec3(0.5f, 1.0f, 4.0f), 1e-6f);
}

TEST("a mirrored mesh collider stays mirrored as its entity grows") {
    // As Make Mesh Collider writes one under an entity scaled -1 in Z.
    ColliderPart mesh;
    mesh.shape     = ColliderShape::Mesh;
    mesh.meshScale = {1.0f, 1.0f, -1.0f};

    const std::vector<ColliderPart> grown = rescaleParts({mesh}, {1.0f, 1.0f, -1.0f}, {2.0f, 2.0f, -2.0f});
    REQUIRE(grown.size() == 1);
    CHECK_NEAR(grown[0].meshScale, glm::vec3(2.0f, 2.0f, -2.0f), 1e-6f);
}

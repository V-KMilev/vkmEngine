#include "support.h"

#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "ecs/component/core/transform.h"
#include "ecs/component/physics/buoyancy.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/ocean.h"
#include "ecs/scene.h"
#include "resource/asset/material_asset.h"
#include "resource/generate/mesh_generators.h"
#include "resource/resource_manager.h"
#include "system/ocean/ocean_data.h"
#include "system/ocean/ocean_system.h"
#include "system/physics/physics_system.h"
#include "system/water/water_data.h"
#include "system/water/water_system.h"

namespace {

// A sheet of water @p width by @p depth, its middle at @p at, turned by @p rotation, running at
// @p flow along its +U, as an entity drawing it: a pond, a pool or, long and narrow, a river.
EntityId addWater(
    Scene& scene,
    ResourceManager& resources,
    const glm::vec3& at,
    float width,
    float depth,
    float flow = 0.0f,
    const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
    float reach = MaterialAsset{}.waterDepth
) {
    MaterialAsset water;
    water.water      = WaterKind::Body;
    water.flowSpeed  = flow;
    water.waterDepth = reach;
    const MaterialHandle look  = resources.add(std::move(water));
    const MeshHandle     sheet = resources.add(generatePlane(width, depth, 4, 4));

    const EntityId id = scene.createEntity();
    Transform placed;
    placed.position = at;
    placed.rotation = rotation;
    scene.add(id, placed);
    scene.add(id, Mesh{sheet, look});
    return id;
}

// A sea's surface @p width by @p depth, its middle at @p at: a sheet drawn with a Sea material.
void addSeaSurface(Scene& scene, ResourceManager& resources, const glm::vec3& at, float width, float depth) {
    MaterialAsset sea;
    sea.water = WaterKind::Sea;
    const EntityId id = scene.createEntity();
    Transform placed;
    placed.position = at;
    scene.add(id, placed);
    scene.add(id, Mesh{resources.add(generatePlane(width, depth, 4, 4)), resources.add(std::move(sea))});
}

// A pond is water where it lies: under its surface wet, over it dry, and nothing beside it; a sheet
// of an ordinary material is no water.
void testAPondIsWaterWhereItLies() {
    std::printf("A pond, asked about points round it:\n");

    Scene     scene;
    TestFrame frame(scene);
    const EntityId pond = addWater(scene, frame.resources, {0.0f, 2.0f, 0.0f}, 10.0f, 10.0f);
    const EntityId dry  = scene.createEntity();
    scene.add(dry, Transform{});
    const MeshHandle wide = frame.resources.add(generatePlane(50.0f, 50.0f));
    scene.add(dry, Mesh{wide, frame.resources.add(MaterialAsset{})});

    WaterSystem system;
    system.update(frame.ctx);
    const WaterData& water = *frame.ctx.water;
    const WaterSample under = water.at({1.0f, 0.5f, -2.0f});
    const WaterSample over  = water.at({1.0f, 3.0f, -2.0f});
    check("one body: the water's, not the ordinary sheet's", water.bodies().size() == 1);
    check("  a point under its surface is in it", under.found && under.wet && under.body == pond);
    check("  at its height", std::abs(under.surface - 2.0f) < 1e-4f);
    check("  and one over it is not", over.found && !over.wet);
    check("  nor is a point beside it, where no water lies", !water.at({8.0f, 0.0f, 0.0f}).found);
}

// A river runs along its strip's +U at its flow, however the strip is turned, and its surface
// climbs as the strip does.
void testARiverRunsAlongItsUVs() {
    std::printf("A river, turned and tilted:\n");

    Scene     scene;
    TestFrame frame(scene);
    // A strip whose +U, world X before it turns, is turned toward +Z and tilted up along it.
    const glm::quat turned = glm::angleAxis(glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat tilted = glm::angleAxis(glm::radians(-5.0f), glm::vec3(1.0f, 0.0f, 0.0f)) * turned;
    addWater(scene, frame.resources, {0.0f, 1.0f, 0.0f}, 20.0f, 4.0f, 3.0f, tilted);

    WaterSystem system;
    system.update(frame.ctx);
    const WaterSample low  = frame.ctx.water->at({0.0f, 0.0f, -6.0f});
    const WaterSample high = frame.ctx.water->at({0.0f, 0.0f, 6.0f});
    std::printf(
        "      flow (%.2f, %.2f, %.2f); surface %.2f m at one end, %.2f m at the other\n",
        low.flow.x,
        low.flow.y,
        low.flow.z,
        low.surface,
        high.surface
    );
    check("the water runs at its flow", low.found && std::abs(glm::length(low.flow) - 3.0f) < 0.01f);
    check("  along the strip's +U, turned with it", glm::normalize(low.flow).z > 0.99f);
    check("  and the surface climbs with the strip", high.surface - low.surface > 0.9f);
}

// Of water over water - a pool 2 m deep on a deck over a lake - a point is in the pool only as deep
// as it reaches: under the deck it is over the lake.
void testStackedWaterAnswersByDepth() {
    std::printf("A pool on a deck above a lake:\n");

    Scene     scene;
    TestFrame frame(scene);
    const glm::quat level(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 deck(0.0f, 10.0f, 0.0f);
    const float     deep = 2.0f;
    const EntityId  lake = addWater(scene, frame.resources, {0.0f, 0.0f, 0.0f}, 40.0f, 40.0f);
    const EntityId  pool = addWater(scene, frame.resources, deck, 4.0f, 4.0f, 0.0f, level, deep);

    WaterSystem system;
    system.update(frame.ctx);
    const WaterData&  water = *frame.ctx.water;
    const WaterSample under = water.at({0.0f, 7.0f, 0.0f});
    check("a point just under the pool is in the pool", water.at({0.0f, 9.5f, 0.0f}).body == pool);
    check("  one under the deck is over the lake, dry", under.body == lake && !under.wet);
    check("  and one in the lake is in the lake", water.at({0.0f, -1.0f, 0.0f}).body == lake);
}

// A lake above the sea is the water in it, however deep: the sea, though nearer, is under the land.
// Beside the lake the sea is the water.
void testALakeAboveTheSeaIsTheWaterInIt() {
    std::printf("A lake 10 m above the sea:\n");

    Scene     scene;
    TestFrame frame(scene);
    OceanData sea;
    sea.present     = true;
    frame.ctx.ocean = &sea;
    addSeaSurface(scene, frame.resources, {0.0f, 0.0f, 0.0f}, 200.0f, 200.0f);
    const EntityId lake = addWater(scene, frame.resources, {0.0f, 10.0f, 0.0f}, 20.0f, 20.0f);

    WaterSystem system;
    system.update(frame.ctx);
    const WaterSample deep   = frame.ctx.water->at({0.0f, 4.0f, 0.0f});
    const WaterSample beside = frame.ctx.water->at({40.0f, -1.0f, 0.0f});
    check("a point 6 m down in the lake is in the lake", deep.found && deep.wet && deep.body == lake);
    check("  one beside it is in the sea", beside.found && beside.wet && !beside.body);
}

// The sea is water over its surfaces, and as far past them as its waves carry: under the land far
// beyond them it is none.
void testTheSeaIsWaterOnlyOverItsSurfaces() {
    std::printf("A point far past the sea's surface:\n");

    Scene     scene;
    TestFrame frame(scene);
    OceanData sea;
    sea.present     = true;
    frame.ctx.ocean = &sea;
    addSeaSurface(scene, frame.resources, {0.0f, 0.0f, 0.0f}, 200.0f, 200.0f);

    WaterSystem system;
    system.update(frame.ctx);
    const WaterSample over   = frame.ctx.water->at({40.0f, -1.0f, 0.0f});
    const WaterSample inland = frame.ctx.water->at({400.0f, -1.0f, 0.0f});
    check("a point over the surface is in the sea", over.wet);
    check("  and one far past it, under the land, is in no water", !inland.found);
}

// A pool in a cave far below the sea hides none of it: a boat on the sea is in the sea, and only a
// point in the pool is in the pool.
void testAPoolBelowTheSeaHidesNoneOfIt() {
    std::printf("A cave pool 50 m below the sea:\n");

    Scene     scene;
    TestFrame frame(scene);
    OceanData sea;
    sea.present     = true;
    frame.ctx.ocean = &sea;
    addSeaSurface(scene, frame.resources, {0.0f, 0.0f, 0.0f}, 200.0f, 200.0f);
    const EntityId pool = addWater(scene, frame.resources, {0.0f, -50.0f, 0.0f}, 20.0f, 20.0f);

    WaterSystem system;
    system.update(frame.ctx);
    const WaterSample boat  = frame.ctx.water->at({0.0f, -0.3f, 0.0f});
    const WaterSample diver = frame.ctx.water->at({0.0f, -51.0f, 0.0f});
    check("a boat on the sea over it is in the sea", boat.found && boat.wet && !boat.body);
    check("  and a diver in the pool is in the pool", diver.wet && diver.body == pool);
}

// A body's grid follows it: moved, it is water where it now lies; given an ordinary material, or
// destroyed, it is no water.
void testABodyIsLaidAgainAsItChanges() {
    std::printf("A pond moved, given another material, then destroyed:\n");

    Scene     scene;
    TestFrame frame(scene);
    const EntityId pond = addWater(scene, frame.resources, {0.0f, 0.0f, 0.0f}, 4.0f, 4.0f);
    const EntityId pool = addWater(scene, frame.resources, {0.0f, 0.0f, 30.0f}, 4.0f, 4.0f);

    WaterSystem system;
    system.update(frame.ctx);
    const WaterData& water = *frame.ctx.water;
    scene.get<Transform>(pond).position = {10.0f, 0.0f, 0.0f};
    system.update(frame.ctx);
    check("a moved pond is water where it now lies", water.at({10.0f, -0.5f, 0.0f}).body == pond);
    check("  and not where it lay", !water.at({0.0f, -0.5f, 0.0f}).found);

    scene.get<Mesh>(pond).material = frame.resources.add(MaterialAsset{});
    system.update(frame.ctx);
    check("  given an ordinary material it is no water", !water.at({10.0f, -0.5f, 0.0f}).found);

    scene.destroyEntity(pool);
    system.update(frame.ctx);
    check("  and a destroyed pool is none either", water.bodies().empty());
}

struct Settled {
    glm::vec3 position;
    glm::vec3 velocity;
};

// Where a box of half-size @p half dropped in water settles after @p seconds, and how fast it moves
// then: water running at @p flow, the box floated or not.
Settled dropInWater(float flow, bool floats, float seconds, const glm::vec3& half = glm::vec3(0.4f)) {
    Scene     scene;
    TestFrame frame(scene);
    addWater(scene, frame.resources, {0.0f, 0.0f, 0.0f}, 400.0f, 400.0f, flow);

    const EntityId box = scene.createEntity();
    Transform at;
    at.position = {0.0f, 1.5f, 0.0f};
    scene.add(box, at);
    Rigidbody body;
    body.mass     = 20.0f;
    body.canSleep = false;
    scene.add(box, body);
    ColliderPart part;
    part.shape       = ColliderShape::Box;
    part.halfExtents = half;
    Collider collider;
    collider.parts = {part};
    scene.add(box, collider);
    if (floats) scene.add(box, Buoyancy{});

    WaterSystem   water;
    PhysicsSystem physics;
    const int ticks = int(seconds / frame.ctx.clock.getFixedStep());
    for (int tick = 0; tick < ticks; ++tick) {
        water.fixedUpdate(frame.ctx);
        physics.fixedUpdate(frame.ctx);
    }
    return {scene.get<Transform>(box).position, scene.get<Rigidbody>(box).linearVelocity};
}

// A Buoyancy body rides the water, near its surface and at rest on still water; one without sinks.
// On a river it is carried at the water's own speed.
void testAFloatRidesTheWaterAndARiverCarriesIt() {
    std::printf("A box dropped in water:\n");

    const Settled still   = dropInWater(0.0f, true, 3.0f);
    const Settled plank   = dropInWater(0.0f, true, 2.0f, {0.6f, 0.02f, 0.3f});
    const Settled sunk    = dropInWater(0.0f, false, 6.0f);
    const Settled carried = dropInWater(2.0f, true, 6.0f);
    std::printf(
        "      floated at %.2f m moving %.2f m/s, unfloated at %.2f m; on a river at %.2f m/s along it\n",
        still.position.y,
        glm::length(still.velocity),
        sunk.position.y,
        carried.velocity.x
    );
    std::printf(
        "      a thin plank, after two seconds, at %.3f m moving %.3f m/s\n",
        plank.position.y,
        glm::length(plank.velocity)
    );
    check("a floated box rides near the surface", std::abs(still.position.y) < 0.4f);
    check("  at rest on still water within three seconds", glm::length(still.velocity) < 0.05f);
    check(
        "  as a thin plank is within two, on it",
        std::abs(plank.position.y) < 0.1f && glm::length(plank.velocity) < 0.05f
    );
    check("  where one without Buoyancy sinks", sunk.position.y < -10.0f);
    check("  and a river carries it at its own speed", std::abs(carried.velocity.x - 2.0f) < 0.2f);
}

// A replayed tick floats on the sea: on a fresh frame, the replayed systems alone, in the order
// they are registered, lift a Buoyancy body deep in it.
void testAReplayedTickFloatsOnTheSea() {
    std::printf("A replayed tick over a box deep in the sea:\n");

    Scene     scene;
    TestFrame frame(scene);
    addSeaSurface(scene, frame.resources, {0.0f, 0.0f, 0.0f}, 200.0f, 200.0f);
    const EntityId sea = scene.createEntity();
    scene.add(sea, Transform{});
    scene.add(sea, Ocean{});

    const EntityId box = scene.createEntity();
    Transform at;
    at.position = {0.0f, -5.0f, 0.0f};
    scene.add(box, at);
    Rigidbody body;
    body.mass     = 20.0f;
    body.canSleep = false;
    scene.add(box, body);
    ColliderPart part;
    part.shape       = ColliderShape::Box;
    part.halfExtents = glm::vec3(0.4f);
    Collider collider;
    collider.parts = {part};
    scene.add(box, collider);
    scene.add(box, Buoyancy{});

    OceanSystem   ocean;
    WaterSystem   water;
    PhysicsSystem physics;
    System* const registered[] = {&ocean, &water, &physics};
    for (System* system : registered) {
        if (system->isReplayed()) system->fixedUpdate(frame.ctx);
    }
    check("the box rises", scene.get<Rigidbody>(box).linearVelocity.y > 0.0f);
}

} // namespace

void runWaterTests() {
    testAPondIsWaterWhereItLies();
    testARiverRunsAlongItsUVs();
    testStackedWaterAnswersByDepth();
    testALakeAboveTheSeaIsTheWaterInIt();
    testTheSeaIsWaterOnlyOverItsSurfaces();
    testAPoolBelowTheSeaHidesNoneOfIt();
    testABodyIsLaidAgainAsItChanges();
    testAFloatRidesTheWaterAndARiverCarriesIt();
    testAReplayedTickFloatsOnTheSea();
}

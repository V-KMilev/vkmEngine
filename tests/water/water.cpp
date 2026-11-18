// Water: where it is and what it does at a point, its waves, and what floats on it.

#include <cmath>
#include <string>

#include "ecs/component/landscape/water.h"
#include "ecs/component/physics/buoyancy.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/scene/transform.h"
#include "io/asset/generate/mesh_generators.h"
#include "io/scene/scene_serializer.h"
#include "system/water/water_system.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

/// A flat sheet of water @p size meters across, its middle at @p at, its vertices @p spacing apart.
EntityId addWater(
    Scene& scene,
    ResourceManager& resources,
    const glm::vec3& at,
    const glm::vec2& size,
    const Water& water,
    float spacing = 2.0f,
    const glm::quat& turn = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
) {
    const auto across = static_cast<uint32_t>(size.x / spacing);
    const auto deep   = static_cast<uint32_t>(size.y / spacing);
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.position = at;
    pose.rotation = turn;
    scene.add(id, pose);
    Mesh mesh;
    mesh.mesh = resources.add(
        generatePlane({size.x, size.y, across, deep}),
        "test:water" + std::to_string(id.slot())
    );
    scene.add(id, mesh);
    scene.add(id, water);
    return id;
}

Water pond(float depth, float flow = 0.0f) {
    Water water;
    water.depth = depth;
    water.flow  = flow;
    return water;
}

Water sea(float waves = 1.0f) {
    Water water;
    water.waves = waves;
    water.depth = 0.0f;
    return water;
}

/// How far the surface strays from its rest, the root of its mean square over a patch of open water.
float roughness(const WaterData& water, float rest = 0.0f) {
    double sum = 0.0;
    int count  = 0;
    for (int i = 0; i < 24; ++i) {
        for (int j = 0; j < 24; ++j) {
            const WaterSample s = water.at({-60.0f + i * 5.3f, 20.0f, -60.0f + j * 5.3f});
            sum += double(s.surface - rest) * (s.surface - rest);
            ++count;
        }
    }
    return float(std::sqrt(sum / count));
}

/// The scene's water as one frame builds it, under @p settings.
float seaRoughness(const WaveSettings& settings, float share = 1.0f) {
    Frame frame;
    WaterSystem system;
    frame.scene.environment().waves = settings;
    addWater(frame.scene, frame.resources, glm::vec3(0.0f), {400.0f, 400.0f}, sea(share));
    system.update(frame.ctx);
    return roughness(frame.scene.water());
}

} // namespace

TEST("WaterData::at says which water a point is in, its surface, normal and flow, for a pond and the sea alike") {
    Frame frame;
    WaterSystem system;
    Scene& scene = frame.scene;
    const EntityId open = addWater(scene, frame.resources, glm::vec3(0.0f), {200.0f, 200.0f}, sea(0.0f));
    // A pond 2 m deep on a hill 5 m up, running at 1.5 m/s; and the same pond turned a quarter round.
    const EntityId hill = addWater(
        scene,
        frame.resources,
        {50.0f, 5.0f, 0.0f},
        {10.0f, 10.0f},
        pond(2.0f, 1.5f),
        1.0f
    );
    const glm::quat quarter = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const EntityId turned   = addWater(
        scene,
        frame.resources,
        {-50.0f, 5.0f, 0.0f},
        {10.0f, 10.0f},
        pond(2.0f, 1.5f),
        1.0f,
        quarter
    );
    system.update(frame.ctx);
    const WaterData& water = scene.water();

    const WaterSample inPond = water.at({50.0f, 4.5f, 1.0f});
    CHECK(inPond.found);
    CHECK(inPond.wet);
    CHECK(inPond.body == hill);
    CHECK_NEAR(inPond.surface, 5.0f, 1e-4f);
    CHECK_NEAR(inPond.normal, glm::vec3(0.0f, 1.0f, 0.0f), 1e-4f);
    CHECK_NEAR(glm::length(inPond.flow), 1.5f, 1e-3f);
    CHECK(inPond.flow.y == 0.0f);
    // The flow runs along the mesh, so turning the pond turns it.
    CHECK_NEAR(water.at({-50.0f, 4.5f, 1.0f}).flow, quarter * inPond.flow, 1e-3f);
    CHECK(water.at({-50.0f, 4.5f, 1.0f}).body == turned);

    // Under the pond's bottom is not in it: there the sea's surface lies below, dry.
    const WaterSample underPond = water.at({50.0f, 2.0f, 1.0f});
    CHECK(underPond.found);
    CHECK(!underPond.wet);
    CHECK(underPond.body == open);
    CHECK_NEAR(underPond.surface, 0.0f, 1e-4f);

    // The sea has no bottom.
    const WaterSample deep = water.at({10.0f, -300.0f, 10.0f});
    CHECK(deep.wet);
    CHECK(deep.body == open);
    CHECK(deep.flow == glm::vec3(0.0f));
    const WaterSample above = water.at({10.0f, 3.0f, 10.0f});
    CHECK(above.found);
    CHECK(!above.wet);

    // Off every water's edge there is none.
    CHECK(!water.at({500.0f, -1.0f, 500.0f}).found);
}

TEST("waves rise with the wind, settle in shallow water, and move each water by its share") {
    WaveSettings light, strong, shallow;
    light.shape.windSpeed   = 4.0f;
    strong.shape.windSpeed  = 12.0f;
    shallow.shape.windSpeed = 12.0f;
    shallow.shape.depth     = 3.0f;

    const float gentle = seaRoughness(light);
    const float rough  = seaRoughness(strong);
    CHECK(gentle > 0.0f);
    CHECK(rough > 2.0f * gentle);
    CHECK(seaRoughness(shallow) < 0.8f * rough);
    // A sheltered harbor takes half the sea's swell; a pond none.
    const float half = seaRoughness(strong, 0.5f);
    CHECK(half > 0.3f * rough);
    CHECK(half < 0.7f * rough);
    CHECK(seaRoughness(strong, 0.0f) == 0.0f);
}

TEST("a scene with no water builds no waves, and the last water to go takes its waves along") {
    Frame frame;
    WaterSystem system;
    frame.scene.environment().waves.shape.windSpeed = 12.0f;
    const auto still = [&] {
        const WaterData& water = frame.scene.water();
        return water.bodies().empty() && !water.waves().surface.swell && water.waves().surface.time == 0.0
            && water.waves().detailDrift == glm::vec2(0.0f);
    };

    frame.tick(system, 30);
    system.update(frame.ctx);
    CHECK(still());

    const EntityId ocean = addWater(frame.scene, frame.resources, glm::vec3(0.0f), {40.0f, 40.0f}, sea());
    frame.tick(system);
    CHECK(frame.scene.water().waves().surface.swell != nullptr);
    CHECK(frame.scene.water().waves().surface.time > 0.0);

    frame.scene.destroyEntity(ocean);
    frame.tick(system);
    CHECK(still());
}

TEST("a surge raises a ridge where gameplay asks, on the water that takes waves, and is not saved") {
    Frame frame;
    WaterSystem system;
    Scene& scene = frame.scene;
    WaveSettings& waves = scene.environment().waves;
    WaveSurge& surge = waves.surges[0];
    waves.shape.windSpeed = 0.0f;
    surge.center          = {0.0f, 0.0f};
    surge.direction       = {1.0f, 0.0f};
    surge.height          = 2.0f;
    surge.halfWidth       = 3.0f;
    surge.halfLength      = 10.0f;
    addWater(scene, frame.resources, glm::vec3(0.0f), {100.0f, 100.0f}, sea(), 1.0f);
    // A pond on a roof over the crest: no share of the waves.
    addWater(scene, frame.resources, {0.0f, 20.0f, 0.0f}, {8.0f, 8.0f}, pond(1.0f), 1.0f);
    system.update(frame.ctx);
    const WaterData& water = scene.water();

    CHECK_NEAR(water.at({0.0f, 5.0f, 0.0f}).surface, 2.0f, 0.05f);
    CHECK_NEAR(water.at({10.0f, 5.0f, 0.0f}).surface, 0.0f, 0.05f);
    CHECK_NEAR(water.at({0.0f, 5.0f, 30.0f}).surface, 0.0f, 0.05f);
    CHECK_NEAR(water.at({0.0f, 20.5f, 0.0f}).surface, 20.0f, 1e-4f);
    // The wind's sea alone, which is calm here.
    CHECK_NEAR(water.waves().surface.heightAt({0.0f, 0.0f}), 2.0f, 0.05f);
    CHECK_NEAR(water.waves().surface.withoutSurges().heightAt({0.0f, 0.0f}), 0.0f, 0.05f);

    const std::string saved = SceneSerializer::saveToString(scene, frame.resources);
    Frame loaded;
    REQUIRE(SceneSerializer::loadFromString(saved, loaded.scene, loaded.resources));
    CHECK(loaded.scene.environment().waves.surges[0].height == 0.0f);
}

TEST("a buoyant body floats at its waterline and drifts with the water's flow") {
    Game game;
    Scene& scene = game.scene();
    // A river 6 m deep running at 2 m/s.
    const EntityId river = addWater(
        scene,
        game.resources(),
        glm::vec3(0.0f),
        {80.0f, 20.0f},
        pond(6.0f, 2.0f)
    );
    auto crate = [&](const glm::vec3& at, bool floats) {
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
        if (floats) scene.add(id, Buoyancy{});
        return id;
    };
    const EntityId raft  = crate({-30.0f, 2.0f, 0.0f}, true);
    const EntityId stone = crate({-30.0f, 2.0f, 5.0f}, false);

    game.run(300);

    // Lifted by 2.2 gravities wholly under, it rests 1/2.2 under: its middle 0.045 m over the surface.
    const glm::vec3 at = scene.get<Transform>(raft).position;
    CHECK_NEAR(at.y, 0.5f - 1.0f / 2.2f, 0.05f);
    CHECK(std::fabs(scene.get<Rigidbody>(raft).linearVelocity.y) < 0.05f);
    const glm::vec3 flow = scene.water().at(at).flow;
    CHECK(scene.water().at(at).body == river);
    CHECK_NEAR(
        glm::vec2(scene.get<Rigidbody>(raft).linearVelocity.x, scene.get<Rigidbody>(raft).linearVelocity.z),
        glm::vec2(flow.x, flow.z),
        0.2f
    );
    CHECK(glm::length(glm::vec2(at.x, at.z) - glm::vec2(-30.0f, 0.0f)) > 5.0f);
    CHECK(scene.get<Transform>(stone).position.y < -6.0f);
}

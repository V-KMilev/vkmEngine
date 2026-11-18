// Day and night: the procedural sky aiming and tinting the scene's key light from the sun, or the moon.

#include "base/math/rotation.h"
#include "ecs/component/light/light.h"
#include "ecs/component/scene/transform.h"
#include "ecs/hierarchy_operations.h"
#include "system/sky/sky_system.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

EntityId addSun(Scene& scene, float intensity = 1.0f) {
    const EntityId id = scene.createEntity();
    scene.add(id, Transform{});
    Light light = generateLight(LightType::Directional);
    light.intensity = intensity;
    scene.add(id, light);
    return id;
}

/// The way @p light shines, in the world.
glm::vec3 shining(Scene& scene, EntityId light) {
    return Math::computeForward(Math::worldRotationOf(HierarchyOperations::computeWorldMatrix(scene, light)));
}

} // namespace

TEST("the sun's angle aims and tints the scene's key light, and below the horizon the moon lights it") {
    Frame frame;
    Scene& scene = frame.scene;
    SkySystem sky;
    SkySettings& sun = scene.environment().sky;
    const NightSkySettings& night = scene.environment().night;
    const EntityId key = addSun(scene);
    // A second sun, of a higher slot: never the key light, so never touched.
    const EntityId other = addSun(scene, 7.0f);

    sun.sunElevation = 60.0f;
    sun.sunAzimuth   = 30.0f;
    sky.update(frame.ctx);
    const Light& light = scene.get<Light>(key);
    CHECK_NEAR(shining(scene, key), -scene.environment().sunDirection(), 1e-4f);
    CHECK(light.intensity == sun.lightIntensity);
    const glm::vec3 high = light.color;
    CHECK(scene.get<Light>(other).intensity == 7.0f);
    CHECK(scene.get<Transform>(other).rotation == glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    // Low in the sky, through more air: dimmer and redder, fading out toward the horizon.
    sun.sunElevation = 2.0f;
    sun.sunAzimuth   = -100.0f;
    sky.update(frame.ctx);
    CHECK_NEAR(shining(scene, key), -scene.environment().sunDirection(), 1e-4f);
    CHECK_NEAR(light.intensity, sun.lightIntensity * 2.0f / NightSkySettings::TWILIGHT_DEGREES, 1e-5f);
    CHECK(light.color.b / light.color.r < high.b / high.r);

    // Night: the moon, opposite, lights the scene in its own color.
    sun.sunElevation = -40.0f;
    sky.update(frame.ctx);
    CHECK_NEAR(shining(scene, key), -scene.environment().moonDirection(), 1e-4f);
    CHECK(scene.environment().moonDirection().y > 0.0f);
    CHECK(light.color == night.moonlightColor);
    CHECK(light.intensity == night.moonlightIntensity);
}

TEST("the key light under a turned parent still shines from the sun, and the sky leaves a scene that has none") {
    Frame frame;
    Scene& scene = frame.scene;
    SkySystem sky;
    const EntityId rig = scene.createEntity();
    Transform turned;
    turned.rotation = glm::angleAxis(glm::radians(70.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f)));
    scene.add(rig, turned);
    const EntityId key = addSun(scene);
    HierarchyOperations::setParent(scene, key, rig);

    sky.update(frame.ctx);
    CHECK_NEAR(shining(scene, key), -scene.environment().sunDirection(), 1e-4f);

    // The first sun switched off: the next one is the key light.
    const EntityId next = addSun(scene);
    scene.get<Light>(key).enabled = false;
    scene.environment().sky.sunAzimuth = 120.0f;
    sky.update(frame.ctx);
    CHECK_NEAR(shining(scene, next), -scene.environment().sunDirection(), 1e-4f);

    // With the procedural sky off the light is the scene's own.
    scene.environment().sky.procedural = false;
    scene.get<Light>(next).intensity = 42.0f;
    scene.environment().sky.sunAzimuth = -20.0f;
    const glm::vec3 before = shining(scene, next);
    sky.update(frame.ctx);
    CHECK(scene.get<Light>(next).intensity == 42.0f);
    CHECK(shining(scene, next) == before);
}

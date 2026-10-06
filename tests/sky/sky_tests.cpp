#include "support.h"

#include "core/math/rotation.h"
#include "ecs/component/render/light.h"
#include "ecs/hierarchy_operations.h"
#include "system/sky/atmosphere.h"
#include "system/sky/sky_system.h"

namespace {

// The light travels away from the body the skybox draws toward; with the sign
// wrong, shadows point inward and nothing else says why.
void testTheSkyAimsTheKeyLight() {
    std::printf("What the sky does to the key light:\n");

    Scene scene;
    const EntityId sun = scene.createEntity();
    scene.add(sun, Transform{});
    Light key;
    key.type = LightType::Directional;
    scene.add(sun, std::move(key));

    Environment& env = scene.environment();
    env.sky.procedural    = true;
    env.sky.sunElevation  = 90.0f;      // straight overhead
    env.sky.sunAzimuth    = 0.0f;
    env.sky.lightColor    = {1.0f, 0.5f, 0.25f};
    env.sky.lightIntensity = 4.0f;

    TestFrame frame(scene);
    SkySystem sky;
    sky.update(frame.ctx);

    const glm::vec3 aim = Math::computeForward(scene.get<Transform>(sun).rotation);
    check("a sun overhead lights straight down", nearly(aim.y, -1.0f));
    check("  in the colour the environment says", nearly(scene.get<Light>(sun).color.g, 0.5f));
    check("  at the intensity it says", nearly(scene.get<Light>(sun).intensity, 4.0f));

    // Below the horizon the moon takes over - a swap, not a blend: the two sit
    // opposite, and interpolating would sweep through directions neither occupies.
    env.sky.sunElevation      = -60.0f;
    env.night.moonlightColor  = {0.2f, 0.4f, 1.0f};
    sky.update(frame.ctx);
    check(
        "below the horizon the moon owns the light",
        nearly(scene.get<Light>(sun).color.b, 1.0f) && nearly(scene.get<Light>(sun).color.r, 0.2f)
    );
    const glm::vec3 nightAim = Math::computeForward(scene.get<Transform>(sun).rotation);
    check("  and it comes from above too, not from under the world", nightAim.y < 0.0f);

    // A Transform is local, so a turned parent's rotation must be divided out.
    env.sky.sunElevation = 90.0f;
    const EntityId rig = scene.createEntity();
    Transform tilted;
    tilted.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    scene.add(rig, std::move(tilted));
    HierarchyOperations::setParent(scene, sun, rig);
    sky.update(frame.ctx);

    const glm::quat world = scene.get<Transform>(rig).rotation * scene.get<Transform>(sun).rotation;
    check(
        "a key light under a turned parent still lights straight down in the world",
        nearly(Math::computeForward(world).y, -1.0f)
    );
    HierarchyOperations::removeFromParent(scene, sun);

    // A non-procedural scene's key light is authored; the sky must not overwrite it.
    env.sky.procedural = false;
    scene.get<Light>(sun).intensity = 7.0f;
    sky.update(frame.ctx);
    check(
        "a scene that is not procedural keeps its authored light",
        nearly(scene.get<Light>(sun).intensity, 7.0f)
    );
}

// The sunlight crosses the atmosphere the sky scatters through; an orange sky over
// a white light reads as two different evenings. Noon is as authored; below it the
// light loses blue first and never gains anything back on the way down.
void testTheSunlightCrossesTheAtmosphere() {
    std::printf("What the atmosphere does to the sunlight:\n");

    SkySettings sky;
    const auto at = [&sky](float elevation) {
        sky.sunElevation = elevation;
        return Atmosphere::sunTransmittance(sky);
    };

    const glm::vec3 zenith = at(90.0f);
    check(
        "a sun overhead arrives as authored",
        nearly(zenith.r, 1.0f) && nearly(zenith.g, 1.0f) && nearly(zenith.b, 1.0f)
    );

    bool falls    = true;
    bool reddens  = true;
    glm::vec3 above = zenith;
    for (float elevation = 85.0f; elevation >= 0.0f; elevation -= 5.0f) {
        const glm::vec3 t = at(elevation);
        if (t.r > above.r || t.g > above.g || t.b > above.b) falls = false;
        if (!(t.b <= t.g && t.g <= t.r)) reddens = false;
        above = t;
    }
    check("  and lower down it only ever dims", falls);
    check("  blue first, then green, then red", reddens);

    const glm::vec3 low = at(3.0f);
    std::printf("      at 3 degrees: %.3f %.3f %.3f\n", low.r, low.g, low.b);
    check("a sun just over the horizon is red and dim", low.r < 0.6f && low.b < 0.1f * low.r);
    check("a sun below the horizon sends nothing", glm::all(glm::equal(at(-5.0f), glm::vec3(0.0f))));

    sky.mie = 4.0f;
    const glm::vec3 hazy = at(10.0f);
    sky.mie = 1.0f;
    const glm::vec3 clear = at(10.0f);
    check("haze the sky scatters more dims the light more", hazy.r < clear.r && hazy.b < clear.b);

    // The key light gets that, times what was authored.
    Scene scene;
    const EntityId sun = scene.createEntity();
    scene.add(sun, Transform{});
    Light key;
    key.type = LightType::Directional;
    scene.add(sun, std::move(key));
    Environment& env = scene.environment();
    env.sky.procedural   = true;
    env.sky.sunElevation = 3.0f;
    env.sky.lightColor   = {1.0f, 1.0f, 1.0f};
    TestFrame frame(scene);
    SkySystem system;
    system.update(frame.ctx);
    const glm::vec3 lit = scene.get<Light>(sun).color;
    check(
        "the key light at a low sun is the sunlight through the air",
        nearly(lit.r, low.r) && nearly(lit.g, low.g) && nearly(lit.b, low.b)
    );
}

// A disabled light is not the key (see findKeyLight); aiming it would turn a light
// nobody sees while the shadows came from another.
void testTheSkyAimsTheLightThatIsLit() {
    std::printf("Which light the sky aims when the first one is off:\n");

    Scene scene;
    const EntityId off = scene.createEntity();
    scene.add(off, Transform{});
    Light dark;
    dark.type    = LightType::Directional;
    dark.enabled = false;
    scene.add(off, std::move(dark));

    const EntityId lit = scene.createEntity();
    scene.add(lit, Transform{});
    Light shining;
    shining.type = LightType::Directional;
    scene.add(lit, std::move(shining));

    Environment& env = scene.environment();
    env.sky.procedural   = true;
    env.sky.sunElevation = 90.0f;

    TestFrame frame(scene);
    SkySystem sky;
    sky.update(frame.ctx);

    check("the key light is the enabled one", findKeyLight(scene) == lit);
    check(
        "  and that is the one the sky points down",
        nearly(Math::computeForward(scene.get<Transform>(lit).rotation).y, -1.0f)
    );
    check(
        "  leaving the disabled one as it was",
        nearly(Math::computeForward(scene.get<Transform>(off).rotation).z, -1.0f)
    );
}

// Each axis of the fog grid is bounded, and so is the whole: every axis at its
// maximum would be two volumes of a gigabyte each.
void testTheFogGridIsBoundedWhole() {
    FogSettings fog;
    const glm::uvec3 standard = fog.froxelGrid();
    check(
        "the default grid is the authored one",
        standard == glm::uvec3(fog.resolutionX, fog.resolutionY, fog.resolutionZ)
    );

    fog.resolutionX = FogSettings::MAX_FROXELS;
    fog.resolutionY = FogSettings::MAX_FROXELS;
    fog.resolutionZ = FogSettings::MAX_FROXELS;
    const glm::uvec3 most = fog.froxelGrid();
    check(
        "every axis at its most is held to the whole's cap",
        static_cast<uint64_t>(most.x) * most.y * most.z <= FogSettings::MAX_FROXEL_COUNT
    );
    check("  scaled evenly", most.x == most.y && most.y == most.z);
}

} // namespace

void runSkyTests() {
    testTheSkyAimsTheKeyLight();
    testTheSunlightCrossesTheAtmosphere();
    testTheSkyAimsTheLightThatIsLit();
    testTheFogGridIsBoundedWhole();
}

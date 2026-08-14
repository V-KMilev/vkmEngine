#include "stress_arena.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/axes.h"
#include "core/math/easing.h"
#include "ecs/scene.h"
#include "io/asset/asset_library.h"
#include "io/asset/cooked_loader.h"
#include "ecs/component/animation.h"
#include "ecs/component/camera.h"
#include "ecs/component/collider.h"
#include "ecs/component/decal.h"
#include "ecs/component/irradiance_volume.h"
#include "ecs/component/light.h"
#include "ecs/component/lod.h"
#include "ecs/component/mesh.h"
#include "ecs/component/name.h"
#include "ecs/component/particle_emitter.h"
#include "ecs/component/reflection_probe.h"
#include "ecs/component/rigidbody.h"
#include "ecs/component/transform.h"
#include "ecs/component/ui_canvas.h"
#include "ecs/component/ui_element.h"
#include "ecs/component/ui_image.h"
#include "ecs/component/ui_text.h"
#include "platform/window/glfw_include.h"
#include "platform/input/input_map.h"
#include "platform/window/input_handle.h"
#include "proc_mesh.h"
#include "resource/resource_manager.h"
#include "system/hierarchy/hierarchy_operations.h"

// Goes to stderr rather than the engine logger: this file also compiles into the
// editor's hot-reload module, which resolves symbols from the host exe, and
// Logger lives in a DLL the exe cannot re-export (same reason as potion_runner).
#define STRESS_LOG(...) (std::fprintf(stderr, "[Stress] " __VA_ARGS__), std::fputc('\n', stderr))

namespace Engine {

namespace {

// The arena is a square city block centred on the origin. Everything is placed
// inside this radius so the scripted camera never flies out of the lit region
// and leaves the profile measuring empty sky.
//
// Sized so the scatter below has room to breathe: the same object count over a
// small footprint packs into a continuous carpet, which is both unreadable and
// misleading to profile - overdraw goes through the roof and nothing ever
// resolves as an individual mesh for the size cull to reject.
constexpr float ARENA_HALF = 150.0f;
constexpr float GROUND_Y   = 0.0f;

// The pit the physics pile falls into, at the centre of the block. Bodies that
// tumble past its rim are relaunched rather than left to sleep - a settled pile
// stops exercising the solver, which is the opposite of what this scene is for.
constexpr float PIT_HALF  = 14.0f;
constexpr float PIT_DEPTH = 3.0f;

// Placement zones, outward from the centre. Everything the camera looks *at*
// goes inside PROP_ZONE_OUTER; the towers form a ring beyond CAM_RADIUS. The
// camera therefore flies a clear lane between the two, which matters for more
// than looks: a path that runs through the buildings spends much of the loop
// with the near plane inside a wall, and then the capture is measuring the
// inside of a box rather than the scene.
constexpr float PROP_ZONE_INNER  = PIT_HALF + 6.0f;
constexpr float PROP_ZONE_OUTER  = 92.0f;
constexpr float TOWER_ZONE_INNER = 118.0f;
constexpr float TOWER_ZONE_OUTER = ARENA_HALF - 8.0f;

// Scripted camera: a circuit in that clear lane, looking inward across the
// block rather than down into it. The height oscillates so the frame alternates
// between street level (heavy overdraw, many lights in frustum) and a raised
// view (heavy draw count, deep clusters) - one loop covers both.
//
// The look target sits above the camera's own mean height on purpose. Aiming
// down at the pit filled the frame with ground and left the skybox off screen,
// which quietly drops the sky out of the very profile it belongs in; a
// level-to-rising view keeps the horizon and the far skyline in shot.
constexpr float CAM_RADIUS     = 104.0f;
constexpr float CAM_HEIGHT     = 26.0f;
constexpr float CAM_HEIGHT_AMP = 11.0f;
// Just above the prop field rather than up at the tower tops: high enough to
// keep the horizon and sky in frame, low enough that the scene - not empty sky -
// is what fills it, since empty sky is the one thing here that costs nothing to
// draw and so is the least useful thing to point the profiler at.
constexpr float CAM_LOOK_Y     = 4.0f;

constexpr uint64_t ARENA_SEED = 0xC0FFEEu;
// Second PCG stream for the runtime churn, so it cannot disturb the layout draw.
constexpr uint64_t CHURN_STREAM = 0x51ED2701u;

// The arena's own actions. Named rather than keyed so the bindings below are
// the only place a key appears, and a capture session can rebind them without
// touching this file.
constexpr const char* ACTION_LIGHTS    = "Stress/ToggleLights";
constexpr const char* ACTION_SHADOWS   = "Stress/ToggleShadows";
constexpr const char* ACTION_PROPS     = "Stress/ToggleProps";
constexpr const char* ACTION_PARTICLES = "Stress/ToggleParticles";
constexpr const char* ACTION_PHYSICS   = "Stress/TogglePhysics";
constexpr const char* ACTION_ANIM      = "Stress/ToggleAnimation";
constexpr const char* ACTION_DECALS    = "Stress/ToggleDecals";
constexpr const char* ACTION_FOG       = "Stress/ToggleFog";
constexpr const char* ACTION_UI        = "Stress/ToggleUI";
constexpr const char* ACTION_RESET     = "Stress/ResetToggles";
constexpr const char* ACTION_CAMERA    = "Stress/ToggleCamera";

/**
 * @brief Bind the arena's actions to the number row, plus F for the camera.
 */
void installStressBindings(InputMap& map) {
    const auto key = [](int code) { return InputBinding{InputSource::Key, code, 1.0f}; };
    map.define(ACTION_LIGHTS,    { key(GLFW_KEY_1) });
    map.define(ACTION_SHADOWS,   { key(GLFW_KEY_2) });
    map.define(ACTION_PROPS,     { key(GLFW_KEY_3) });
    map.define(ACTION_PARTICLES, { key(GLFW_KEY_4) });
    map.define(ACTION_PHYSICS,   { key(GLFW_KEY_5) });
    map.define(ACTION_ANIM,      { key(GLFW_KEY_6) });
    map.define(ACTION_DECALS,    { key(GLFW_KEY_7) });
    map.define(ACTION_FOG,       { key(GLFW_KEY_8) });
    map.define(ACTION_UI,        { key(GLFW_KEY_9) });
    map.define(ACTION_RESET,     { key(GLFW_KEY_0) });
    map.define(ACTION_CAMERA,    { key(GLFW_KEY_F) });
}

/**
 * @brief Place item @p index of @p count evenly across a ground annulus.
 *
 * Golden-angle (Vogel) placement: consecutive indices land far apart in angle
 * while the radius grows as a square root, which spreads points evenly by *area*
 * instead of by radius. Drawing the angle and radius at random - which is what
 * this scene did first - clumps badly at these counts, and a radius drawn with a
 * squared bias piles everything against the inner edge on top of that. The
 * result read as one continuous carpet rather than as thousands of objects.
 *
 * Even coverage is not only cosmetic. A carpet means huge overdraw in the near
 * field and nothing small enough for the screen-size cull to ever reject, so it
 * quietly profiles a different renderer than the one being shipped.
 *
 * The jitter is a fraction of the local spacing, so it breaks up the visible
 * lattice without letting neighbours collide again.
 *
 * @param index   Item index; consecutive values are spread apart, not adjacent.
 * @param count   Total items sharing the annulus, which sets the spacing.
 * @param inner   Inner radius of the annulus.
 * @param outer   Outer radius.
 * @param rng     Source for the jitter.
 * @param jitter  Jitter as a fraction of local spacing (0 = a perfect lattice).
 * @return A ground-plane position; Y is left at zero for the caller to set.
 */
glm::vec3 scatterOnGround(int index, int count, float inner, float outer,
                          Math::Rng& rng, float jitter = 0.4f) {
    constexpr float GOLDEN_ANGLE = 2.39996323f;

    const int   total = std::max(1, count);
    const float u     = (static_cast<float>(index) + 0.5f) / static_cast<float>(total);
    const float radius = std::sqrt(inner * inner + u * (outer * outer - inner * inner));

    // Mean centre-to-centre distance at this density, used to size the jitter.
    const float spacing = (outer - inner) / std::sqrt(static_cast<float>(total));

    const float radial  = rng.nextFloat(-1.0f, 1.0f) * spacing * jitter;
    const float jittered = glm::clamp(radius + radial, inner, outer);
    // Convert the same linear jitter into an angle at this radius, so the
    // spacing stays even rather than tightening toward the middle.
    const float angular = rng.nextFloat(-1.0f, 1.0f) * spacing * jitter / std::max(1.0f, jittered);

    const float theta = static_cast<float>(index) * GOLDEN_ANGLE + angular;
    return {std::cos(theta) * jittered, 0.0f, std::sin(theta) * jittered};
}

/**
 * @brief A looping spin about Y, for props that should keep the AnimationSystem
 *        and the hierarchy's dirty-transform walk busy.
 *
 * Three keys 120 degrees apart with linear easing, so each slerp takes the short
 * way round and the rotation rate stays constant across the loop.
 */
Animation makeSpin(float period, float phase) {
    Animation anim;
    anim.rotationTrack.setEasing(&Easing::linear);
    for (int k = 0; k <= 3; ++k) {
        anim.rotationTrack.addKeyframe(
            period * static_cast<float>(k) / 3.0f,
            glm::angleAxis(glm::two_pi<float>() * static_cast<float>(k) / 3.0f, Math::WORLD_AXIS_Y));
    }
    anim.time    = phase;
    anim.playing = true;
    anim.looping = true;
    anim.updateDuration();
    return anim;
}

/**
 * @brief A looping vertical bob, so some animated props write a position track
 *        (a different code path in the track evaluator than rotation).
 */
Animation makeBob(float period, float height, float phase) {
    Animation anim;
    anim.positionTrack.setEasing(Easing::byName("easeInOutSine"));
    anim.positionTrack.addKeyframe(0.0f,          {0.0f, 0.0f, 0.0f});
    anim.positionTrack.addKeyframe(period * 0.5f, {0.0f, height, 0.0f});
    anim.positionTrack.addKeyframe(period,        {0.0f, 0.0f, 0.0f});
    anim.time    = phase;
    anim.playing = true;
    anim.looping = true;
    anim.updateDuration();
    return anim;
}

/**
 * @brief A breathing scale pulse: the third single-track shape, and the only one
 *        that changes an entity's world bounds every frame.
 *
 * That matters beyond the track evaluator - a prop whose extent keeps changing
 * cannot have its culling result reused, so this is what makes the screen-size
 * cull recompute rather than coast.
 */
Animation makePulse(float period, float amount, float phase) {
    Animation anim;
    anim.scaleTrack.setEasing(Easing::byName("easeInOutSine"));
    anim.scaleTrack.addKeyframe(0.0f,          glm::vec3(1.0f));
    anim.scaleTrack.addKeyframe(period * 0.5f, glm::vec3(1.0f + amount));
    anim.scaleTrack.addKeyframe(period,        glm::vec3(1.0f));
    anim.time    = phase;
    anim.playing = true;
    anim.looping = true;
    anim.updateDuration();
    return anim;
}

/**
 * @brief All three tracks at once: the worst case for the evaluator, since a
 *        clip driving position, rotation and scale together costs three
 *        keyframe searches and three interpolations per entity per frame.
 *
 * A scene of single-track clips quietly measures a third of what a real
 * animated character costs.
 */
Animation makeOrbitHop(float period, float radius, float height, float phase) {
    Animation anim;
    anim.positionTrack.setEasing(Easing::byName("easeInOutSine"));
    anim.rotationTrack.setEasing(&Easing::linear);
    anim.scaleTrack.setEasing(Easing::byName("easeInOutSine"));

    // Four position keys tracing a square-ish loop, three rotation keys for a
    // constant-rate spin, and a scale squash on the down beats.
    for (int k = 0; k <= 4; ++k) {
        const float t     = period * static_cast<float>(k) / 4.0f;
        const float angle = glm::half_pi<float>() * static_cast<float>(k);
        anim.positionTrack.addKeyframe(t, {
            std::cos(angle) * radius,
            (k % 2 == 0) ? 0.0f : height,
            std::sin(angle) * radius
        });
    }
    for (int k = 0; k <= 3; ++k) {
        anim.rotationTrack.addKeyframe(
            period * static_cast<float>(k) / 3.0f,
            glm::angleAxis(glm::two_pi<float>() * static_cast<float>(k) / 3.0f, Math::WORLD_AXIS_Y));
    }
    anim.scaleTrack.addKeyframe(0.0f,          glm::vec3(1.0f));
    anim.scaleTrack.addKeyframe(period * 0.25f, glm::vec3(0.8f, 1.25f, 0.8f));
    anim.scaleTrack.addKeyframe(period * 0.5f,  glm::vec3(1.0f));
    anim.scaleTrack.addKeyframe(period * 0.75f, glm::vec3(1.2f, 0.78f, 1.2f));
    anim.scaleTrack.addKeyframe(period,         glm::vec3(1.0f));

    anim.time    = phase;
    anim.playing = true;
    anim.looping = true;
    anim.updateDuration();
    return anim;
}

} // namespace

void StressArena::onStart() {
    if (m_built) return;
    m_built = true;

    m_scene     = context().scene;
    m_resources = context().resources;
    m_window    = context().window;
    m_rng.seed(ARENA_SEED);
    installStressBindings(*context().input);
    m_churnRng.seed(ARENA_SEED, CHURN_STREAM);

    // Procedural sky rather than an HDR file, for the same reason nothing else
    // here is loaded: the atmosphere is baked from parameters, so the lighting
    // environment is identical on every machine and no capture depends on which
    // .hdr happens to be present. The bake follows the primary directional light
    // (the sun built in buildLights) and re-runs only when it or a sky parameter
    // moves - so it costs one bake at startup, not one per frame.
    Environment& environment = m_scene->environment();
    environment.showSkybox    = true;
    environment.proceduralSky = true;
    environment.intensity     = 1.0f;

    // Daylight: a mid-morning sun high enough to light the block from above, so
    // the towers read as solid volumes with their own cast shadows rather than
    // as flat silhouettes against a bright horizon. Rayleigh carries the blue,
    // Mie is kept modest - a large Mie term at this elevation washes the whole
    // sky toward white and buries the geometry it is supposed to light.
    environment.skySunIntensity     = 22.0f;
    environment.skyRayleigh         = 1.0f;
    environment.skyMie              = 0.7f;
    environment.skyMieG             = 0.76f;
    environment.skySunDiscIntensity = 15.0f;

    // Volumetric fog on by default: one of the heaviest passes in the pipeline
    // and the one most often left out of a benchmark, which is exactly why it
    // belongs in the default load. Thin enough for a clear day - it reads as
    // aerial haze over distance rather than as a ground fog bank, and still
    // costs the same froxel grid either way. Key 8 takes it out.
    environment.fogEnabled    = true;
    environment.fogDensity    = 0.006f;
    environment.fogHeight     = 18.0f;
    environment.fogHeightFalloff = 0.05f;
    environment.fogAnisotropy = 0.7f;

    environment.gravity = {0.0f, -18.0f, 0.0f};

    m_scene->forEach<Camera>([&](EntityId id, Camera& camera) {
        if (!m_camera) {
            m_camera = id;
            camera.zFar  = 600.0f;   // the far towers must stay in frustum
            camera.zNear = 0.2f;

            // Depth of field is off unless a camera asks for it - the pass
            // early-outs at amount 0, so the default camera never runs it and
            // it would be missing from a capture entirely. Kept modest: enough
            // to put a real circle-of-confusion on the far skyline (which is
            // what the pass costs) without blurring the scene into mush.
            // focusDistance tracks the look target in updateCamera.
            camera.dofAmount     = 0.35f;
            camera.focusDistance = CAM_RADIUS;
        }
    });

    buildMaterials();
    buildGround();
    buildTowers();
    buildProps();
    buildLights();
    buildEmitters();
    buildDecals();
    buildProbes();
    buildPhysics();
    buildModels();
    buildDrones();
    buildUI();

    STRESS_LOG("built: %zu props, %zu lights (%d shadowed, %zu moving), %zu emitters, "
               "%zu decals, %zu bodies, %zu animated, %zu drones, %d materials",
               m_props.size(), m_lights.size(), shadowLights, m_patrol.size(),
               m_emitters.size(), m_decals.size(), m_bodies.size(), m_spinners.size(),
               m_drones.size(), uniqueMaterials);
    STRESS_LOG("keys: 1 lights  2 shadows  3 props  4 particles  5 physics  "
               "6 anim  7 decals  8 fog  9 UI  0 all   F camera");
}

MaterialHandle StressArena::makeMaterial(const MaterialAsset& source, const char* name) {
    MaterialAsset material = source;
    material.name = name;
    return m_resources->add(std::move(material), name);
}

void StressArena::buildMaterials() {
    m_cube     = m_resources->add(makeCubeMesh(), "stress:cube");
    m_sphere   = m_resources->add(makeSphereMesh(24, 12), "stress:sphere");
    m_cylinder = m_resources->add(makeCylinderMesh(20), "stress:cylinder");

    // Coarser builds of the round shapes for the far LOD levels. A cube has no
    // detail to drop, so it has no levels and keeps its single mesh - which is
    // also the case that proves LOD is opt-in per entity rather than global.
    m_sphereMid = m_resources->add(makeSphereMesh(12, 6), "stress:sphere_mid");
    m_sphereLow = m_resources->add(makeSphereMesh(6, 4),  "stress:sphere_low");
    m_cylMid    = m_resources->add(makeCylinderMesh(10),  "stress:cyl_mid");
    m_cylLow    = m_resources->add(makeCylinderMesh(6),   "stress:cyl_low");

    // Mid-grey architecture, roughly 40-55% albedo. Dark surfaces would swallow
    // the daylight and hide exactly the shadowing and GI this scene exists to
    // put under load.
    MaterialAsset base;
    base.roughness = 0.85f;
    base.metallic  = 0.0f;

    base.albedo = {0.42f, 0.43f, 0.45f, 1.0f};
    m_matGround = makeMaterial(base, "stress:ground");

    base.albedo    = {0.55f, 0.54f, 0.51f, 1.0f};
    base.roughness = 0.7f;
    m_matTower = makeMaterial(base, "stress:tower");

    // The prop palette. Each entry varies the parameters the PBR shader
    // branches on, so the forward pass is not measured on one uniform BRDF:
    // a share of the palette carries clearcoat, anisotropy or sheen, each of
    // which lights a different lobe in the shader.
    m_propMaterials.reserve(static_cast<size_t>(uniqueMaterials));
    for (int i = 0; i < uniqueMaterials; ++i) {
        MaterialAsset m;
        m.albedo    = glm::vec4(frand(0.15f, 0.9f), frand(0.15f, 0.9f), frand(0.15f, 0.9f), 1.0f);
        m.metallic  = (i % 3 == 0) ? frand(0.7f, 1.0f) : frand(0.0f, 0.25f);
        m.roughness = frand(0.12f, 0.85f);

        if (i % 4 == 0) {
            m.clearcoat          = frand(0.4f, 1.0f);
            m.clearcoatRoughness = frand(0.05f, 0.4f);
        }
        if (i % 5 == 0) {
            m.anisotropy = frand(0.3f, 0.9f);
        }
        if (i % 7 == 0) {
            m.sheenColor     = glm::vec3(frand(0.2f, 0.8f), frand(0.2f, 0.8f), frand(0.2f, 0.8f));
            m.sheenRoughness = frand(0.2f, 0.6f);
        }

        const std::string name = "stress:prop_" + std::to_string(i);
        m_propMaterials.push_back(makeMaterial(m, name.c_str()));
    }

    // Transparent glass: forces the sorted transparent queue and the
    // transmission path, which the opaque props never touch.
    MaterialAsset glass;
    glass.type         = MaterialType::Transparent;
    glass.albedo       = {0.75f, 0.85f, 0.95f, 0.32f};
    glass.roughness    = 0.08f;
    glass.metallic     = 0.0f;
    glass.transmission = 0.85f;
    glass.ior          = 1.45f;
    glass.thicknessFactor = 0.4f;
    m_matGlass = makeMaterial(glass, "stress:glass");

    MaterialAsset chrome;
    chrome.albedo    = {0.92f, 0.93f, 0.96f, 1.0f};
    chrome.metallic  = 1.0f;
    chrome.roughness = 0.06f;
    m_matChrome = makeMaterial(chrome, "stress:chrome");

    // Emissive fixtures still need to clear the bloom threshold in daylight, so
    // this is brighter than a night scene would want - the bloom pass has to see
    // something above threshold or it profiles an empty bright-pass.
    MaterialAsset emissive;
    emissive.albedo           = {1.0f, 0.88f, 0.62f, 1.0f};
    emissive.emission         = {1.0f, 0.80f, 0.45f};
    emissive.emissiveStrength = 6.0f;
    emissive.roughness        = 0.4f;
    m_matEmissive = makeMaterial(emissive, "stress:emissive");

    // Decal albedo alpha is what the projector blends on, so it must be < 1.
    MaterialAsset decal;
    decal.type   = MaterialType::Transparent;
    decal.albedo = {0.05f, 0.06f, 0.09f, 0.8f};
    decal.roughness = 0.9f;
    m_matDecal = makeMaterial(decal, "stress:decal");
}

Entity StressArena::spawnMesh(MeshHandle mesh, MaterialHandle material, const char* name,
                              const glm::vec3& position, const glm::vec3& scale) {
    Entity entity = m_scene->createEntity();
    m_scene->add(entity, makeName(name));
    m_scene->add(entity, Mesh{mesh, material});

    Transform transform;
    transform.position = position;
    transform.scale    = scale;
    m_scene->add(entity, std::move(transform));
    return entity;
}

void StressArena::buildGround() {
    Entity ground = spawnMesh(m_cube, m_matGround, "Ground",
                              {0.0f, GROUND_Y - 0.5f, 0.0f},
                              {ARENA_HALF * 2.0f, 1.0f, ARENA_HALF * 2.0f});
    // The ground can never occlude anything from a light above it, so keeping it
    // out of the shadow pass costs nothing and saves a full-extent draw per tile.
    m_scene->get<Mesh>(ground).castShadows = false;

    Rigidbody rb;
    rb.isStatic    = true;
    rb.inverseMass = 0.0f;
    m_scene->add(ground, std::move(rb));

    Collider col;
    col.parts = {ColliderBox{{0.0f, 0.0f, 0.0f}, {ARENA_HALF, 0.5f, ARENA_HALF}}};
    m_scene->add(ground, std::move(col));

    // Pit floor and four walls, so the physics pile has something to pack
    // against instead of scattering across the whole block.
    Entity floor = spawnMesh(m_cube, m_matTower, "Pit Floor",
                             {0.0f, GROUND_Y - PIT_DEPTH, 0.0f},
                             {PIT_HALF * 2.0f, 0.5f, PIT_HALF * 2.0f});
    m_scene->get<Mesh>(floor).castShadows = false;

    Rigidbody floorBody;
    floorBody.isStatic    = true;
    floorBody.inverseMass = 0.0f;
    m_scene->add(floor, std::move(floorBody));

    Collider floorCol;
    floorCol.parts = {ColliderBox{{0.0f, 0.0f, 0.0f}, {PIT_HALF, 0.25f, PIT_HALF}}};
    m_scene->add(floor, std::move(floorCol));

    for (int i = 0; i < 4; ++i) {
        const bool  alongX = (i % 2) == 0;
        const float sign   = (i < 2) ? 1.0f : -1.0f;
        const glm::vec3 position = alongX
            ? glm::vec3(sign * PIT_HALF, GROUND_Y - PIT_DEPTH * 0.5f, 0.0f)
            : glm::vec3(0.0f, GROUND_Y - PIT_DEPTH * 0.5f, sign * PIT_HALF);
        const glm::vec3 half = alongX
            ? glm::vec3(0.5f, PIT_DEPTH * 0.5f, PIT_HALF)
            : glm::vec3(PIT_HALF, PIT_DEPTH * 0.5f, 0.5f);

        Entity wall = spawnMesh(m_cube, m_matTower, "Pit Wall", position, half * 2.0f);
        m_scene->get<Mesh>(wall).castShadows = false;

        Rigidbody wallBody;
        wallBody.isStatic    = true;
        wallBody.inverseMass = 0.0f;
        m_scene->add(wall, std::move(wallBody));

        Collider wallCol;
        wallCol.parts = {ColliderBox{{0.0f, 0.0f, 0.0f}, half}};
        m_scene->add(wall, std::move(wallCol));
    }
}

void StressArena::buildTowers() {
    // Towers ring the block but leave the pit clear. Each is a stack of boxes
    // with a glass band and a lit crown, so the silhouette has depth complexity
    // for the prepass and mixed queues for the forward pass.
    for (int i = 0; i < towerCount; ++i) {
        const glm::vec3 base = scatterOnGround(i, towerCount, TOWER_ZONE_INNER,
                                              TOWER_ZONE_OUTER, m_rng, 0.55f);

        const int   floors = m_rng.nextInt(2, 7);
        const float width  = frand(4.0f, 9.0f);
        const float depth  = frand(4.0f, 9.0f);
        float       y      = GROUND_Y;

        for (int f = 0; f < floors; ++f) {
            const float height = frand(3.0f, 6.0f);
            // Every third floor is the glass band.
            const MaterialHandle material = (f % 3 == 1) ? m_matGlass : m_matTower;

            spawnMesh(m_cube, material, "Tower",
                      {base.x, y + height * 0.5f, base.z},
                      {width, height, depth});
            y += height;
        }

        // Lit crown: an emissive cap, so the bloom pass always has bright
        // sources spread across the frame rather than clustered in one spot.
        spawnMesh(m_cube, m_matEmissive, "Tower Crown",
                  {base.x, y + 0.4f, base.z},
                  {width * 0.55f, 0.8f, depth * 0.55f});
    }
}

void StressArena::buildProps() {
    m_props.reserve(static_cast<size_t>(propCount));
    m_spinners.reserve(static_cast<size_t>(animatedCount));

    for (int i = 0; i < propCount; ++i) {
        const glm::vec3 spot = scatterOnGround(i, propCount, PROP_ZONE_INNER,
                                              PROP_ZONE_OUTER, m_rng);
        const float scale = frand(0.5f, 2.2f);

        const int shape = i % 3;
        const MeshHandle mesh = (shape == 0) ? m_cube : (shape == 1) ? m_sphere : m_cylinder;

        // A slice of props take chrome instead of the palette so there are
        // smooth metals scattered everywhere for the probes to show up in.
        const MaterialHandle material = (i % 23 == 0)
            ? m_matChrome
            : m_propMaterials[static_cast<size_t>(i) % m_propMaterials.size()];

        Entity prop = spawnMesh(mesh, material, "Prop",
            {spot.x, GROUND_Y + scale * 0.5f, spot.z}, glm::vec3(scale));

        m_props.push_back(prop);

        // Round props drop tessellation with distance; cubes have nothing to
        // drop. Thresholds are deliberately short for the arena's scale so the
        // switch is exercised across the camera loop rather than never reached.
        if (lodEnabled && shape != 0) {
            LOD lod;
            if (shape == 1) lod.levels = { {m_sphere,   35.0f}, {m_sphereMid, 70.0f}, {m_sphereLow, 0.0f} };
            else            lod.levels = { {m_cylinder, 35.0f}, {m_cylMid,    70.0f}, {m_cylLow,    0.0f} };
            m_scene->add(prop, std::move(lod));
        }

        // The first animatedCount props get a track. Spread across all four clip
        // shapes so the evaluator is measured on its real mix rather than on one
        // branch: rotation-only, position-only, scale-only (which also keeps the
        // culling bounds moving) and one clip driving all three at once.
        if (static_cast<int>(m_spinners.size()) < animatedCount) {
            switch (i % 4) {
                case 0:
                    m_scene->add(prop, makeSpin(frand(2.0f, 6.0f), frand(0.0f, 4.0f)));
                    break;
                case 1:
                    m_scene->add(prop, makeBob(frand(1.5f, 4.0f), frand(0.5f, 2.5f), frand(0.0f, 3.0f)));
                    break;
                case 2:
                    m_scene->add(prop, makePulse(frand(1.2f, 3.5f), frand(0.2f, 0.7f), frand(0.0f, 3.0f)));
                    break;
                default:
                    m_scene->add(prop, makeOrbitHop(frand(3.0f, 7.0f), frand(0.6f, 2.4f),
                                                    frand(0.8f, 2.6f), frand(0.0f, 5.0f)));
                    break;
            }
            m_spinners.push_back(prop);
        }
    }
}

void StressArena::buildLights() {
    m_lights.reserve(static_cast<size_t>(lightCount));

    for (int i = 0; i < lightCount; ++i) {
        // Two thirds light the prop field the camera looks at; the rest sit out
        // in the tower ring so the skyline is lit rather than a black cutout.
        // Spread evenly for the same reason the props are: clustered lights
        // leave dark gaps and pile several into one cluster cell, which is not
        // the binning behaviour worth measuring.
        const bool  inField = (i % 3) != 0;
        const glm::vec3 spot = inField
            ? scatterOnGround(i, lightCount, PIT_HALF, PROP_ZONE_OUTER, m_rng, 0.5f)
            : scatterOnGround(i, lightCount, TOWER_ZONE_INNER, TOWER_ZONE_OUTER, m_rng, 0.5f);
        const float height  = inField ? frand(4.0f, 18.0f) : frand(8.0f, 34.0f);
        const glm::vec3 position(spot.x, GROUND_Y + height, spot.z);

        Entity entity = m_scene->createEntity();
        m_scene->add(entity, makeName("Light"));

        Transform transform;
        transform.position = position;
        // Spots point down and outward; the rotation is only read for spots.
        transform.rotation = glm::angleAxis(frand(0.6f, 1.4f), Math::WORLD_AXIS_X);
        m_scene->add(entity, std::move(transform));

        Light light;
        // A third are spots: they take a cheaper 2D atlas tile than a point
        // light's cube, so the mix decides what the shadow pass actually costs.
        light.type  = (i % 3 == 0) ? LightType::Spot : LightType::Point;
        light.color = glm::vec3(frand(0.5f, 1.0f), frand(0.5f, 1.0f), frand(0.6f, 1.0f));
        // Sized to read against daylight without blowing out: the cluster pass
        // costs the same whatever the intensity, so this is purely so the frame
        // stays legible while several hundred of them are in it.
        light.intensity = frand(8.0f, 22.0f);
        light.radius    = frand(10.0f, 24.0f);
        light.innerConeAngle = 0.35f;
        light.outerConeAngle = 0.7f;
        // Only the first shadowLights cast: the atlas has a fixed tile budget,
        // and every extra caster is a full extra scene pass.
        light.castShadows = (i < shadowLights);
        m_scene->add(entity, std::move(light));

        // Every light gets a visible emissive fixture. Without one the frame
        // reads as light with no source, and the bloom pass has nothing to
        // threshold where the brightness actually comes from.
        Entity fixture = spawnMesh(m_sphere, m_matEmissive, "Light Fixture",
                                   position, glm::vec3(0.45f));
        m_scene->get<Mesh>(fixture).castShadows = false;

        m_lights.push_back(entity);

        // The first movingLights entries patrol instead of standing still.
        // Recorded here rather than derived later so the orbit keeps the radius
        // and height the light was placed at, and the ring stays evenly spread.
        if (i < movingLights) {
            m_patrol.push_back(PatrolLight{
                entity, fixture, glm::length(glm::vec2(spot.x, spot.z)), height,
                frand(0.10f, 0.55f) * (i % 2 == 0 ? 1.0f : -1.0f),
                frand(0.0f, glm::two_pi<float>()),
                frand(1.0f, 4.0f)
            });
        }
    }

    // One directional key light. It drives the CSM cascades and the contact
    // shadow pass, neither of which any point or spot light reaches - and the
    // procedural sky bakes its atmosphere around this direction, so the sky and
    // the key light stay consistent.
    Entity sun = m_scene->createEntity();
    m_scene->add(sun, makeName("Sun"));

    Transform sunTransform;
    // Mid-morning, matching the daylight atmosphere set in onStart. High enough
    // that the towers cast shadows down onto the block instead of across the
    // whole arena, and off-axis in yaw so those shadows fall diagonally and the
    // cascades cover a genuinely varied depth range rather than one flat slab.
    //
    // The pitch is POSITIVE for a sun overhead. A light's direction is the way
    // it shines, and this engine's forward is +Z (Math::computeForward), so a
    // positive X rotation tilts +Z downward - the -Z convention most GL code
    // assumes would put the sun under the floor shining up.
    sunTransform.rotation = glm::quat(glm::vec3(glm::radians(70.0f), glm::radians(35.0f), 0.0f));
    m_scene->add(sun, std::move(sunTransform));

    Light sunLight;
    sunLight.type           = LightType::Directional;
    sunLight.color          = {1.0f, 0.96f, 0.90f};   // near-white daylight, faintly warm
    sunLight.intensity      = 4.5f;
    sunLight.castShadows    = true;
    sunLight.shadowDistance = 280.0f;
    m_scene->add(sun, std::move(sunLight));

    m_lights.push_back(sun);
}

void StressArena::buildEmitters() {
    m_emitters.reserve(static_cast<size_t>(emitterCount));

    for (int i = 0; i < emitterCount; ++i) {
        const glm::vec3 spot = scatterOnGround(i, emitterCount, PIT_HALF + 4.0f,
                                              PROP_ZONE_OUTER, m_rng);

        Entity entity = m_scene->createEntity();
        m_scene->add(entity, makeName("Emitter"));

        Transform transform;
        transform.position = {spot.x, GROUND_Y + 1.0f, spot.z};
        m_scene->add(entity, std::move(transform));

        ParticleEmitter emitter;
        // Half additive sparks, half alpha smoke: the two blend modes sort and
        // draw separately, so a mix measures the real transparent path rather
        // than one homogeneous batch.
        const bool sparks = (i % 2) == 0;
        emitter.additive     = sparks;
        emitter.rate         = sparks ? frand(60.0f, 140.0f) : frand(20.0f, 50.0f);
        emitter.lifetime     = sparks ? frand(0.8f, 1.8f) : frand(2.5f, 4.5f);
        emitter.maxParticles = sparks ? 320 : 200;
        emitter.velocity     = sparks ? glm::vec3(0.0f, 5.0f, 0.0f) : glm::vec3(0.0f, 1.6f, 0.0f);
        emitter.spread       = sparks ? 2.4f : 0.8f;
        emitter.acceleration = sparks ? glm::vec3(0.0f, -6.0f, 0.0f) : glm::vec3(0.0f, 0.5f, 0.0f);
        emitter.startColor   = sparks ? glm::vec4(1.0f, 0.75f, 0.30f, 1.0f)
                                      : glm::vec4(0.55f, 0.58f, 0.65f, 0.5f);
        emitter.endColor     = sparks ? glm::vec4(1.0f, 0.20f, 0.05f, 0.0f)
                                      : glm::vec4(0.30f, 0.32f, 0.38f, 0.0f);
        emitter.startSize    = sparks ? 0.18f : 1.2f;
        emitter.endSize      = sparks ? 0.02f : 3.4f;
        emitter.softness     = sparks ? 0.3f : 1.0f;
        m_scene->add(entity, std::move(emitter));

        // The brazier the sparks come off.
        Entity source = spawnMesh(m_cylinder, m_matEmissive, "Brazier",
            {spot.x, GROUND_Y + 0.4f, spot.z}, {1.1f, 0.8f, 1.1f});
        m_scene->get<Mesh>(source).castShadows = false;

        m_emitters.push_back(entity);
    }
}

void StressArena::buildDecals() {
    m_decals.reserve(static_cast<size_t>(decalCount));

    for (int i = 0; i < decalCount; ++i) {
        const glm::vec3 spot = scatterOnGround(i, decalCount, PIT_HALF,
                                              PROP_ZONE_OUTER, m_rng);

        Entity entity = m_scene->createEntity();
        m_scene->add(entity, makeName("Decal"));

        Transform transform;
        // Sit above the ground and project down: the box's Y extent is the
        // projection depth, so it must reach the surface it marks.
        transform.position = {spot.x, GROUND_Y + 2.0f, spot.z};
        transform.rotation = glm::angleAxis(frand(0.0f, glm::two_pi<float>()), Math::WORLD_AXIS_Y);
        transform.scale    = glm::vec3(frand(3.0f, 8.0f), 5.0f, frand(3.0f, 8.0f));
        m_scene->add(entity, std::move(transform));

        Decal decal;
        decal.material  = m_matDecal;
        decal.angleFade = 0.6f;
        decal.opacity   = frand(0.35f, 0.9f);
        m_scene->add(entity, std::move(decal));

        m_decals.push_back(entity);
    }
}

void StressArena::buildProbes() {
    // Reflection probes: each bakes six faces of the scene on first sight
    // (throttled to one per frame by the probe manager), so they show up as a
    // burst of long frames at startup and then settle. That startup burst is
    // itself worth capturing - it is what a player sees on level load.
    for (int i = 0; i < reflectionProbes; ++i) {
        const float angle  = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(std::max(1, reflectionProbes));
        const float radius = ARENA_HALF * 0.45f;

        Entity entity = m_scene->createEntity();
        m_scene->add(entity, makeName("Reflection Probe"));

        Transform transform;
        transform.position = {std::cos(angle) * radius, GROUND_Y + 10.0f, std::sin(angle) * radius};
        m_scene->add(entity, std::move(transform));

        ReflectionProbe probe;
        probe.halfExtents = glm::vec3(ARENA_HALF * 0.5f, 22.0f, ARENA_HALF * 0.5f);
        probe.resolution  = 256;
        probe.intensity   = 1.0f;
        m_scene->add(entity, std::move(probe));
    }

    // One irradiance volume over the whole block, so the diffuse GI path is
    // exercised alongside the specular probes.
    Entity volume = m_scene->createEntity();
    m_scene->add(volume, makeName("Irradiance Volume"));

    Transform transform;
    transform.position = {0.0f, GROUND_Y + 14.0f, 0.0f};
    m_scene->add(volume, std::move(transform));

    IrradianceVolume irradiance;
    irradiance.halfExtents = glm::vec3(ARENA_HALF, 20.0f, ARENA_HALF);
    irradiance.resolutionX = 12;
    irradiance.resolutionY = 4;
    irradiance.resolutionZ = 12;
    m_scene->add(volume, std::move(irradiance));
}

void StressArena::buildPhysics() {
    m_bodies.reserve(static_cast<size_t>(physicsBodies));

    for (int i = 0; i < physicsBodies; ++i) {
        const float size = frand(0.6f, 1.4f);
        const glm::vec3 position(
            frand(-PIT_HALF + 2.0f, PIT_HALF - 2.0f),
            GROUND_Y + frand(2.0f, 40.0f),
            frand(-PIT_HALF + 2.0f, PIT_HALF - 2.0f));

        const MaterialHandle material =
            m_propMaterials[static_cast<size_t>(i) % m_propMaterials.size()];

        Entity entity = spawnMesh(m_cube, material, "Body", position, glm::vec3(size));

        Rigidbody rb;
        rb.mass        = size * size * size * 8.0f;
        rb.restitution = 0.35f;
        rb.friction    = 0.45f;
        // These must keep moving to be worth measuring: a slept body leaves the
        // solver entirely, and a pile that settles quietly stops being a load.
        rb.canSleep = false;
        m_scene->add(entity, std::move(rb));

        Collider col;
        col.parts = {ColliderBox{{0.0f, 0.0f, 0.0f}, glm::vec3(size * 0.5f)}};
        m_scene->add(entity, std::move(col));

        m_bodies.push_back(entity);
    }
}

void StressArena::buildModels() {
    if (modelInstances <= 0) return;

    AssetLibrary& library = AssetLibrary::get();

    // Discover what the project actually has rather than hardcoding names; the
    // list is sorted, so the same meshes are picked on every run.
    const std::vector<std::string> meshNames = library.namesOf(AssetType::Mesh);
    const std::vector<std::string> textureNames = library.namesOf(AssetType::Texture);

    // Skip the engine's own generated primitives - those are the procedural
    // shapes the props already use, and re-adding them would measure nothing new.
    std::vector<std::string> usable;
    for (const std::string& name : meshNames) {
        if (name.rfind("mesh:generator:", 0) == 0) continue;
        usable.push_back(name);
    }

    if (usable.empty()) {
        STRESS_LOG("no cooked meshes in the library - running procedural props only");
        return;
    }

    // Bounded by what the project actually has; instances spread across whatever
    // is taken. Every kind costs a cooked read at startup, so this is the dial
    // that decides how long the load screen is as well as how varied the scene.
    const size_t kinds = std::min(usable.size(), static_cast<size_t>(std::max(1, modelKinds)));

    // Textured materials built from cooked albedo maps. Sampling a real texture
    // is a different cost from the props' flat colours, and it is the only thing
    // in the scene that exercises the texture bindings in GLMaterial.
    std::vector<MaterialHandle> materials;
    for (size_t i = 0; i < textureNames.size() && materials.size() < 8; ++i) {
        // "#s" marks an sRGB-cooked texture, which is what an albedo map is;
        // the linear ones are normal/ORM maps and would read as flat colour here.
        if (textureNames[i].size() < 2 ||
            textureNames[i].compare(textureNames[i].size() - 2, 2, "#s") != 0) continue;

        MaterialAsset m;
        m.albedo        = {1.0f, 1.0f, 1.0f, 1.0f};
        m.roughness     = 0.65f;
        m.metallic      = 0.0f;
        m.albedoTexture = requestCookedTextureAsync(textureNames[i], *m_resources);
        if (!m.albedoTexture) continue;

        const std::string name = "stress:model_mat_" + std::to_string(materials.size());
        materials.push_back(makeMaterial(m, name.c_str()));
    }
    if (materials.empty()) materials = m_propMaterials;

    m_models.reserve(kinds);
    for (size_t k = 0; k < kinds; ++k) {
        MeshHandle mesh = requestCookedMeshAsync(usable[k], *m_resources);
        if (!mesh) continue;
        m_models.push_back(ModelKind{mesh, {}, {}, false});
        ++m_unfittedKinds;
    }

    if (m_models.empty()) {
        STRESS_LOG("cooked meshes present but none resolved - procedural props only");
        return;
    }

    for (int i = 0; i < modelInstances; ++i) {
        ModelKind& kind = m_models[static_cast<size_t>(i) % m_models.size()];

        // Offset the index so models interleave with the props rather than
        // landing on the same golden-angle spiral and shadowing them.
        const glm::vec3 spot = scatterOnGround(i * 3 + 1, modelInstances * 3,
                                              PIT_HALF + 8.0f, PROP_ZONE_OUTER, m_rng);
        const float size = frand(2.5f, 7.0f);

        Entity entity = spawnMesh(kind.mesh,
            materials[static_cast<size_t>(i) % materials.size()], "Model",
            {spot.x, GROUND_Y, spot.z}, glm::vec3(1.0f));

        m_scene->get<Transform>(entity).rotation =
            glm::angleAxis(frand(0.0f, glm::two_pi<float>()), Math::WORLD_AXIS_Y);

        kind.instances.push_back(entity);
        kind.sizes.push_back(size);
    }

    STRESS_LOG("models: %zu kinds, %d instances, %zu textured materials",
               m_models.size(), modelInstances, materials.size());
}

void StressArena::updateModelScales() {
    // Every kind is fitted within the first seconds and never again, so this
    // drops out of the frame entirely rather than rescanning the list forever.
    if (m_unfittedKinds == 0) return;

    // A cooked mesh is handed back as an empty stub and filled in off-thread, so
    // its bounds are only known some frames after the build. Until then an
    // instance cannot be scaled sensibly - Sponza's meshes are authored at wildly
    // different extents, and a fixed scale would leave some invisible and others
    // swallowing the arena. So each kind is fitted once, the frame its vertices
    // land, and then left alone.
    for (ModelKind& kind : m_models) {
        if (kind.fitted) continue;

        const MeshAsset& asset = m_resources->get(kind.mesh);
        if (asset.loading || asset.vertices.empty()) continue;

        const glm::vec3 extent = asset.boundsMax - asset.boundsMin;
        const float     longest = std::max({extent.x, extent.y, extent.z});
        if (longest <= 1e-4f) { kind.fitted = true; --m_unfittedKinds; continue; }

        for (size_t i = 0; i < kind.instances.size(); ++i) {
            if (!m_scene->isAlive(kind.instances[i])) continue;

            Transform& transform = m_scene->get<Transform>(kind.instances[i]);
            const float scale = kind.sizes[i] / longest;
            transform.scale = glm::vec3(scale);
            // Sit the model on the ground: its local origin is wherever the
            // exporter left it, so lift by the scaled distance to its underside.
            transform.position.y = GROUND_Y - asset.boundsMin.y * scale;
        }
        kind.fitted = true;
        --m_unfittedKinds;
    }
}

void StressArena::buildDrones() {
    m_drones.reserve(static_cast<size_t>(droneCount));

    for (int i = 0; i < droneCount; ++i) {
        const float radius = frand(PIT_HALF + 10.0f, PROP_ZONE_OUTER);
        const float height = frand(10.0f, 30.0f);

        // Body: the only part this behavior moves.
        Entity body = spawnMesh(m_cube, m_matChrome, "Drone",
                                {radius, height, 0.0f}, {1.6f, 0.5f, 2.4f});

        // Arm, then rotor: the chain exists so the rig is three deep. The
        // scattered props are all hierarchy roots, so without something like
        // this the depth-bucketed resolve in HierarchySystem never runs on
        // anything but depth 0 and its cost stays invisible.
        Entity arm = spawnMesh(m_cube, m_matTower, "Drone Arm",
                               {0.0f, 0.0f, 0.0f}, {0.25f, 0.9f, 0.25f});
        m_scene->get<Transform>(arm).position = {0.0f, 0.6f, 0.0f};
        HierarchyOperations::setParent(*m_scene, arm.getID(), body.getID());

        Entity rotor = spawnMesh(m_cylinder, m_matEmissive, "Drone Rotor",
                                 {0.0f, 0.0f, 0.0f}, {2.6f, 0.08f, 2.6f});
        m_scene->get<Transform>(rotor).position = {0.0f, 0.55f, 0.0f};
        HierarchyOperations::setParent(*m_scene, rotor.getID(), arm.getID());
        // Spun by the AnimationSystem, not by hand: an animated node inside a
        // moved subtree is the realistic case, and it dirties the chain from
        // two different sources in the same frame.
        m_scene->add(rotor, makeSpin(0.35f, frand(0.0f, 0.35f)));

        // A quarter carry a downward spot. These are the only shadow casters in
        // the scene that move, so their atlas tile re-renders a different
        // frustum every frame rather than the same one.
        Drone drone{body, Entity{}, radius, height,
                    frand(0.12f, 0.42f) * (i % 2 == 0 ? 1.0f : -1.0f),
                    frand(0.0f, glm::two_pi<float>())};
        if (i % 4 == 0) {
            Entity lamp = m_scene->createEntity();
            m_scene->add(lamp, makeName("Drone Lamp"));

            Transform lampTransform;
            lampTransform.position = {0.0f, -0.4f, 0.0f};
            // Point straight down: +Z is forward, so a +90 degree X rotation
            // tips it from horizontal to floorward.
            lampTransform.rotation = glm::angleAxis(glm::half_pi<float>(), Math::WORLD_AXIS_X);
            m_scene->add(lamp, std::move(lampTransform));

            Light spot;
            spot.type           = LightType::Spot;
            spot.color          = {1.0f, 0.85f, 0.6f};
            spot.intensity      = 30.0f;
            spot.radius         = 45.0f;
            spot.innerConeAngle = 0.25f;
            spot.outerConeAngle = 0.5f;
            spot.castShadows    = false;   // promoted below, within the atlas budget
            m_scene->add(lamp, std::move(spot));

            HierarchyOperations::setParent(*m_scene, lamp.getID(), body.getID());
            drone.lamp = lamp;
        }

        m_drones.push_back(drone);
    }

    // Hand a couple of atlas tiles to moving casters. Taken from the budget
    // rather than added to it, so toggling shadows still measures the same
    // number of tiles - only now some of them move.
    int promoted = 0;
    for (Drone& drone : m_drones) {
        if (promoted >= 2) break;
        if (!drone.lamp || !m_scene->isAlive(drone.lamp)) continue;
        m_scene->get<Light>(drone.lamp).castShadows = true;
        ++promoted;
    }
}

void StressArena::updatePatrolLights() {
    for (PatrolLight& light : m_patrol) {
        if (!m_scene->isAlive(light.entity)) continue;

        const float angle = light.phase + m_motionTime * light.speed;
        const glm::vec3 position(
            std::cos(angle) * light.radius,
            GROUND_Y + light.height + std::sin(angle * 2.3f) * light.bobAmp,
            std::sin(angle) * light.radius);

        m_scene->get<Transform>(light.entity).position = position;
        if (m_scene->isAlive(light.fixture)) {
            m_scene->get<Transform>(light.fixture).position = position;
        }
    }
}

void StressArena::updateDrones() {
    for (Drone& drone : m_drones) {
        if (!m_scene->isAlive(drone.body)) continue;

        const float angle = drone.phase + m_motionTime * drone.speed;
        Transform& transform = m_scene->get<Transform>(drone.body);
        transform.position = {
            std::cos(angle) * drone.radius,
            GROUND_Y + drone.height + std::sin(angle * 1.9f) * 2.5f,
            std::sin(angle) * drone.radius
        };
        // Bank into the turn and face along the tangent. +Z forward again, so
        // the yaw is measured from +Z rather than the -Z most code assumes.
        transform.rotation =
            glm::angleAxis(-angle, Math::WORLD_AXIS_Y) *
            glm::angleAxis(std::sin(angle * 1.9f) * 0.25f, Math::WORLD_AXIS_Z);

        // The subtree is stale until this is called - the arm, rotor and lamp
        // all hang off this transform, and nothing else marks it.
        HierarchyOperations::markDirty(*m_scene, drone.body.getID());
    }
}

void StressArena::updateDebris(float dt) {
    // Age out the live pieces first, so a piece spawned this frame is not
    // immediately considered for destruction.
    for (size_t i = 0; i < m_debris.size();) {
        Debris& piece = m_debris[i];
        piece.life -= dt;

        if (piece.life > 0.0f && m_scene->isAlive(piece.entity)) {
            ++i;
            continue;
        }

        if (m_scene->isAlive(piece.entity)) destroy(piece.entity.getID());
        // Swap-and-pop: order carries no meaning here and this keeps the
        // per-frame cost flat no matter how many are live.
        m_debris[i] = m_debris.back();
        m_debris.pop_back();
    }

    if (debrisRate <= 0.0f) return;

    // Cap the live set so a high rate cannot run the entity count away and turn
    // the capture into a memory test instead of a churn test.
    constexpr size_t MAX_LIVE = 900;

    m_debrisAccum += debrisRate * dt;
    while (m_debrisAccum >= 1.0f) {
        // Keep only the fraction while capped, matching ParticleSystem: banking
        // whole spawns at the cap discharges them the moment pieces start
        // expiring. The shipped rate stays under one per frame so it never
        // reaches that, but debrisRate is a dial.
        if (m_debris.size() >= MAX_LIVE) {
            m_debrisAccum -= std::floor(m_debrisAccum);
            break;
        }
        m_debrisAccum -= 1.0f;

        const float angle  = m_churnRng.nextFloat(0.0f, glm::two_pi<float>());
        const float radius = m_churnRng.nextFloat(PIT_HALF, PROP_ZONE_OUTER);
        const float size   = m_churnRng.nextFloat(0.25f, 0.8f);

        // The shared cube: the churn being measured is entity lifetime, not
        // geometry upload, so every piece batches with the props.
        Entity piece = spawnMesh(m_cube,
            m_propMaterials[static_cast<size_t>(m_debris.size()) % m_propMaterials.size()],
            "Debris",
            {std::cos(angle) * radius,
             GROUND_Y + m_churnRng.nextFloat(14.0f, 26.0f),
             std::sin(angle) * radius},
            glm::vec3(size));

        // Shadow-casting is off: a piece that lives two seconds is not worth a
        // shadow re-render, and this keeps the churn measuring the ECS and the
        // draw list rather than the shadow pass.
        m_scene->get<Mesh>(piece).castShadows = false;

        Rigidbody rb;
        rb.mass            = size * 4.0f;
        rb.restitution     = 0.4f;
        rb.friction        = 0.4f;
        rb.canSleep        = false;
        rb.linearVelocity  = {m_churnRng.nextFloat(-6.0f, 6.0f),
                              m_churnRng.nextFloat(-2.0f, 4.0f),
                              m_churnRng.nextFloat(-6.0f, 6.0f)};
        rb.angularVelocity = {m_churnRng.nextFloat(-6.0f, 6.0f),
                              m_churnRng.nextFloat(-6.0f, 6.0f),
                              m_churnRng.nextFloat(-6.0f, 6.0f)};
        m_scene->add(piece, std::move(rb));

        Collider col;
        col.parts   = {ColliderBox{{0.0f, 0.0f, 0.0f}, glm::vec3(size * 0.5f)}};
        col.enabled = m_physicsOn;   // spawned mid-toggle, so honour the current state
        m_scene->add(piece, std::move(col));

        m_debris.push_back(Debris{piece, m_churnRng.nextFloat(2.5f, 5.0f)});
    }
}

void StressArena::updateBlast(float dt) {
    if (blastInterval <= 0.0f || !m_physicsOn) return;

    m_blastTimer -= dt;
    if (m_blastTimer > 0.0f) return;
    m_blastTimer = blastInterval;

    // Radial impulse from the pit floor. Without this the pile packs down, its
    // contacts stabilise, and the solver coasts - so the physics zone would
    // report the cost of a settled stack rather than a working one.
    const glm::vec3 origin(0.0f, GROUND_Y - PIT_DEPTH, 0.0f);
    for (Entity body : m_bodies) {
        if (!m_scene->isAlive(body)) continue;

        const glm::vec3 offset = m_scene->get<Transform>(body).position - origin;
        const float     dist   = std::max(1.0f, glm::length(offset));

        Rigidbody& rb = m_scene->get<Rigidbody>(body);
        rb.linearVelocity  += (offset / dist) * (28.0f / dist) + glm::vec3(0.0f, 9.0f, 0.0f);
        rb.angularVelocity += glm::vec3(m_churnRng.nextFloat(-5.0f, 5.0f),
                                        m_churnRng.nextFloat(-5.0f, 5.0f),
                                        m_churnRng.nextFloat(-5.0f, 5.0f));
        rb.sleeping = false;
    }
}

void StressArena::updateMaterialPulse() {
    if (pulsingMaterials <= 0 || m_propMaterials.empty()) return;

    const int count = std::min(pulsingMaterials, static_cast<int>(m_propMaterials.size()));
    for (int i = 0; i < count; ++i) {
        const MaterialHandle handle = m_propMaterials[static_cast<size_t>(i)];
        const float phase = m_motionTime * 2.0f + static_cast<float>(i) * 0.7f;
        const float glow  = 0.5f + 0.5f * std::sin(phase);

        MaterialAsset& material = m_resources->edit(handle);
        material.emission         = glm::vec3(0.9f, 0.45f, 0.15f) * glow;
        material.emissiveStrength = 1.0f + glow * 3.0f;
        // commit bumps the version, which is what makes GLView re-upload this
        // material's UBO next sync. That path is dead in a scene of fixed
        // materials and hot in any game that pulses, flashes or fades.
        m_resources->commit(handle);
    }
}

void StressArena::buildUI() {
    Entity canvas = m_scene->createEntity();
    m_scene->add(canvas, makeName("HUD"));
    m_scene->add(canvas, UICanvas{});
    m_hudCanvas = canvas.getID();

    // The two readout lines, rewritten once a second in refreshUI. Anchored to
    // the top-left so they stay clear of the toggle grid on the right.
    auto addLine = [&](const char* name, float y, float pixelSize, const glm::vec4& color) {
        Entity line = m_scene->createEntity();
        m_scene->add(line, makeName(name));

        UIElement element;
        element.anchor   = {0.0f, 0.0f};
        element.pivot    = {0.0f, 0.0f};
        element.position = {24.0f, y};
        element.size     = {900.0f, 40.0f};
        m_scene->add(line, std::move(element));

        UIText text;
        text.text      = "warming up";
        text.pixelSize = pixelSize;
        text.color     = color;
        m_scene->add(line, std::move(text));

        HierarchyOperations::setParent(*m_scene, line.getID(), canvas.getID());
        return line;
    };

    m_uiStats   = addLine("Stats",   18.0f, 28.0f, {0.85f, 0.92f, 1.00f, 1.0f});
    m_uiToggles = addLine("Toggles", 56.0f, 18.0f, {0.70f, 0.78f, 0.90f, 0.95f});

    // Filler widgets. These exist to give the UI layout walk and the UI draw
    // pass real work - a HUD of two labels measures nothing.
    m_uiWidgets.reserve(static_cast<size_t>(uiWidgetCount));
    for (int i = 0; i < uiWidgetCount; ++i) {
        Entity widget = m_scene->createEntity();
        m_scene->add(widget, makeName("Widget"));

        UIElement element;
        element.anchor   = {1.0f, 0.0f};
        element.pivot    = {1.0f, 0.0f};
        element.position = {-20.0f - static_cast<float>(i % 6) * 108.0f,
                             20.0f + static_cast<float>(i / 6) * 40.0f};
        element.size     = {100.0f, 32.0f};
        m_scene->add(widget, std::move(element));

        // Alternate image and text so both UI draw paths carry load.
        if (i % 2 == 0) {
            UIImage image;
            image.color = {frand(0.2f, 0.9f), frand(0.2f, 0.9f), frand(0.4f, 1.0f), 0.55f};
            m_scene->add(widget, std::move(image));
        } else {
            UIText text;
            text.text      = "SLOT " + std::to_string(i);
            text.pixelSize = 18.0f;
            text.color     = {0.75f, 0.85f, 1.0f, 0.9f};
            m_scene->add(widget, std::move(text));
        }

        HierarchyOperations::setParent(*m_scene, widget.getID(), canvas.getID());
        m_uiWidgets.push_back(widget);
    }
}

void StressArena::onUpdate(float dt) {
    // One clock for every scripted motion, so the whole scene stays phase-locked
    // and two runs of the same build are frame-comparable.
    m_motionTime += dt;

    readInput();
    updateCamera(dt);
    updatePatrolLights();
    updateDrones();
    updateDebris(dt);
    updateBlast(dt);
    updateMaterialPulse();
    updatePhysics();
    updateModelScales();
    refreshUI(dt);
}

void StressArena::readInput() {
    const InputMap& input = *context().input;

    // Edges come from the map, which samples once a frame for every reader, so
    // there is no per-key "was it down" state to keep here.
    if (input.pressed(ACTION_LIGHTS))    { m_lightsOn     = !m_lightsOn;     setLightsEnabled(m_lightsOn); }
    if (input.pressed(ACTION_SHADOWS))   { m_shadowsOn    = !m_shadowsOn;    setShadowsEnabled(m_shadowsOn); }
    if (input.pressed(ACTION_PROPS))     { m_propsOn      = !m_propsOn;      setPropsVisible(m_propsOn); }
    if (input.pressed(ACTION_PARTICLES)) { m_particlesOn  = !m_particlesOn;  setParticlesEnabled(m_particlesOn); }
    if (input.pressed(ACTION_PHYSICS))   { m_physicsOn    = !m_physicsOn;    setPhysicsEnabled(m_physicsOn); }
    if (input.pressed(ACTION_ANIM))      { m_animationsOn = !m_animationsOn; setAnimationsEnabled(m_animationsOn); }
    if (input.pressed(ACTION_DECALS))    { m_decalsOn     = !m_decalsOn;     setDecalsEnabled(m_decalsOn); }
    if (input.pressed(ACTION_FOG)) {
        m_fogOn = !m_fogOn;
        m_scene->environment().fogEnabled = m_fogOn;
    }
    if (input.pressed(ACTION_UI)) { m_uiOn = !m_uiOn; setUIVisible(m_uiOn); }

    // 0 restores everything, so a capture can be returned to the reference load
    // without restarting play.
    if (input.pressed(ACTION_RESET)) {
        m_lightsOn = m_shadowsOn = m_propsOn = m_particlesOn = true;
        m_physicsOn = m_animationsOn = m_decalsOn = m_fogOn = m_uiOn = true;
        setLightsEnabled(true);
        setShadowsEnabled(true);
        setPropsVisible(true);
        setParticlesEnabled(true);
        setPhysicsEnabled(true);
        setAnimationsEnabled(true);
        setDecalsEnabled(true);
        setUIVisible(true);
        m_scene->environment().fogEnabled = true;
    }

    // F hands the camera to the engine's free-fly controller and back.
    if (input.pressed(ACTION_CAMERA)) {
        scriptedCamera = !scriptedCamera;
        STRESS_LOG("camera: %s", scriptedCamera ? "scripted" : "free");
    }
}

void StressArena::updateCamera(float dt) {
    if (!m_camera || !m_scene->isAlive(m_camera)) return;
    if (!scriptedCamera) return;

    m_camTime += dt;

    // A cut jumps most of the way round the loop at once: nothing visible before
    // is visible after, so the culling sets, the batch membership and every
    // newly-referenced mesh and material all turn over in a single frame. That
    // is the worst case for frame-to-frame coherence and shows up as a
    // deliberate spike, which is why it is off unless asked for.
    if (cameraCutInterval > 0.0f) {
        m_cutTimer -= dt;
        if (m_cutTimer <= 0.0f) {
            m_cutTimer = cameraCutInterval;
            m_camTime += cameraLoopTime * 0.41f;   // not a clean fraction, so cuts do not repeat a pose
        }
    }

    const float loop = (cameraLoopTime > 0.1f) ? cameraLoopTime : 0.1f;
    const float t    = m_camTime / loop * glm::two_pi<float>();

    // Circle the block while the height oscillates at a different rate, so the
    // path does not repeat exactly each lap until both cycles realign - one
    // capture therefore covers street level and overview without a cut.
    const glm::vec3 position(
        std::cos(t) * CAM_RADIUS,
        GROUND_Y + CAM_HEIGHT + std::sin(t * 1.7f) * CAM_HEIGHT_AMP,
        std::sin(t) * CAM_RADIUS);

    const glm::vec3 target(0.0f, GROUND_Y + CAM_LOOK_Y, 0.0f);
    const glm::vec3 forward = glm::normalize(target - position);

    // Keep the focal plane on whatever the camera is looking at, the way a game
    // camera would, so the in-focus band moves through the scene over the loop
    // instead of sitting at a fixed depth the whole capture.
    if (m_scene->has<Camera>(m_camera)) {
        m_scene->get<Camera>(m_camera).focusDistance = glm::distance(position, target);
    }

    Transform& transform = m_scene->get<Transform>(m_camera);
    transform.position = position;
    // Negated on purpose. glm::quatLookAt builds a rotation for GLM's -Z
    // forward, but this engine's forward is +Z (Math::computeForward), so the
    // unnegated result aims the camera directly away from the target - here,
    // out at the tower ring a few units behind it instead of across the block.
    transform.rotation = glm::quatLookAt(-forward, Math::WORLD_AXIS_Y);
}

void StressArena::updatePhysics() {
    if (!m_physicsOn) return;

    // Relaunch anything that has left the pit or come to rest at the bottom.
    // The pile must stay agitated: a settled stack drops out of the solver's
    // active set, and the physics zone quietly stops measuring anything.
    for (Entity body : m_bodies) {
        if (!m_scene->isAlive(body)) continue;

        Transform& transform = m_scene->get<Transform>(body);
        const bool escaped = std::abs(transform.position.x) > PIT_HALF + 6.0f ||
                             std::abs(transform.position.z) > PIT_HALF + 6.0f ||
                             transform.position.y < GROUND_Y - PIT_DEPTH - 6.0f;
        if (!escaped) continue;

        transform.position = {
            m_churnRng.nextFloat(-PIT_HALF + 2.0f, PIT_HALF - 2.0f),
            GROUND_Y + m_churnRng.nextFloat(25.0f, 45.0f),
            m_churnRng.nextFloat(-PIT_HALF + 2.0f, PIT_HALF - 2.0f)
        };

        Rigidbody& rb = m_scene->get<Rigidbody>(body);
        rb.linearVelocity  = {m_churnRng.nextFloat(-2.0f, 2.0f), 0.0f,
                              m_churnRng.nextFloat(-2.0f, 2.0f)};
        rb.angularVelocity = {m_churnRng.nextFloat(-3.0f, 3.0f),
                              m_churnRng.nextFloat(-3.0f, 3.0f),
                              m_churnRng.nextFloat(-3.0f, 3.0f)};
        rb.sleeping = false;
    }
}

void StressArena::refreshUI(float dt) {
    ++m_frames;
    m_statsTimer += dt;
    if (m_statsTimer < 1.0f) return;

    const float fps = static_cast<float>(m_frames) / m_statsTimer;
    const float ms  = 1000.0f / (fps > 0.001f ? fps : 0.001f);
    m_statsTimer = 0.0f;
    m_frames     = 0;

    // Tracy owns the real numbers. These lines exist so a capture can be read
    // back against the load that produced it: at a glance, which subsystems
    // were in the frame and whether the camera was on its scripted path.
    char buffer[256];

    if (m_scene->isAlive(m_uiStats)) {
        // Entity count and live debris are here because they are the two numbers
        // that move on their own: if a capture looks off, the first question is
        // whether the churn was actually running at the rate it claims.
        std::snprintf(buffer, sizeof(buffer),
            "%.1f fps   %.2f ms   %zu entities   %zu debris   camera:%s",
            static_cast<double>(fps), static_cast<double>(ms),
            m_scene->entityCount(), m_debris.size(),
            scriptedCamera ? "scripted [F]" : "free [F]");
        m_scene->get<UIText>(m_uiStats).text = buffer;
    }

    if (m_scene->isAlive(m_uiToggles)) {
        std::snprintf(buffer, sizeof(buffer),
            "1 light:%s  2 shadow:%s  3 props:%s  4 fx:%s  5 phys:%s  "
            "6 anim:%s  7 decal:%s  8 fog:%s  9 ui:%s  0 reset",
            m_lightsOn ? "on" : "OFF", m_shadowsOn ? "on" : "OFF",
            m_propsOn ? "on" : "OFF", m_particlesOn ? "on" : "OFF",
            m_physicsOn ? "on" : "OFF", m_animationsOn ? "on" : "OFF",
            m_decalsOn ? "on" : "OFF", m_fogOn ? "on" : "OFF",
            m_uiOn ? "on" : "OFF");
        m_scene->get<UIText>(m_uiToggles).text = buffer;
    }
}

void StressArena::setLightsEnabled(bool enabled) {
    for (Entity light : m_lights) {
        if (m_scene->isAlive(light)) m_scene->get<Light>(light).enabled = enabled;
    }
    // Drone lamps live on the rigs, not in m_lights, and would otherwise stay
    // lit with the toggle reading "off".
    for (Drone& drone : m_drones) {
        if (drone.lamp && m_scene->isAlive(drone.lamp)) {
            m_scene->get<Light>(drone.lamp).enabled = enabled;
        }
    }
}

void StressArena::setShadowsEnabled(bool enabled) {
    // Only the lights that were built as casters are restored, so toggling
    // shadows back on cannot silently promote all lightCount lights to casters
    // and change the load being measured.
    for (size_t i = 0; i < m_lights.size(); ++i) {
        if (!m_scene->isAlive(m_lights[i])) continue;
        const bool caster = (static_cast<int>(i) < shadowLights) || (i + 1 == m_lights.size());
        m_scene->get<Light>(m_lights[i]).castShadows = enabled && caster;
    }
    // The moving casters are the interesting half of the shadow cost, so they
    // have to follow the same toggle. Which drones were promoted is recorded by
    // their current flag, so only re-enable the two that already had it.
    int promoted = 0;
    for (Drone& drone : m_drones) {
        if (!drone.lamp || !m_scene->isAlive(drone.lamp)) continue;
        if (promoted >= 2) break;
        m_scene->get<Light>(drone.lamp).castShadows = enabled;
        ++promoted;
    }
}

void StressArena::setPropsVisible(bool visible) {
    for (Entity prop : m_props) {
        if (m_scene->isAlive(prop)) m_scene->get<Mesh>(prop).visible = visible;
    }
}

void StressArena::setParticlesEnabled(bool enabled) {
    for (Entity emitter : m_emitters) {
        if (m_scene->isAlive(emitter)) m_scene->get<ParticleEmitter>(emitter).emitting = enabled;
    }
}

void StressArena::setPhysicsEnabled(bool enabled) {
    // Disabling the collider drops the body out of the broadphase entirely,
    // which is what takes the solver cost to zero; leaving the Rigidbody alone
    // means re-enabling resumes from the pose it stopped at.
    for (Entity body : m_bodies) {
        if (m_scene->isAlive(body)) m_scene->get<Collider>(body).enabled = enabled;
    }
    // Debris carries bodies too. New pieces read m_physicsOn at spawn, so this
    // only has to cover the ones already live.
    for (Debris& piece : m_debris) {
        if (m_scene->isAlive(piece.entity)) m_scene->get<Collider>(piece.entity).enabled = enabled;
    }
}

void StressArena::setAnimationsEnabled(bool enabled) {
    for (Entity spinner : m_spinners) {
        if (m_scene->isAlive(spinner)) m_scene->get<Animation>(spinner).playing = enabled;
    }
    // Rotors are animated but are not props, so they are not in m_spinners.
    for (Drone& drone : m_drones) {
        if (!m_scene->isAlive(drone.body)) continue;
        HierarchyOperations::forEachChild(*m_scene, drone.body.getID(), [&](EntityId arm) {
            HierarchyOperations::forEachChild(*m_scene, arm, [&](EntityId rotor) {
                if (m_scene->has<Animation>(rotor)) m_scene->get<Animation>(rotor).playing = enabled;
            });
        });
    }
}

void StressArena::setDecalsEnabled(bool enabled) {
    // Decal carries no enable flag, so the component itself comes and goes -
    // which is the only way to take the projector out of the pass rather than
    // just making it invisible while still paying for it.
    for (Entity decal : m_decals) {
        if (!m_scene->isAlive(decal)) continue;

        if (enabled && !m_scene->has<Decal>(decal)) {
            Decal component;
            component.material  = m_matDecal;
            component.angleFade = 0.6f;
            component.opacity   = 0.7f;
            m_scene->add(decal, std::move(component));
        } else if (!enabled && m_scene->has<Decal>(decal)) {
            m_scene->remove<Decal>(decal);
        }
    }
}

void StressArena::setUIVisible(bool visible) {
    if (m_hudCanvas && m_scene->isAlive(m_hudCanvas)) {
        m_scene->get<UICanvas>(m_hudCanvas).visible = visible;
    }
}

} // namespace Engine

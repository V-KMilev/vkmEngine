#define VKM_LOG_CATEGORY "STRESS"

#include "stress_arena.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/math/axes.h"
#include "core/math/easing.h"
#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "io/asset/asset_library.h"
#include "io/asset/cooked_loader.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/hierarchy_operations.h"
#include "platform/window/glfw_include.h"
#include "platform/input/input_map.h"
#include "platform/input/input_handle.h"
#include "resource/generate/mesh_generators.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"

namespace Arena {

namespace {

// A square city block centred on the origin; everything is placed inside this radius.
constexpr float ARENA_HALF = 150.0f;
constexpr float GROUND_Y   = 0.0f;

// The pit the physics pile falls into, at the centre of the block.
constexpr float PIT_HALF  = 14.0f;
constexpr float PIT_DEPTH = 3.0f;

constexpr float GROUND_BAND = (ARENA_HALF - PIT_HALF) * 0.5f;

// Placement zones, outward. The camera flies the clear lane between props and
// towers; a path through the buildings would put the near plane inside walls.
constexpr float PROP_ZONE_INNER  = PIT_HALF + 6.0f;
constexpr float PROP_ZONE_OUTER  = 92.0f;
constexpr float TOWER_ZONE_INNER = 118.0f;
constexpr float TOWER_ZONE_OUTER = ARENA_HALF - 8.0f;

// The height oscillates between street level (overdraw, many lights) and a raised
// view (draw count, deep clusters).
constexpr float CAM_RADIUS     = 104.0f;
constexpr float CAM_HEIGHT     = 26.0f;
constexpr float CAM_HEIGHT_AMP = 11.0f;
// Low enough that the scene, not the sky, fills the frame.
constexpr float CAM_LOOK_Y     = 4.0f;

// 2D atlas tiles left after the sun's cascades, for the moving drone spots.
constexpr int MOVING_SHADOW_CASTERS =
    static_cast<int>(Config::MAX_SHADOW_CASTERS_2D) - static_cast<int>(Config::NUM_CASCADES);

constexpr uint64_t ARENA_SEED = 0xC0FFEEu;
// Second PCG stream for the runtime churn, so it cannot disturb the layout draw.
constexpr uint64_t CHURN_STREAM = 0x51ED2701u;

constexpr const char* ACTION_LIGHTS    = "Toggle/Lights";
constexpr const char* ACTION_SHADOWS   = "Toggle/Shadows";
constexpr const char* ACTION_PROPS     = "Toggle/Props";
constexpr const char* ACTION_PARTICLES = "Toggle/Particles";
constexpr const char* ACTION_PHYSICS   = "Toggle/Physics";
constexpr const char* ACTION_ANIM      = "Toggle/Animation";
constexpr const char* ACTION_DECALS    = "Toggle/Decals";
constexpr const char* ACTION_FOG       = "Toggle/Fog";
constexpr const char* ACTION_UI        = "Toggle/UI";
constexpr const char* ACTION_RESET     = "Toggle/Reset";
constexpr const char* ACTION_CAMERA    = "Toggle/Camera";

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

// Generated paving: the arena ships no source art.
constexpr uint32_t GROUND_TEXTURE_SIZE = 512;
constexpr uint32_t GROUND_SLABS        = 4;      ///< Per texture edge.
constexpr float    GROUND_TILE_WORLD   = 7.5f;   ///< World units per repeat.

/**
 * @brief Generate the ground's albedo map: paving slabs, grout and grain.
 *
 * Drawn from the arena's fixed seed, so every run gets the same pixels.
 *
 * @return An sRGB RGBA8 texture that tiles seamlessly, with mipmaps requested.
 */
TextureAsset makeGroundTexture() {
    constexpr uint32_t SIZE  = GROUND_TEXTURE_SIZE;
    constexpr uint32_t SLAB  = SIZE / GROUND_SLABS;
    constexpr uint32_t GROUT = 4;

    TextureAsset texture;
    texture.params.width          = SIZE;
    texture.params.height         = SIZE;
    texture.params.internalFormat = TextureInternalFormat::SRGBA8;
    texture.params.format         = TexturePixelFormat::RGBA;
    texture.params.type           = TexturePixelType::UnsignedByte;
    texture.params.wrapS          = TextureWrapMode::Repeat;
    texture.params.wrapT          = TextureWrapMode::Repeat;
    texture.pixelData.resize(static_cast<size_t>(SIZE) * SIZE * 4);

    Math::Rng rng(ARENA_SEED);
    float slabTint[GROUND_SLABS * GROUND_SLABS];
    for (float& tint : slabTint) tint = rng.nextFloat(0.38f, 0.54f);

    for (uint32_t y = 0; y < SIZE; ++y) {
        for (uint32_t x = 0; x < SIZE; ++x) {
            const bool grout = (x % SLAB) < GROUT || (y % SLAB) < GROUT;
            const float base = grout ? 0.13f : slabTint[(y / SLAB) * GROUND_SLABS + (x / SLAB)];
            const float value = glm::clamp(base + rng.nextFloat(-0.045f, 0.045f), 0.0f, 1.0f);

            // A hair warm, not a pure grey card.
            uint8_t* pixel = &texture.pixelData[(static_cast<size_t>(y) * SIZE + x) * 4];
            pixel[0] = static_cast<uint8_t>(value * 255.0f);
            pixel[1] = static_cast<uint8_t>(value * 251.0f);
            pixel[2] = static_cast<uint8_t>(value * 244.0f);
            pixel[3] = 255;
        }
    }
    return texture;
}

/**
 * @brief A ground slab's mesh: a cube whose UVs repeat the paving across it.
 *
 * The repeat count comes from the slab's size, because the slabs differ in shape.
 *
 * @param extents Full width, height and depth of the slab this mesh is for.
 * @return The unit cube with its UVs scaled to GROUND_TILE_WORLD per repeat.
 */
MeshAsset makeGroundMesh(const glm::vec3& extents) {
    MeshAsset mesh = generateCube();
    const glm::vec2 tiles(extents.x / GROUND_TILE_WORLD, extents.z / GROUND_TILE_WORLD);
    for (Vertex& vertex : mesh.vertices) vertex.uv *= tiles;
    return mesh;
}

/**
 * @brief Place item @p index of @p count evenly across a ground annulus.
 *
 * Golden-angle (Vogel) placement spreads points evenly by area; random placement
 * clumps at these counts. Jitter scales with local spacing, so neighbours never collide.
 *
 * @param index   Item index; consecutive values land far apart.
 * @param count   Total items sharing the annulus, which sets the spacing.
 * @param inner   Inner radius of the annulus.
 * @param outer   Outer radius.
 * @param rng     Source for the jitter.
 * @param jitter  Jitter as a fraction of local spacing (0 = a perfect lattice).
 * @return A ground-plane position; Y is left at zero for the caller to set.
 */
glm::vec3 scatterOnGround(
    int index,
    int count,
    float inner,
    float outer,
    Math::Rng& rng,
    float jitter = 0.4f
) {
    constexpr float GOLDEN_ANGLE = 2.39996323f;

    const int   total = std::max(1, count);
    const float u     = (static_cast<float>(index) + 0.5f) / static_cast<float>(total);
    const float radius = std::sqrt(inner * inner + u * (outer * outer - inner * inner));

    // Mean centre-to-centre distance at this density.
    const float spacing = (outer - inner) / std::sqrt(static_cast<float>(total));

    const float radial  = rng.nextFloat(-1.0f, 1.0f) * spacing * jitter;
    const float jittered = glm::clamp(radius + radial, inner, outer);
    // The same linear jitter as an angle, so spacing does not tighten toward the middle.
    const float angular = rng.nextFloat(-1.0f, 1.0f) * spacing * jitter / std::max(1.0f, jittered);

    const float theta = static_cast<float>(index) * GOLDEN_ANGLE + angular;
    return {std::cos(theta) * jittered, 0.0f, std::sin(theta) * jittered};
}

/**
 * @brief A looping spin about Y.
 *
 * Linear keys 120 degrees apart, so each slerp takes the short way at a constant rate.
 *
 * @param period Seconds per full revolution.
 * @param phase  Seconds into the loop it starts at.
 * @return A playing, looping Animation.
 */
Animation makeSpin(float period, float phase) {
    Animation anim;
    anim.rotationTrack.setEasing(Easing::Linear);
    for (int k = 0; k <= 3; ++k) {
        anim.rotationTrack.addKeyframe(
            period * static_cast<float>(k) / 3.0f,
            glm::angleAxis(glm::two_pi<float>() * static_cast<float>(k) / 3.0f, Math::WORLD_AXIS_Y)
        );
    }
    anim.time    = phase;
    anim.playing = true;
    anim.looping = true;
    return anim;
}

/**
 * @brief A looping vertical bob, to exercise the position track.
 *
 * @param period Seconds per rise and fall.
 * @param height Peak lift above the prop's resting position.
 * @param phase  Seconds into the loop it starts at.
 * @return A playing, looping Animation.
 */
Animation makeBob(float period, float height, float phase) {
    Animation anim;
    anim.positionTrack.setEasing(Easing::EaseInOutSine);
    anim.positionTrack.addKeyframe(0.0f,          {0.0f, 0.0f, 0.0f});
    anim.positionTrack.addKeyframe(period * 0.5f, {0.0f, height, 0.0f});
    anim.positionTrack.addKeyframe(period,        {0.0f, 0.0f, 0.0f});
    anim.time    = phase;
    anim.playing = true;
    anim.looping = true;
    return anim;
}

/**
 * @brief A breathing scale pulse.
 *
 * @param period Seconds per swell and return.
 * @param amount Peak growth, as a fraction of the prop's scale.
 * @param phase  Seconds into the loop it starts at.
 * @return A playing, looping Animation.
 */
Animation makePulse(float period, float amount, float phase) {
    Animation anim;
    anim.scaleTrack.setEasing(Easing::EaseInOutSine);
    anim.scaleTrack.addKeyframe(0.0f,          glm::vec3(1.0f));
    anim.scaleTrack.addKeyframe(period * 0.5f, glm::vec3(1.0f + amount));
    anim.scaleTrack.addKeyframe(period,        glm::vec3(1.0f));
    anim.time    = phase;
    anim.playing = true;
    anim.looping = true;
    return anim;
}

/**
 * @brief Position, rotation and scale tracks at once: the evaluator's worst case.
 *
 * @param period Seconds per circuit of the hop.
 * @param radius Radius of the circle the hops trace.
 * @param height Peak of each hop.
 * @param phase  Seconds into the loop it starts at.
 * @return A playing, looping Animation.
 */
Animation makeOrbitHop(float period, float radius, float height, float phase) {
    Animation anim;
    anim.positionTrack.setEasing(Easing::EaseInOutSine);
    anim.rotationTrack.setEasing(Easing::Linear);
    anim.scaleTrack.setEasing(Easing::EaseInOutSine);

    for (int k = 0; k <= 4; ++k) {
        const float t     = period * static_cast<float>(k) / 4.0f;
        const float angle = glm::half_pi<float>() * static_cast<float>(k);
        anim.positionTrack.addKeyframe(
            t,
            {std::cos(angle) * radius, (k % 2 == 0) ? 0.0f : height, std::sin(angle) * radius}
        );
    }
    for (int k = 0; k <= 3; ++k) {
        anim.rotationTrack.addKeyframe(
            period * static_cast<float>(k) / 3.0f,
            glm::angleAxis(glm::two_pi<float>() * static_cast<float>(k) / 3.0f, Math::WORLD_AXIS_Y)
        );
    }
    anim.scaleTrack.addKeyframe(0.0f,          glm::vec3(1.0f));
    anim.scaleTrack.addKeyframe(period * 0.25f, glm::vec3(0.8f, 1.25f, 0.8f));
    anim.scaleTrack.addKeyframe(period * 0.5f,  glm::vec3(1.0f));
    anim.scaleTrack.addKeyframe(period * 0.75f, glm::vec3(1.2f, 0.78f, 1.2f));
    anim.scaleTrack.addKeyframe(period,         glm::vec3(1.0f));

    anim.time    = phase;
    anim.playing = true;
    anim.looping = true;
    return anim;
}

} // namespace

void StressArena::onStart() {
    if (m_built) return;
    m_built = true;

    m_rng.seed(ARENA_SEED);
    installStressBindings(input());
    m_churnRng.seed(ARENA_SEED, CHURN_STREAM);

    // Procedural, not an HDR file, so the lighting is identical on every machine.
    Environment& environment = scene().environment();
    environment.sky.showSkybox = true;
    environment.sky.procedural = true;
    environment.sky.intensity  = 1.0f;

    // Off-axis, so tower shadows fall onto the block and the cascades span varied
    // depth. A large Mie term at this elevation washes the sky white.
    environment.sky.lightColor        = {1.0f, 0.96f, 0.90f};
    environment.sky.lightIntensity    = 3.2f;
    environment.sky.sunElevation      = 70.0f;
    environment.sky.sunAzimuth        = -145.0f;
    environment.sky.sunIntensity      = 22.0f;
    environment.sky.rayleigh          = 1.0f;
    environment.sky.mie               = 0.7f;
    environment.sky.mieG              = 0.76f;
    environment.sky.sunDiscIntensity  = 15.0f;

    // On: one of the heaviest passes. Thin enough to read as haze; the froxel grid
    // costs the same either way.
    environment.fog.enabled       = true;
    environment.fog.density       = 0.006f;
    environment.fog.height        = 18.0f;
    environment.fog.heightFalloff = 0.05f;
    environment.fog.anisotropy    = 0.7f;

    scene().physics().gravity = {0.0f, -18.0f, 0.0f};

    m_camera = findActiveCamera(scene());
    if (Camera* camera = scene().tryGet<Camera>(m_camera)) {
        camera->zFar  = CAMERA_FAR;
        camera->zNear = 0.2f;

        // The DoF pass early-outs at amount 0.
        camera->dofAmount     = 0.35f;
        camera->focusDistance = CAM_RADIUS;
    }

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

    // Casters are the built set: the static points plus the sun and the drone lamps.
    LOG_INFO(
        "built: %zu props, %zu lights (%zu shadowed, %zu moving), %zu emitters, "
        "%zu decals, %zu bodies, %zu animated, %zu drones, %d materials",
        m_props.size(),
        m_lights.size(),
        m_shadowCasters.size(),
        m_patrol.size(),
        m_emitters.size(),
        m_decals.size(),
        m_bodies.size(),
        m_spinners.size(),
        m_drones.size(),
        uniqueMaterials
    );
    LOG_INFO(
        "keys: 1 lights  2 shadows  3 props  4 particles  5 physics  "
        "6 anim  7 decals  8 fog  9 UI  0 all   F camera"
    );
}

MaterialHandle StressArena::makeMaterial(const MaterialAsset& source, const char* name) {
    MaterialAsset material = source;
    return resources().add(std::move(material), name);
}

void StressArena::buildMaterials() {
    m_cube       = resources().add(generateCube(), "stress:cube");
    m_sphere     = resources().add(generateSphere(24, 12), "stress:sphere");
    m_cylinder   = resources().add(generateCylinder(0.5f, 1.0f, 20), "stress:cylinder");

    // One mesh per ground slab shape, so the paving keeps the same world scale
    // across all four and meets cleanly at the seams.
    m_groundBand = resources().add(
        makeGroundMesh({ARENA_HALF * 2.0f, 1.0f, GROUND_BAND * 2.0f}),
        "stress:ground_band"
    );
    m_groundSide = resources().add(
        makeGroundMesh({GROUND_BAND * 2.0f, 1.0f, PIT_HALF * 2.0f}),
        "stress:ground_side"
    );

    m_sphereMid = resources().add(generateSphere(12, 6), "stress:sphere_mid");
    m_sphereLow = resources().add(generateSphere(6, 4),  "stress:sphere_low");
    m_cylMid    = resources().add(generateCylinder(0.5f, 1.0f, 10),  "stress:cyl_mid");
    m_cylLow    = resources().add(generateCylinder(0.5f, 1.0f, 6),   "stress:cyl_low");

    // Mid-grey, roughly 40-55% albedo: dark surfaces would hide the shadowing and GI.
    MaterialAsset base;
    base.roughness = 0.85f;
    base.metallic  = 0.0f;

    // White: the scalar multiplies the map, so anything darker would tint it.
    MaterialAsset ground = base;
    ground.albedo        = {1.0f, 1.0f, 1.0f, 1.0f};
    ground.albedoTexture = resources().add(makeGroundTexture(), "stress:ground_albedo");
    m_matGround = makeMaterial(ground, "stress:ground");

    base.albedo    = {0.55f, 0.54f, 0.51f, 1.0f};
    base.roughness = 0.7f;
    m_matTower = makeMaterial(base, "stress:tower");

    // A share of the palette carries clearcoat, anisotropy or sheen, which the shader branches on.
    const int paletteSize = std::max(1, uniqueMaterials);
    m_propMaterials.reserve(static_cast<size_t>(paletteSize));
    for (int i = 0; i < paletteSize; ++i) {
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

    // Forces the sorted transparent queue and the transmission path.
    MaterialAsset glass;
    glass.type            = MaterialType::Transparent;
    glass.albedo          = {0.75f, 0.85f, 0.95f, 0.32f};
    glass.roughness       = 0.08f;
    glass.metallic        = 0.0f;
    glass.transmission    = 0.85f;
    glass.ior             = 1.45f;
    glass.thicknessFactor = 0.4f;
    m_matGlass = makeMaterial(glass, "stress:glass");

    MaterialAsset chrome;
    chrome.albedo    = {0.92f, 0.93f, 0.96f, 1.0f};
    chrome.metallic  = 1.0f;
    chrome.roughness = 0.06f;
    m_matChrome = makeMaterial(chrome, "stress:chrome");

    // Bright enough to clear the bloom threshold in daylight.
    MaterialAsset emissive;
    emissive.albedo           = {1.0f, 0.88f, 0.62f, 1.0f};
    emissive.emission         = {1.0f, 0.80f, 0.45f};
    emissive.emissiveStrength = 6.0f;
    emissive.roughness        = 0.4f;
    m_matEmissive = makeMaterial(emissive, "stress:emissive");

    // The projector blends on albedo alpha, so it must be < 1.
    MaterialAsset decal;
    decal.type      = MaterialType::Transparent;
    decal.albedo    = {0.05f, 0.06f, 0.09f, 0.8f};
    decal.roughness = 0.9f;
    m_matDecal = makeMaterial(decal, "stress:decal");
}

EntityId StressArena::spawnMesh(
    MeshHandle mesh,
    MaterialHandle material,
    const char* name,
    const glm::vec3& position,
    const glm::vec3& scale
) {
    EntityId entity = spawn(name);
    scene().add(entity, Mesh{mesh, material});

    Transform transform;
    transform.position = position;
    transform.scale    = scale;
    scene().add(entity, std::move(transform));
    return entity;
}

void StressArena::buildGround() {
    // Four slabs around an opening, so things can fall into the pit.
    const glm::vec3 slabs[4] = {
        { 0.0f, GROUND_Y - 0.5f,  PIT_HALF + GROUND_BAND},
        { 0.0f, GROUND_Y - 0.5f, -PIT_HALF - GROUND_BAND},
        { PIT_HALF + GROUND_BAND, GROUND_Y - 0.5f, 0.0f},
        {-PIT_HALF - GROUND_BAND, GROUND_Y - 0.5f, 0.0f},
    };
    const glm::vec3 halves[4] = {
        {ARENA_HALF,  0.5f, GROUND_BAND}, {ARENA_HALF,  0.5f, GROUND_BAND},
        {GROUND_BAND, 0.5f, PIT_HALF},    {GROUND_BAND, 0.5f, PIT_HALF},
    };

    for (int i = 0; i < 4; ++i) {
        EntityId ground = spawnMesh(
            i < 2 ? m_groundBand : m_groundSide,
            m_matGround,
            "Ground",
            slabs[i],
            halves[i] * 2.0f
        );
        // The ground occludes nothing from lights above it.
        scene().get<Mesh>(ground).castShadows = false;

        Rigidbody rb;
        rb.motion = RigidbodyMotion::Static;
        scene().add(ground, std::move(rb));

        Collider col;
        col.parts = {ColliderPart{ColliderShape::Box, {0.0f, 0.0f, 0.0f}, halves[i]}};
        scene().add(ground, std::move(col));
    }

    // Walls, so the pile packs instead of scattering across the block.
    EntityId floor = spawnMesh(
        m_cube,
        m_matTower,
        "Pit Floor",
        {0.0f, GROUND_Y - PIT_DEPTH, 0.0f},
        {PIT_HALF * 2.0f, 0.5f, PIT_HALF * 2.0f}
    );
    scene().get<Mesh>(floor).castShadows = false;

    Rigidbody floorBody;
    floorBody.motion = RigidbodyMotion::Static;
    scene().add(floor, std::move(floorBody));

    Collider floorCol;
    floorCol.parts = {ColliderPart{ColliderShape::Box, {0.0f, 0.0f, 0.0f}, {PIT_HALF, 0.25f, PIT_HALF}}};
    scene().add(floor, std::move(floorCol));

    for (int i = 0; i < 4; ++i) {
        const bool  alongX = (i % 2) == 0;
        const float sign   = (i < 2) ? 1.0f : -1.0f;
        const glm::vec3 position = alongX
            ? glm::vec3(sign * PIT_HALF, GROUND_Y - PIT_DEPTH * 0.5f, 0.0f)
            : glm::vec3(0.0f, GROUND_Y - PIT_DEPTH * 0.5f, sign * PIT_HALF);
        const glm::vec3 half = alongX
            ? glm::vec3(0.5f, PIT_DEPTH * 0.5f, PIT_HALF)
            : glm::vec3(PIT_HALF, PIT_DEPTH * 0.5f, 0.5f);

        EntityId wall = spawnMesh(m_cube, m_matTower, "Pit Wall", position, half * 2.0f);
        scene().get<Mesh>(wall).castShadows = false;

        Rigidbody wallBody;
        wallBody.motion = RigidbodyMotion::Static;
        scene().add(wall, std::move(wallBody));

        Collider wallCol;
        wallCol.parts = {ColliderPart{ColliderShape::Box, {0.0f, 0.0f, 0.0f}, half}};
        scene().add(wall, std::move(wallCol));
    }
}

void StressArena::buildTowers() {
    // Stacked boxes, a glass band and a lit crown: depth complexity for the prepass
    // and mixed queues for the forward pass.
    for (int i = 0; i < towerCount; ++i) {
        const glm::vec3 base = scatterOnGround(
            i,
            towerCount,
            TOWER_ZONE_INNER,
            TOWER_ZONE_OUTER,
            m_rng,
            0.55f
        );

        const int   floors = m_rng.nextInt(2, 7);
        const float width  = frand(4.0f, 9.0f);
        const float depth  = frand(4.0f, 9.0f);
        float       y      = GROUND_Y;

        for (int f = 0; f < floors; ++f) {
            const float height = frand(3.0f, 6.0f);
            const MaterialHandle material = (f % 3 == 1) ? m_matGlass : m_matTower;

            spawnMesh(m_cube, material, "Tower", {base.x, y + height * 0.5f, base.z}, {width, height, depth});
            y += height;
        }

        // Emissive cap: bloom sources spread across the frame.
        spawnMesh(
            m_cube,
            m_matEmissive,
            "Tower Crown",
            {base.x, y + 0.4f, base.z},
            {width * 0.55f, 0.8f, depth * 0.55f}
        );
    }
}

void StressArena::buildProps() {
    m_props.reserve(static_cast<size_t>(propCount));
    m_spinners.reserve(static_cast<size_t>(animatedCount));

    for (int i = 0; i < propCount; ++i) {
        const glm::vec3 spot = scatterOnGround(i, propCount, PROP_ZONE_INNER, PROP_ZONE_OUTER, m_rng);
        const float scale = frand(0.5f, 2.2f);

        const int shape = i % 3;
        const MeshHandle mesh = (shape == 0) ? m_cube : (shape == 1) ? m_sphere : m_cylinder;

        // Some chrome, so the probes have smooth metals to show in.
        const MaterialHandle material = (i % 23 == 0)
            ? m_matChrome
            : m_propMaterials[static_cast<size_t>(i) % m_propMaterials.size()];

        EntityId prop = spawnMesh(
            mesh,
            material,
            "Prop",
            {spot.x, GROUND_Y + scale * 0.5f, spot.z},
            glm::vec3(scale)
        );

        m_props.push_back(prop);

        // Short thresholds, so the camera loop crosses them.
        if (lodEnabled && shape != 0) {
            LOD lod;
            if (shape == 1) lod.levels = { {m_sphere,   35.0f}, {m_sphereMid, 70.0f}, {m_sphereLow, 0.0f} };
            else            lod.levels = { {m_cylinder, 35.0f}, {m_cylMid,    70.0f}, {m_cylLow,    0.0f} };
            scene().add(prop, std::move(lod));
        }

        if (static_cast<int>(m_spinners.size()) < animatedCount) {
            switch (i % 4) {
                case 0:
                    scene().add(prop, makeSpin(frand(2.0f, 6.0f), frand(0.0f, 4.0f)));
                    break;
                case 1:
                    scene().add(prop, makeBob(frand(1.5f, 4.0f), frand(0.5f, 2.5f), frand(0.0f, 3.0f)));
                    break;
                case 2:
                    scene().add(prop, makePulse(frand(1.2f, 3.5f), frand(0.2f, 0.7f), frand(0.0f, 3.0f)));
                    break;
                default:
                    scene().add(
                        prop,
                        makeOrbitHop(
                            frand(3.0f, 7.0f),
                            frand(0.6f, 2.4f),
                            frand(0.8f, 2.6f),
                            frand(0.0f, 5.0f)
                        )
                    );
                    break;
            }
            m_spinners.push_back(prop);
        }
    }
}

void StressArena::buildLights() {
    m_lights.reserve(static_cast<size_t>(lightCount));

    // Counted, not indexed: only point lights take static caster tiles (see shadowLights).
    int pointCasters = 0;

    for (int i = 0; i < lightCount; ++i) {
        // A third light the tower ring, so the skyline is not a black cutout.
        const bool  inField = (i % 3) != 0;
        const glm::vec3 spot = inField
            ? scatterOnGround(i, lightCount, PIT_HALF, PROP_ZONE_OUTER, m_rng, 0.5f)
            : scatterOnGround(i, lightCount, TOWER_ZONE_INNER, TOWER_ZONE_OUTER, m_rng, 0.5f);
        const float height  = inField ? frand(4.0f, 18.0f) : frand(8.0f, 34.0f);
        const glm::vec3 position(spot.x, GROUND_Y + height, spot.z);

        EntityId entity = spawn("Light");

        Transform transform;
        transform.position = position;
        // Read only for spots. Negative: with forward -Z, a positive turn about X tilts up.
        transform.rotation = glm::angleAxis(-frand(0.6f, 1.4f), Math::WORLD_AXIS_X);
        scene().add(entity, std::move(transform));

        Light light;
        light.type  = (i % 3 == 0) ? LightType::Spot : LightType::Point;
        light.color = glm::vec3(frand(0.5f, 1.0f), frand(0.5f, 1.0f), frand(0.6f, 1.0f));
        light.intensity = frand(8.0f, 22.0f);
        light.radius    = frand(10.0f, 24.0f);
        light.innerConeAngle = 0.35f;
        light.outerConeAngle = 0.7f;
        // Only static points (the first movingLights patrol), up to the cube budget:
        // the 2D tiles belong to the cascades and drone spots.
        const bool casts = light.type == LightType::Point && i >= movingLights && pointCasters < shadowLights;
        light.castShadows = casts;
        if (casts) ++pointCasters;
        scene().add(entity, std::move(light));

        // A visible fixture, so bloom has a source where the light is.
        EntityId fixture = spawnMesh(m_sphere, m_matEmissive, "Light Fixture", position, glm::vec3(0.45f));
        scene().get<Mesh>(fixture).castShadows = false;

        m_lights.push_back(entity);
        if (casts) m_shadowCasters.push_back(entity);

        // The orbit keeps the radius and height the light was placed at.
        if (i < movingLights) {
            PatrolLight patrol{
                entity,
                fixture,
                glm::length(glm::vec2(spot.x, spot.z)),
                height,
                frand(0.10f, 0.55f) * (i % 2 == 0 ? 1.0f : -1.0f),
                frand(0.0f, glm::two_pi<float>()),
                frand(1.0f, 4.0f)
            };
            m_patrol.push_back(patrol);
        }
    }

    EntityId sun = spawn("Sun");

    Transform sunTransform;
    // No rotation: with the procedural sky on, SkySystem aims it from the sun angles
    // every frame.
    scene().add(sun, std::move(sunTransform));

    Light sunLight;
    sunLight.type           = LightType::Directional;
    // Colour and intensity come from the sky, set in onStart.
    sunLight.castShadows    = true;
    sunLight.shadowDistance = 280.0f;
    scene().add(sun, std::move(sunLight));

    m_lights.push_back(sun);
    m_shadowCasters.push_back(sun);
}

void StressArena::buildEmitters() {
    m_emitters.reserve(static_cast<size_t>(emitterCount));

    for (int i = 0; i < emitterCount; ++i) {
        const glm::vec3 spot = scatterOnGround(i, emitterCount, PIT_HALF + 4.0f, PROP_ZONE_OUTER, m_rng);

        EntityId entity = spawn("Emitter");

        Transform transform;
        transform.position = {spot.x, GROUND_Y + 1.0f, spot.z};
        scene().add(entity, std::move(transform));

        ParticleEmitter emitter;
        // Half additive sparks, half alpha smoke: the blend modes sort and draw separately.
        const bool sparks = (i % 2) == 0;
        emitter.additive     = sparks;
        emitter.rate         = sparks ? frand(60.0f, 140.0f) : frand(20.0f, 50.0f);
        emitter.lifetime     = sparks ? frand(0.8f, 1.8f) : frand(2.5f, 4.5f);
        emitter.maxParticles = sparks ? 320 : 200;
        emitter.velocity     = sparks ? glm::vec3(0.0f, 5.0f, 0.0f) : glm::vec3(0.0f, 1.6f, 0.0f);
        emitter.spread       = sparks ? 2.4f : 0.8f;
        emitter.acceleration = sparks ? glm::vec3(0.0f, -6.0f, 0.0f) : glm::vec3(0.0f, 0.5f, 0.0f);
        emitter.startColor   = sparks
            ? glm::vec4(1.0f, 0.75f, 0.30f, 1.0f)
            : glm::vec4(0.55f, 0.58f, 0.65f, 0.5f);
        emitter.endColor     = sparks
            ? glm::vec4(1.0f, 0.20f, 0.05f, 0.0f)
            : glm::vec4(0.30f, 0.32f, 0.38f, 0.0f);
        emitter.startSize    = sparks ? 0.18f : 1.2f;
        emitter.endSize      = sparks ? 0.02f : 3.4f;
        emitter.softness     = sparks ? 0.3f : 1.0f;
        scene().add(entity, std::move(emitter));

        EntityId source = spawnMesh(
            m_cylinder,
            m_matEmissive,
            "Brazier",
            {spot.x, GROUND_Y + 0.4f, spot.z},
            {1.1f, 0.8f, 1.1f}
        );
        scene().get<Mesh>(source).castShadows = false;

        m_emitters.push_back(entity);
    }
}

void StressArena::buildDecals() {
    m_decals.reserve(static_cast<size_t>(decalCount));

    for (int i = 0; i < decalCount; ++i) {
        const glm::vec3 spot = scatterOnGround(i, decalCount, PIT_HALF, PROP_ZONE_OUTER, m_rng);

        EntityId entity = spawn("Decal");

        Transform transform;
        // The box's Y extent is the projection depth, so it must reach the ground.
        transform.position = {spot.x, GROUND_Y + 2.0f, spot.z};
        transform.rotation = glm::angleAxis(frand(0.0f, glm::two_pi<float>()), Math::WORLD_AXIS_Y);
        transform.scale    = glm::vec3(frand(3.0f, 8.0f), 5.0f, frand(3.0f, 8.0f));
        scene().add(entity, std::move(transform));

        Decal decal;
        decal.material  = m_matDecal;
        decal.angleFade = 0.6f;
        decal.opacity   = frand(0.35f, 0.9f);
        scene().add(entity, std::move(decal));

        m_decals.push_back(entity);
    }
}

void StressArena::buildProbes() {
    // Each bakes six faces on first sight (throttled by GLProbeManager): long frames at startup.
    for (int i = 0; i < reflectionProbes; ++i) {
        const float angle  = glm::two_pi<float>() * static_cast<float>(i)
            / static_cast<float>(std::max(1, reflectionProbes));
        const float radius = ARENA_HALF * 0.45f;

        EntityId entity = spawn("Reflection Probe");

        Transform transform;
        transform.position = {std::cos(angle) * radius, GROUND_Y + 10.0f, std::sin(angle) * radius};
        scene().add(entity, std::move(transform));

        ReflectionProbe probe;
        probe.halfExtents = glm::vec3(ARENA_HALF * 0.5f, 22.0f, ARENA_HALF * 0.5f);
        probe.resolution  = 256;
        probe.intensity   = 1.0f;
        scene().add(entity, std::move(probe));
    }

    // Exercises the diffuse GI path beside the specular probes.
    EntityId volume = spawn("Irradiance Volume");

    Transform transform;
    transform.position = {0.0f, GROUND_Y + 14.0f, 0.0f};
    scene().add(volume, std::move(transform));

    IrradianceVolume irradiance;
    irradiance.halfExtents = glm::vec3(ARENA_HALF, 20.0f, ARENA_HALF);
    irradiance.resolutionX = 12;
    irradiance.resolutionY = 4;
    irradiance.resolutionZ = 12;
    scene().add(volume, std::move(irradiance));
}

void StressArena::buildPhysics() {
    m_bodies.reserve(static_cast<size_t>(physicsBodies));

    for (int i = 0; i < physicsBodies; ++i) {
        const float size = frand(0.6f, 1.4f);
        const glm::vec3 position(
            frand(-PIT_HALF + 2.0f, PIT_HALF - 2.0f),
            GROUND_Y + frand(2.0f, 40.0f),
            frand(-PIT_HALF + 2.0f, PIT_HALF - 2.0f)
        );

        const MaterialHandle material =
            m_propMaterials[static_cast<size_t>(i) % m_propMaterials.size()];

        EntityId entity = spawnMesh(m_cube, material, "Body", position, glm::vec3(size));

        Rigidbody rb;
        rb.mass        = size * size * size * 8.0f;
        rb.restitution = 0.35f;
        rb.friction    = 0.45f;
        // Must keep moving: the solver skips every pair a slept body rests in.
        rb.canSleep = false;
        scene().add(entity, std::move(rb));

        Collider col;
        col.parts = {ColliderPart{ColliderShape::Box, {0.0f, 0.0f, 0.0f}, glm::vec3(size * 0.5f)}};
        scene().add(entity, std::move(col));

        m_bodies.push_back(entity);
    }
}

void StressArena::buildModels() {
    if (modelInstances <= 0) return;

    AssetLibrary& library = AssetLibrary::get();

    // Discovered, not hardcoded; the list is sorted, so every run picks the same meshes.
    const std::vector<std::string> meshNames = library.namesOf(AssetType::Mesh);
    const std::vector<std::string> textureNames = library.namesOf(AssetType::Texture);

    // Skip the engine's generated primitives; the props already use them.
    std::vector<std::string> usable;
    for (const std::string& name : meshNames) {
        if (name.rfind("mesh:generator:", 0) == 0) continue;
        usable.push_back(name);
    }

    if (usable.empty()) {
        LOG_INFO("no cooked meshes in the library - running procedural props only");
        return;
    }

    // Every kind costs a cooked read at startup.
    const size_t kinds = std::min(usable.size(), static_cast<size_t>(std::max(1, modelKinds)));

    std::vector<MaterialHandle> materials;
    for (size_t i = 0; i < textureNames.size() && materials.size() < 8; ++i) {
        // "#s" marks an sRGB-cooked texture, i.e. an albedo map; linear ones are normal/ORM.
        const std::string& texture = textureNames[i];
        if (texture.size() < 2 || texture.compare(texture.size() - 2, 2, "#s") != 0) continue;

        MaterialAsset m;
        m.albedo        = {1.0f, 1.0f, 1.0f, 1.0f};
        m.roughness     = 0.65f;
        m.metallic      = 0.0f;
        m.albedoTexture = loadCookedTexture(texture, resources());
        if (!m.albedoTexture) continue;

        const std::string name = "stress:model_mat_" + std::to_string(materials.size());
        materials.push_back(makeMaterial(m, name.c_str()));
    }
    if (materials.empty()) materials = m_propMaterials;

    m_models.reserve(kinds);
    for (size_t k = 0; k < kinds; ++k) {
        MeshHandle mesh = loadCookedMesh(usable[k], resources());
        if (!mesh) continue;
        m_models.push_back(ModelKind{mesh, {}, {}, false});
        ++m_unfittedKinds;
    }

    if (m_models.empty()) {
        LOG_WARNING("cooked meshes present but none resolved - procedural props only");
        return;
    }

    for (int i = 0; i < modelInstances; ++i) {
        ModelKind& kind = m_models[static_cast<size_t>(i) % m_models.size()];

        // Offset, so models interleave with the props instead of sharing their spiral.
        const glm::vec3 spot = scatterOnGround(
            i * 3 + 1,
            modelInstances * 3,
            PIT_HALF + 8.0f,
            PROP_ZONE_OUTER,
            m_rng
        );
        const float size = frand(2.5f, 7.0f);

        EntityId entity = spawnMesh(
            kind.mesh,
            materials[static_cast<size_t>(i) % materials.size()],
            "Model",
            {spot.x, GROUND_Y, spot.z},
            glm::vec3(1.0f)
        );

        scene().get<Transform>(entity).rotation =
            glm::angleAxis(frand(0.0f, glm::two_pi<float>()), Math::WORLD_AXIS_Y);

        kind.instances.push_back(entity);
        kind.sizes.push_back(size);
    }

    LOG_INFO(
        "models: %zu kinds, %d instances, %zu textured materials",
        m_models.size(),
        modelInstances,
        materials.size()
    );
}

void StressArena::updateModelScales() {
    if (m_unfittedKinds == 0) return;

    // Sources vary wildly in extent, so each kind is fitted the frame its vertices arrive.
    for (ModelKind& kind : m_models) {
        if (kind.fitted) continue;

        const MeshAsset& asset = resources().get(kind.mesh);
        if (asset.loading || asset.vertices.empty()) continue;

        const glm::vec3 extent = asset.boundsMax - asset.boundsMin;
        const float     longest = std::max({extent.x, extent.y, extent.z});
        if (longest <= glm::epsilon<float>()) {
            kind.fitted = true;
            --m_unfittedKinds;
            continue;
        }

        for (size_t i = 0; i < kind.instances.size(); ++i) {
            Transform* held = scene().tryGet<Transform>(kind.instances[i]);
            if (!held) continue;

            Transform& transform = *held;
            const float scale = kind.sizes[i] / longest;
            transform.scale = glm::vec3(scale);
            // The origin is wherever the exporter left it, so lift by the scaled underside.
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

        EntityId body = spawnMesh(m_cube, m_matChrome, "Drone", {radius, height, 0.0f}, {1.6f, 0.5f, 2.4f});

        EntityId arm = spawnMesh(m_cube, m_matTower, "Drone Arm", {0.0f, 0.0f, 0.0f}, {0.25f, 0.9f, 0.25f});
        scene().get<Transform>(arm).position = {0.0f, 0.6f, 0.0f};
        HierarchyOperations::setParent(scene(), arm, body);

        EntityId rotor = spawnMesh(
            m_cylinder,
            m_matEmissive,
            "Drone Rotor",
            {0.0f, 0.0f, 0.0f},
            {2.6f, 0.08f, 2.6f}
        );
        scene().get<Transform>(rotor).position = {0.0f, 0.55f, 0.0f};
        HierarchyOperations::setParent(scene(), rotor, arm);
        // Animated, so the chain is dirtied from two sources in one frame.
        scene().add(rotor, makeSpin(0.35f, frand(0.0f, 0.35f)));

        Drone drone{
            body,
            EntityId{},
            radius,
            height,
            frand(0.12f, 0.42f) * (i % 2 == 0 ? 1.0f : -1.0f),
            frand(0.0f, glm::two_pi<float>())
        };
        if (i % 4 == 0) {
            EntityId lamp = spawn("Drone Lamp", body);

            Transform lampTransform;
            lampTransform.position = {0.0f, -0.4f, 0.0f};
            // Straight down: with forward -Z, the floorward quarter turn about X is negative.
            lampTransform.rotation =
                glm::angleAxis(-glm::half_pi<float>(), Math::WORLD_AXIS_X);
            scene().add(lamp, std::move(lampTransform));

            Light spot;
            spot.type           = LightType::Spot;
            spot.color          = {1.0f, 0.85f, 0.6f};
            spot.intensity      = 30.0f;
            spot.radius         = 45.0f;
            spot.innerConeAngle = 0.25f;
            spot.outerConeAngle = 0.5f;
            spot.castShadows    = false;   // promoted below
            scene().add(lamp, std::move(spot));
            drone.lamp = lamp;
        }

        m_drones.push_back(drone);
    }

    // Only as many as there are tiles left; a surplus caster draws unshadowed.
    int promoted = 0;
    for (Drone& drone : m_drones) {
        if (promoted >= MOVING_SHADOW_CASTERS) break;
        Light* lamp = scene().tryGet<Light>(drone.lamp);
        if (!lamp) continue;
        lamp->castShadows = true;
        m_shadowCasters.push_back(drone.lamp);
        ++promoted;
    }
}

void StressArena::updatePatrolLights() {
    for (PatrolLight& light : m_patrol) {
        Transform* at = scene().tryGet<Transform>(light.entity);
        if (!at) continue;

        const float angle = light.phase + m_motionTime * light.speed;
        const glm::vec3 position(
            std::cos(angle) * light.radius,
            GROUND_Y + light.height + std::sin(angle * 2.3f) * light.bobAmp,
            std::sin(angle) * light.radius
        );

        at->position = position;
        if (Transform* fixture = scene().tryGet<Transform>(light.fixture)) {
            fixture->position = position;
        }
    }
}

void StressArena::updateDrones() {
    for (Drone& drone : m_drones) {
        Transform* held = scene().tryGet<Transform>(drone.body);
        if (!held) continue;

        const float angle = drone.phase + m_motionTime * drone.speed;
        Transform& transform = *held;
        transform.position = {
            std::cos(angle) * drone.radius,
            GROUND_Y + drone.height + std::sin(angle * 1.9f) * 2.5f,
            std::sin(angle) * drone.radius
        };
        // Bank and face the tangent; the half turn maps yaw from +Z onto forward -Z.
        transform.rotation = glm::angleAxis(-angle, Math::WORLD_AXIS_Y)
            * glm::angleAxis(std::sin(angle * 1.9f) * 0.25f, Math::WORLD_AXIS_Z)
            * glm::angleAxis(glm::pi<float>(), Math::WORLD_AXIS_Y);
    }
}

void StressArena::updateDebris(float dt) {
    // Age first, so a piece spawned this frame is not considered for destruction.
    for (size_t i = 0; i < m_debris.size();) {
        Debris& piece = m_debris[i];
        piece.life -= dt;

        if (piece.life > 0.0f && scene().isAlive(piece.entity)) {
            ++i;
            continue;
        }

        if (scene().isAlive(piece.entity)) destroy(piece.entity);
        m_debris[i] = m_debris.back();
        m_debris.pop_back();
    }

    if (debrisRate <= 0.0f) return;

    constexpr size_t MAX_LIVE = 900;

    m_debrisAccum += debrisRate * dt;
    while (m_debrisAccum >= 1.0f) {
        // Keep only the fraction while capped, or banked spawns burst once pieces expire.
        if (m_debris.size() >= MAX_LIVE) {
            m_debrisAccum -= std::floor(m_debrisAccum);
            break;
        }
        m_debrisAccum -= 1.0f;

        const float angle  = m_churnRng.nextFloat(0.0f, glm::two_pi<float>());
        const float radius = m_churnRng.nextFloat(PIT_HALF, PROP_ZONE_OUTER);
        const float size   = m_churnRng.nextFloat(0.25f, 0.8f);

        // The shared cube, so pieces batch with the props: the churn is entity lifetime.
        EntityId piece = spawnMesh(
            m_cube,
            m_propMaterials[static_cast<size_t>(m_debris.size()) % m_propMaterials.size()],
            "Debris",
            {
                std::cos(angle) * radius,
                GROUND_Y + m_churnRng.nextFloat(14.0f, 26.0f),
                std::sin(angle) * radius
            },
            glm::vec3(size)
        );

        // No shadows: the churn measures the ECS and the draw list.
        scene().get<Mesh>(piece).castShadows = false;

        Rigidbody rb;
        rb.mass            = size * 4.0f;
        rb.restitution     = 0.4f;
        rb.friction        = 0.4f;
        rb.canSleep        = false;
        rb.linearVelocity  = {
            m_churnRng.nextFloat(-6.0f, 6.0f),
            m_churnRng.nextFloat(-2.0f, 4.0f),
            m_churnRng.nextFloat(-6.0f, 6.0f)
        };
        rb.angularVelocity = {
            m_churnRng.nextFloat(-6.0f, 6.0f),
            m_churnRng.nextFloat(-6.0f, 6.0f),
            m_churnRng.nextFloat(-6.0f, 6.0f)
        };
        scene().add(piece, std::move(rb));

        Collider col;
        col.parts   = {ColliderPart{ColliderShape::Box, {0.0f, 0.0f, 0.0f}, glm::vec3(size * 0.5f)}};
        col.enabled = m_physicsOn;
        scene().add(piece, std::move(col));

        m_debris.push_back(Debris{piece, m_churnRng.nextFloat(2.5f, 5.0f)});
    }
}

void StressArena::updateBlast(float dt) {
    if (blastInterval <= 0.0f || !m_physicsOn) return;

    m_blastTimer -= dt;
    if (m_blastTimer > 0.0f) return;
    m_blastTimer = blastInterval;

    // Radial impulse from the pit floor.
    const glm::vec3 origin(0.0f, GROUND_Y - PIT_DEPTH, 0.0f);
    for (EntityId body : m_bodies) {
        const Transform* at = scene().tryGet<Transform>(body);
        Rigidbody*       held = at ? scene().tryGet<Rigidbody>(body) : nullptr;
        if (!held) continue;

        const glm::vec3 offset = at->position - origin;
        const float     dist   = std::max(1.0f, glm::length(offset));

        Rigidbody& rb = *held;
        rb.linearVelocity  += (offset / dist) * (28.0f / dist) + glm::vec3(0.0f, 9.0f, 0.0f);
        rb.angularVelocity += glm::vec3(
            m_churnRng.nextFloat(-5.0f, 5.0f),
            m_churnRng.nextFloat(-5.0f, 5.0f),
            m_churnRng.nextFloat(-5.0f, 5.0f)
        );
        Rigidbody::wake(rb);
    }
}

void StressArena::updateMaterialPulse() {
    if (pulsingMaterials <= 0 || m_propMaterials.empty()) return;

    const int count = std::min(pulsingMaterials, static_cast<int>(m_propMaterials.size()));
    for (int i = 0; i < count; ++i) {
        const MaterialHandle handle = m_propMaterials[static_cast<size_t>(i)];
        const float phase = m_motionTime * 2.0f + static_cast<float>(i) * 0.7f;
        const float glow  = 0.5f + 0.5f * std::sin(phase);

        MaterialAsset& material = resources().edit(handle);
        material.emission         = glm::vec3(0.9f, 0.45f, 0.15f) * glow;
        material.emissiveStrength = 1.0f + glow * 3.0f;
        // commit bumps the version, so GLView re-uploads the UBO next sync.
        resources().commit(handle);
    }
}

void StressArena::buildUI() {
    EntityId canvas = spawn("HUD");
    scene().add(canvas, UICanvas{});
    m_hudCanvas = canvas;

    auto addLine = [&](const char* name, float y, float pixelSize, const glm::vec4& color) {
        EntityId line = spawn(name, canvas);
        scene().add(line, UIElement::at({0.0f, 0.0f}, {24.0f, y}, {900.0f, 40.0f}));

        UIText text;
        text.text      = "warming up";
        text.pixelSize = pixelSize;
        text.color     = color;
        scene().add(line, std::move(text));
        return line;
    };

    m_uiStats   = addLine("Stats",   18.0f, 28.0f, {0.85f, 0.92f, 1.00f, 1.0f});
    m_uiToggles = addLine("Toggles", 56.0f, 18.0f, {0.70f, 0.78f, 0.90f, 0.95f});

    // Filler widgets, to load the UI layout walk and draw pass.
    m_uiWidgets.reserve(static_cast<size_t>(uiWidgetCount));
    for (int i = 0; i < uiWidgetCount; ++i) {
        EntityId widget = spawn("Widget", canvas);
        const float column = static_cast<float>(i % 6);
        const float row    = static_cast<float>(i / 6);
        const glm::vec2 position{-20.0f - column * 108.0f, 20.0f + row * 40.0f};
        scene().add(widget, UIElement::at({1.0f, 0.0f}, position, {100.0f, 32.0f}));

        // Alternate image and text, to load both draw paths.
        if (i % 2 == 0) {
            UIImage image;
            image.color = {frand(0.2f, 0.9f), frand(0.2f, 0.9f), frand(0.4f, 1.0f), 0.55f};
            scene().add(widget, std::move(image));
        } else {
            UIText text;
            text.text      = "SLOT " + std::to_string(i);
            text.pixelSize = 18.0f;
            text.color     = {0.75f, 0.85f, 1.0f, 0.9f};
            scene().add(widget, std::move(text));
        }
        m_uiWidgets.push_back(widget);
    }
}

void StressArena::onUpdate(float dt) {
    // One clock for every scripted motion, so the scene stays phase-locked.
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
    const InputMap& input = this->input();

    if (input.pressed(ACTION_LIGHTS)) {
        m_lightsOn = !m_lightsOn;
        setLightsEnabled(m_lightsOn);
    }
    if (input.pressed(ACTION_SHADOWS)) {
        m_shadowsOn = !m_shadowsOn;
        setShadowsEnabled(m_shadowsOn);
    }
    if (input.pressed(ACTION_PROPS)) {
        m_propsOn = !m_propsOn;
        setPropsVisible(m_propsOn);
    }
    if (input.pressed(ACTION_PARTICLES)) {
        m_particlesOn = !m_particlesOn;
        setParticlesEnabled(m_particlesOn);
    }
    if (input.pressed(ACTION_PHYSICS)) {
        m_physicsOn = !m_physicsOn;
        setPhysicsEnabled(m_physicsOn);
    }
    if (input.pressed(ACTION_ANIM)) {
        m_animationsOn = !m_animationsOn;
        setAnimationsEnabled(m_animationsOn);
    }
    if (input.pressed(ACTION_DECALS)) {
        m_decalsOn = !m_decalsOn;
        setDecalsEnabled(m_decalsOn);
    }
    if (input.pressed(ACTION_FOG)) {
        m_fogOn = !m_fogOn;
        scene().environment().fog.enabled = m_fogOn;
    }
    if (input.pressed(ACTION_UI)) {
        m_uiOn = !m_uiOn;
        setUIVisible(m_uiOn);
    }

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
        scene().environment().fog.enabled = true;
    }

    // F pauses the camera; the loop resumes from where it stopped.
    if (input.pressed(ACTION_CAMERA)) {
        scriptedCamera = !scriptedCamera;
        LOG_INFO("camera: %s", scriptedCamera ? "scripted" : "held");
    }
}

void StressArena::updateCamera(float dt) {
    if (!scriptedCamera) return;
    Transform* view = scene().tryGet<Transform>(m_camera);
    if (!view) return;

    m_camTime += dt;

    if (cameraCutInterval > 0.0f) {
        m_cutTimer -= dt;
        if (m_cutTimer <= 0.0f) {
            m_cutTimer = cameraCutInterval;
            m_camTime += cameraLoopTime * 0.41f;   // not a clean fraction, so poses do not repeat
        }
    }

    const float loop = (cameraLoopTime > 0.1f) ? cameraLoopTime : 0.1f;
    const float t    = m_camTime / loop * glm::two_pi<float>();

    // Height oscillates at a different rate, so laps do not repeat exactly.
    const glm::vec3 position(
        std::cos(t) * CAM_RADIUS,
        GROUND_Y + CAM_HEIGHT + std::sin(t * 1.7f) * CAM_HEIGHT_AMP,
        std::sin(t) * CAM_RADIUS
    );

    const glm::vec3 target(0.0f, GROUND_Y + CAM_LOOK_Y, 0.0f);
    const glm::vec3 forward = glm::normalize(target - position);

    // Focus on the look target.
    if (Camera* camera = scene().tryGet<Camera>(m_camera)) {
        camera->focusDistance = glm::distance(position, target);
    }

    view->position = position;
    view->rotation = Math::lookRotation(forward);
}

void StressArena::updatePhysics() {
    if (!m_physicsOn) return;

    // Relaunch anything that has left the pit.
    for (EntityId body : m_bodies) {
        Transform* held = scene().tryGet<Transform>(body);
        if (!held) continue;

        Transform& transform = *held;
        const bool escaped = std::abs(transform.position.x) > PIT_HALF + 6.0f
            || std::abs(transform.position.z) > PIT_HALF + 6.0f
            || transform.position.y < GROUND_Y - PIT_DEPTH - 6.0f;
        if (!escaped) continue;

        transform.position = {
            m_churnRng.nextFloat(-PIT_HALF + 2.0f, PIT_HALF - 2.0f),
            GROUND_Y + m_churnRng.nextFloat(25.0f, 45.0f),
            m_churnRng.nextFloat(-PIT_HALF + 2.0f, PIT_HALF - 2.0f)
        };

        Rigidbody& rb = scene().get<Rigidbody>(body);
        rb.linearVelocity  = {m_churnRng.nextFloat(-2.0f, 2.0f), 0.0f, m_churnRng.nextFloat(-2.0f, 2.0f)};
        rb.angularVelocity = {
            m_churnRng.nextFloat(-3.0f, 3.0f),
            m_churnRng.nextFloat(-3.0f, 3.0f),
            m_churnRng.nextFloat(-3.0f, 3.0f)
        };
        Rigidbody::wake(rb);
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

    // Tracy owns the real numbers; these let a capture be read against its load.
    char buffer[256];

    if (UIText* stats = scene().tryGet<UIText>(m_uiStats)) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%.1f fps   %.2f ms   %zu entities   %zu debris   camera:%s",
            static_cast<double>(fps),
            static_cast<double>(ms),
            scene().entityCount(),
            m_debris.size(),
            scriptedCamera ? "scripted [F]" : "held [F]"
        );
        stats->text = buffer;
    }

    if (UIText* toggles = scene().tryGet<UIText>(m_uiToggles)) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "1 light:%s  2 shadow:%s  3 props:%s  4 fx:%s  5 phys:%s  "
            "6 anim:%s  7 decal:%s  8 fog:%s  9 ui:%s  0 reset",
            m_lightsOn ? "on" : "OFF",
            m_shadowsOn ? "on" : "OFF",
            m_propsOn ? "on" : "OFF",
            m_particlesOn ? "on" : "OFF",
            m_physicsOn ? "on" : "OFF",
            m_animationsOn ? "on" : "OFF",
            m_decalsOn ? "on" : "OFF",
            m_fogOn ? "on" : "OFF",
            m_uiOn ? "on" : "OFF"
        );
        toggles->text = buffer;
    }
}

void StressArena::setLightsEnabled(bool enabled) {
    for (EntityId light : m_lights) {
        if (Light* held = scene().tryGet<Light>(light)) held->enabled = enabled;
    }
    // Drone lamps are not in m_lights.
    for (Drone& drone : m_drones) {
        if (Light* lamp = scene().tryGet<Light>(drone.lamp)) lamp->enabled = enabled;
    }
}

void StressArena::setShadowsEnabled(bool enabled) {
    // Which lights cast is decided at build, against the atlas budget; this only toggles it.
    for (EntityId caster : m_shadowCasters) {
        if (Light* held = scene().tryGet<Light>(caster)) held->castShadows = enabled;
    }
}

void StressArena::setPropsVisible(bool visible) {
    for (EntityId prop : m_props) {
        if (Mesh* mesh = scene().tryGet<Mesh>(prop)) mesh->visible = visible;
    }
}

void StressArena::setParticlesEnabled(bool enabled) {
    for (EntityId emitter : m_emitters) {
        if (ParticleEmitter* e = scene().tryGet<ParticleEmitter>(emitter)) e->emitting = enabled;
    }
}

void StressArena::setPhysicsEnabled(bool enabled) {
    // A disabled collider leaves the broadphase but the body still integrates, so
    // the pile falls away; updatePhysics relaunches what escaped once back on.
    for (EntityId body : m_bodies) {
        if (Collider* col = scene().tryGet<Collider>(body)) col->enabled = enabled;
    }
    // Live debris too; new pieces read m_physicsOn at spawn.
    for (Debris& piece : m_debris) {
        if (Collider* col = scene().tryGet<Collider>(piece.entity)) col->enabled = enabled;
    }
}

void StressArena::setAnimationsEnabled(bool enabled) {
    for (EntityId spinner : m_spinners) {
        if (Animation* anim = scene().tryGet<Animation>(spinner)) anim->playing = enabled;
    }
    // Rotors are not in m_spinners.
    for (Drone& drone : m_drones) {
        if (!scene().isAlive(drone.body)) continue;
        HierarchyOperations::forEachChild(scene(), drone.body, [&](EntityId arm) {
            HierarchyOperations::forEachChild(scene(), arm, [&](EntityId rotor) {
                if (Animation* anim = scene().tryGet<Animation>(rotor)) anim->playing = enabled;
            });
        });
    }
}

void StressArena::setDecalsEnabled(bool enabled) {
    for (EntityId decal : m_decals) {
        if (Decal* held = scene().tryGet<Decal>(decal)) held->enabled = enabled;
    }
}

void StressArena::setUIVisible(bool visible) {
    if (UICanvas* hud = scene().tryGet<UICanvas>(m_hudCanvas)) hud->visible = visible;
}

} // namespace Arena

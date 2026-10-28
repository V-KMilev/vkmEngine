// Golden images: whole frames rendered by the real backend, compared with frames it
// rendered before. Some defects - a sky reflected at full strength in a shaded floor,
// a reflection speckled by a normal map - only show in a frame, so these render a
// small scene built in code through the systems a host runs ahead of the backend (sky,
// visibility, RenderView) and GLBackend, read back off the default framebuffer.
//
// A frame is a property of the GPU too, so each golden records its renderer and is
// only compared there; elsewhere the test says so and passes. After an intended
// change, regenerate with VKM_UPDATE_GOLDENS=1, look at the images, and commit them.

#include "golden_tests.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "stb_image.h"
#include "stb_image_write.h"

#include "egl_context.h"

#include "gl_backend.h"
#include "gl_view.h"
#include "storage/gl_lightmaps.h"
#include "gl_shader_base.h"
#include "gl_shader_reload.h"
#include "gl_forward_programs.h"
#include "asset/gl_material.h"
#include "pass/gl_planar_reflection_pass.h"
#include "frame/gl_occlusion_cull.h"
#include "offline/gl_capture_settle.h"
#include "offline/gl_ibl_baker.h"
#include "offline/gl_irradiance_baker.h"
#include "offline/gl_probe_baker.h"
#include "storage/gl_probe_manager.h"
#include "core/clock.h"
#include "core/event/event_bus.h"
#include "core/host_chrome.h"
#include "core/system.h"
#include "debug/debug_draw.h"
#include "ecs/environment.h"
#include "convention/gl_bindings.h"
#include "ecs/scene.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/ocean.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/planar_reflector.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/component/render/terrain.h"
#include "net/net_session.h"
#include "platform/input/input_map.h"
#include "platform/window/window_manager.h"
#include "resource/resource_manager.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/texture_format.h"
#include "resource/generate/mesh_generators.h"
#include "system/animation/pose_buffer.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_settings.h"
#include "system/particle/live_particles.h"
#include "system/render/render_view.h"
#include "system/ui/ui_draw_data.h"
#include "system/ocean/ocean_system.h"
#include "system/debug/debug_view_system.h"
#include "system/sky/sky_system.h"
#include "system/terrain/terrain_system.h"
#include "system/visibility/visibility_system.h"

namespace Vkm::Test {

namespace {

using namespace Vkm::Engine;

constexpr int WIDTH  = 320;
constexpr int HEIGHT = 180;

// Enough frames for the bakes a first frame starts to be in the picture.
constexpr int FRAMES = 4;
// Past the frames a bake waits for the scene to hold still, and FRAMES more for it to be in the picture.
constexpr int SETTLED = static_cast<int>(CAPTURE_SETTLE_FRAMES) + FRAMES;

// What each frame advances the clock by, for what adapts over time: a tenth of a second, so an
// adaptation shows within tens of frames.
constexpr float FRAME_SECONDS = 0.1f;

// The resolution buildProbe's probe asks for, which the arrays raise to GLProbeArray::MIN_RESOLUTION.
constexpr uint32_t PROBE_RESOLUTION = 64;

// A channel further than this from the golden is a differing pixel; more than this
// share of them is a differing frame. Loose enough for a driver update; a lost lobe,
// shadow or reflection is thousands of pixels over.
constexpr int   CHANNEL_TOLERANCE = 12;
constexpr float PIXEL_TOLERANCE   = 0.002f;
// How much of a frame a baked shadow may draw unlike the live one: its penumbra is the bake's
// filter at the lightmap's texel, not the frame's, so its edges differ by a little.
constexpr float BAKED_TOLERANCE   = 0.02f;

const std::filesystem::path GOLDEN_DIR = "tests/render/golden";

// What render() gives a frame's long bakes; 0, whole, unless a test slices them.
float g_bakeBudget = 0.0f;
// Whether render() draws a world still waiting on its meshes or textures (RenderView::worldArriving).
bool g_worldArriving = false;
// The rigs' poses render() hands the frame; null for none.
const PoseBuffer* g_poses = nullptr;
// Ticks the world has run when render() draws: the moment the water's ripples and flow stand at.
int g_worldTicks = 0;

struct Scenery {
    Scene           scene;
    ResourceManager resources;
    LiveParticles   particles;  ///< Placed by the scene, as ParticleSystem would simulate them.
    RenderSettings  settings;   ///< What the frame is rendered with.
    MeshHandle      cube;
    MeshHandle      sphere;

    /// What DebugDraw held, drawn over the scene as RenderSystem hands it on; none for most.
    std::vector<DebugLine> debugLines;
    double                 debugSeconds = 0.0;  ///< Where the pulses stand
};

MaterialHandle material(
    Scenery& s,
    const char* name,
    glm::vec3 albedo,
    float roughness,
    float metallic,
    glm::vec3 emission = glm::vec3(0.0f)
) {
    MaterialAsset m;
    m.albedo    = glm::vec4(albedo, 1.0f);
    m.roughness = roughness;
    m.metallic  = metallic;
    m.emission  = emission;
    return s.resources.add(std::move(m), name);
}

void place(Scenery& s, MeshHandle mesh, MaterialHandle mat, glm::vec3 at, glm::vec3 scale) {
    const EntityId id = s.scene.createEntity();
    Transform t;
    t.position = at;
    t.scale    = scale;
    s.scene.add(id, std::move(t));
    Mesh m;
    m.mesh     = mesh;
    m.material = mat;
    s.scene.add(id, std::move(m));
}

// The eye at @p eye looking at @p target, framed for the golden size.
void camera(Scenery& s, glm::vec3 eye, glm::vec3 target) {
    const EntityId eyeId = s.scene.createEntity();
    Transform eyePose;
    eyePose.position = eye;
    eyePose.rotation = glm::quatLookAt(glm::normalize(target - eye), glm::vec3(0.0f, 1.0f, 0.0f));
    s.scene.add(eyeId, std::move(eyePose));
    Camera lens;
    lens.aspect = static_cast<float>(WIDTH) / static_cast<float>(HEIGHT);
    s.scene.add(eyeId, std::move(lens));
}

// The key light where the sky says the sun is. Under the procedural sky SkySystem
// aims and colors it each frame, as in a host; under none, this is the authored aim.
void sunAndCamera(Scenery& s, glm::vec3 eye, glm::vec3 target) {
    const EntityId sun = s.scene.createEntity();
    Transform sunPose;
    sunPose.rotation = glm::quatLookAt(-s.scene.environment().sunDirection(), glm::vec3(0.0f, 1.0f, 0.0f));
    s.scene.add(sun, std::move(sunPose));
    Light light;
    light.type        = LightType::Directional;
    light.intensity   = 3.0f;
    light.castShadows = true;
    s.scene.add(sun, std::move(light));
    camera(s, eye, target);
}

void base(Scenery& s) {
    s.cube   = s.resources.add(generateCube(), "golden:cube");
    s.sphere = s.resources.add(generateSphere(), "golden:sphere");
}

// An emitter at the origin, with the pool ParticleSystem would keep for it: what the scene puts
// in the pool is what the frame draws.
LiveParticles::Pool& addEmitterPool(Scenery& s, ParticleEmitter emitter) {
    const EntityId id = s.scene.createEntity();
    s.scene.add(id, Transform{});
    s.scene.add(id, std::move(emitter));
    LiveParticles::Pool& pool = s.particles.pools[id.slot()];
    pool.emitter = id;
    return pool;
}

// The same, for an emitter whose pool holds particles alone.
std::vector<Particle>& addEmitter(Scenery& s, ParticleEmitter emitter) {
    return addEmitterPool(s, std::move(emitter)).particles;
}

// One look over a whole life: @p color, brightened by @p intensity, at the size each particle has.
void holdLook(ParticleEmitter& emitter, glm::vec4 color, float intensity = 1.0f) {
    emitter.color.keys = {{0.0f, color}};
    emitter.sizeOverLife.keys.clear();
    emitter.intensity = intensity;
}

// A point lamp at @p at, returned for what else a scene sets on it.
Light& addLamp(Scenery& s, glm::vec3 at, float intensity, float radius, bool castShadows) {
    const EntityId lamp = s.scene.createEntity();
    Transform pose;
    pose.position = at;
    s.scene.add(lamp, std::move(pose));
    Light light;
    light.type        = LightType::Point;
    light.intensity   = intensity;
    light.radius      = radius;
    light.castShadows = castShadows;
    return s.scene.add(lamp, std::move(light));
}

// Metals and dielectrics across roughness on a matte floor, under the sun: direct
// lobes, environment, shadows, GTAO under the spheres.
void buildMaterials(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:floor", {0.45f, 0.45f, 0.42f}, 0.85f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );
    const float roughness[5] = {0.05f, 0.25f, 0.45f, 0.65f, 0.9f};
    for (int i = 0; i < 5; ++i) {
        const float x = -4.0f + 2.0f * static_cast<float>(i);
        const std::string metal = "golden:metal" + std::to_string(i);
        const std::string paint = "golden:paint" + std::to_string(i);
        place(
            s,
            s.sphere,
            material(s, metal.c_str(), {0.95f, 0.64f, 0.54f}, roughness[i], 1.0f),
            {x, 0.8f, -1.2f},
            glm::vec3(1.5f)
        );
        place(
            s,
            s.sphere,
            material(s, paint.c_str(), {0.15f, 0.35f, 0.75f}, roughness[i], 0.0f),
            {x, 0.8f, 1.2f},
            glm::vec3(1.5f)
        );
    }
    sunAndCamera(s, {0.0f, 4.5f, 9.0f}, {0.0f, 0.5f, 0.0f});
}

// A glossy floor under colored boxes and a glowing sphere: SSR, its blur and fade,
// bloom on the emitter.
void buildReflections(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:mirror", {0.08f, 0.08f, 0.09f}, 0.08f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );
    place(
        s,
        s.cube,
        material(s, "golden:red", {0.8f, 0.1f, 0.08f}, 0.5f, 0.0f),
        {-2.5f, 1.0f, -1.5f},
        {1.2f, 2.0f, 1.2f}
    );
    place(
        s,
        s.cube,
        material(s, "golden:green", {0.1f, 0.7f, 0.2f}, 0.3f, 0.0f),
        {2.5f, 0.75f, -2.0f},
        {1.5f, 1.5f, 1.5f}
    );
    place(
        s,
        s.sphere,
        material(s, "golden:glow", {1.0f, 1.0f, 1.0f}, 0.5f, 0.0f, {6.0f, 4.0f, 1.5f}),
        {0.0f, 0.8f, -3.0f},
        glm::vec3(1.2f)
    );
    sunAndCamera(s, {0.0f, 2.2f, 7.0f}, {0.0f, 0.6f, -1.5f});
}

// A polished floor and a clear-coated sphere, seen where the sun's reflection lands:
// a smooth surface's GGX peak and the sun's disc - the glint - bright enough to test
// the bloom above it.
void buildGlint(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:polished", {0.05f, 0.05f, 0.06f}, 0.05f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {30.0f, 1.0f, 30.0f}
    );

    MaterialAsset coat;
    coat.albedo             = glm::vec4(0.6f, 0.05f, 0.05f, 1.0f);
    coat.roughness          = 0.6f;
    coat.clearcoat          = 1.0f;
    coat.clearcoatRoughness = 0.0f;
    place(s, s.sphere, s.resources.add(std::move(coat), "golden:coat"), {2.6f, 0.7f, 2.4f}, glm::vec3(1.4f));
    place(
        s,
        s.cube,
        material(s, "golden:block", {0.7f, 0.7f, 0.65f}, 0.5f, 0.0f),
        {-0.6f, 0.75f, 3.6f},
        {1.0f, 1.5f, 1.0f}
    );

    // Toward the floor's mirror of the sun: along its azimuth, down at its elevation.
    const glm::vec3 sun = s.scene.environment().sunDirection();
    const glm::vec3 eye = {0.0f, 3.0f, 0.0f};
    sunAndCamera(s, eye, eye + glm::vec3(sun.x, -sun.y, sun.z) * (eye.y / sun.y));
}

// Smoke between the eye and a glossy floor's reflection of a lit block: blended
// particles dim the reflection as they dim the floor, not letting it through at full.
void buildSmoke(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:mirror", {0.06f, 0.06f, 0.07f}, 0.05f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );
    place(
        s,
        s.cube,
        material(s, "golden:lamp", {0.9f, 0.2f, 0.1f}, 0.5f, 0.0f, {4.0f, 0.8f, 0.3f}),
        {0.0f, 1.0f, -4.0f},
        {3.0f, 2.0f, 0.6f}
    );

    ParticleEmitter emitter;
    emitter.blend    = ParticleBlend::Alpha;
    emitter.softness = 0.6f;
    holdLook(emitter, {0.35f, 0.35f, 0.37f, 0.8f});
    std::vector<Particle>& smoke = addEmitter(s, std::move(emitter));
    for (int i = 0; i < 7; ++i) {
        Particle p;
        p.position = {-2.4f + 0.8f * static_cast<float>(i), 0.25f, -0.6f};
        p.size     = 0.7f;
        smoke.push_back(p);
    }
    sunAndCamera(s, {0.0f, 1.2f, 3.5f}, {0.0f, 0.0f, -1.5f});
}

// Particles as a sheet's frames, turned, and streaked. Above the floor, left to right: a 2x2
// sheet's four frames, a particle born on frame 0 at each quarter of its life - red, green,
// blue, white, each with a dark bar along its top and left edges; one turned an eighth of a
// turn; then additive streaks, level, rising and coming at the eye, which barely streaks. Below
// the cards a plain disc, blended with them: a second run of the alpha batch.
void buildSprites(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:slate", {0.12f, 0.12f, 0.13f}, 0.8f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );

    constexpr uint32_t SIZE  = 64;
    constexpr uint32_t FRAME = SIZE / 2;
    constexpr uint32_t BAR   = 5;
    // Rows bottom first, as an image loads (image_loaders.cpp): the top row is y = SIZE - 1.
    const uint8_t tint[4][3] = {{230, 40, 30}, {40, 220, 60}, {40, 80, 240}, {240, 240, 240}};
    TextureAsset sheet;
    sheet.params.width  = SIZE;
    sheet.params.height = SIZE;
    sheet.pixelData.resize(SIZE * SIZE * 4);
    for (uint32_t y = 0; y < SIZE; ++y) {
        for (uint32_t x = 0; x < SIZE; ++x) {
            const uint32_t fromTop = SIZE - 1 - y;
            const uint32_t frame   = (fromTop / FRAME) * 2 + x / FRAME;
            const bool     edge    = fromTop % FRAME < BAR || x % FRAME < BAR;
            uint8_t* p = &sheet.pixelData[(y * SIZE + x) * 4];
            for (int c = 0; c < 3; ++c) p[c] = edge ? 20 : tint[frame][c];
            p[3] = 255;
        }
    }
    const TextureHandle frames = s.resources.add(std::move(sheet), "golden:sheet");

    ParticleEmitter cards;
    cards.blend        = ParticleBlend::Alpha;
    cards.texture      = frames;
    cards.sheetColumns = 2;
    cards.sheetRows    = 2;
    holdLook(cards, glm::vec4(1.0f));
    std::vector<Particle>& card = addEmitter(s, cards);
    for (int i = 0; i < 5; ++i) {
        Particle p;
        p.position = {-2.6f + 0.75f * static_cast<float>(i), 1.0f, 0.0f};
        p.size     = 0.3f;
        p.lifetime = 4.0f;
        p.age      = i < 4 ? 0.5f + static_cast<float>(i) : 0.5f;
        p.angle    = i < 4 ? 0.0f : glm::quarter_pi<float>();
        card.push_back(p);
    }

    ParticleEmitter puff;
    puff.blend = ParticleBlend::Alpha;
    holdLook(puff, {0.9f, 0.8f, 0.2f, 1.0f});
    Particle disc;
    disc.position = {-1.5f, 0.45f, 0.3f};
    disc.size     = 0.25f;
    addEmitter(s, puff).push_back(disc);

    ParticleEmitter sparks;
    sparks.render   = ParticleRender::Stretched;
    sparks.softness = 0.3f;
    sparks.stretch  = 0.1f;
    holdLook(sparks, {1.0f, 1.6f / 3.0f, 0.4f / 3.0f, 1.0f}, 3.0f);
    std::vector<Particle>& spark = addEmitter(s, sparks);
    const glm::vec3 headings[3] = {{8.0f, 0.0f, 0.0f}, {0.0f, 8.0f, 0.0f}, {0.0f, 0.0f, 8.0f}};
    for (int i = 0; i < 3; ++i) {
        Particle p;
        p.position = {1.8f + 0.8f * static_cast<float>(i), 1.0f, 0.0f};
        p.size     = 0.05f;
        p.velocity = headings[i];
        spark.push_back(p);
    }
    sunAndCamera(s, {0.0f, 1.3f, 4.0f}, {0.0f, 1.0f, 0.0f});
}

// A wall of @p stripes upright stripes, light and dark by turns, from @p firstX rightward, at
// height @p y.
void addStripedWall(Scenery& s, int stripes, float firstX, float y) {
    const MaterialHandle light = material(s, "golden:chalk", {0.8f, 0.8f, 0.75f}, 0.8f, 0.0f);
    const MaterialHandle dark  = material(s, "golden:ink", {0.05f, 0.08f, 0.2f}, 0.8f, 0.0f);
    for (int i = 0; i < stripes; ++i) {
        const float x = firstX + 0.3f * static_cast<float>(i);
        place(s, s.cube, i % 2 == 0 ? light : dark, {x, y, -1.5f}, {0.3f, 3.0f, 0.2f});
    }
}

// What draws past a card. Before a wall of upright stripes, left to right: an additive ribbon
// arching through eight particles, thinning as they age; an additive trail behind one particle
// along a curve, narrowing and fading; a heat-haze card bending the stripes behind it; and a puff
// of smoke beside a lamp, shaded round as a ball rather than flat as a card.
void buildVfx(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:slate", {0.12f, 0.12f, 0.13f}, 0.8f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );
    addStripedWall(s, 24, -3.45f, 1.5f);

    ParticleEmitter ribbon;
    ribbon.render = ParticleRender::Ribbon;
    holdLook(ribbon, {1.0f, 0.45f, 0.1f, 1.0f}, 2.0f);
    ribbon.sizeOverLife.keys = {{0.0f, 1.0f}, {1.0f, 0.2f}};
    std::vector<Particle>& arch = addEmitter(s, ribbon);
    for (int i = 0; i < 8; ++i) {
        const float t = static_cast<float>(i) / 7.0f;
        Particle p;
        p.position = {-2.8f + 1.2f * t, 0.6f + std::sin(t * glm::pi<float>()) * 1.0f, 0.0f};
        p.size     = 0.08f;
        p.lifetime = 1.0f;
        p.age      = 1.0f - t;  // the oldest first, at the arch's left foot
        arch.push_back(p);
    }

    ParticleEmitter trail;
    trail.render = ParticleRender::Trail;
    holdLook(trail, {0.2f, 0.8f, 1.0f, 1.0f}, 2.0f);
    LiveParticles::Pool& streak = addEmitterPool(s, trail);
    Particle head;
    head.position   = {-0.4f, 1.8f, 0.0f};
    head.size       = 0.06f;
    head.trailCount = LiveParticles::TRAIL_POINTS;
    streak.particles.push_back(head);
    for (uint32_t k = 0; k < LiveParticles::TRAIL_POINTS; ++k) {
        const float t = (static_cast<float>(k) + 1.0f) / static_cast<float>(LiveParticles::TRAIL_POINTS);
        streak.trails.push_back(head.position + glm::vec3(-0.6f * t + 0.3f * t * t, -1.2f * t, 0.0f));
    }

    ParticleEmitter haze;
    haze.blend      = ParticleBlend::Distortion;
    haze.distortion = 0.04f;
    holdLook(haze, glm::vec4(1.0f));
    Particle shimmer;
    shimmer.position = {0.9f, 1.2f, 0.0f};
    shimmer.size     = 0.6f;
    addEmitter(s, haze).push_back(shimmer);

    ParticleEmitter smoke;
    smoke.blend = ParticleBlend::Alpha;
    holdLook(smoke, {0.45f, 0.45f, 0.45f, 1.0f});
    Particle ball;
    ball.position = {2.4f, 1.2f, 0.0f};
    ball.size     = 0.5f;
    addEmitter(s, smoke).push_back(ball);
    addLamp(s, {3.4f, 1.2f, 0.0f}, 1.5f, 2.5f, false);

    sunAndCamera(s, {0.0f, 1.3f, 4.0f}, {0.0f, 1.2f, 0.0f});
}

// A weapon held into a wall. The eye stands 0.4 m from a wall, wide (100 degrees); two blocks are
// held @p depth ahead, into the wall: on the right one seen by @p seenBy, on the left an EyeOnly
// one, which the wall hides. Lit by the sun and by a lamp beside the right one, which glows @p glow.
void buildHeld(Scenery& s, SeenBy seenBy, float depth, glm::vec3 glow = glm::vec3(0.0f)) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:ground", {0.4f, 0.4f, 0.38f}, 0.8f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );
    const MaterialHandle plaster = material(s, "golden:wall", {0.55f, 0.5f, 0.45f}, 0.7f, 0.0f);
    place(s, s.cube, plaster, {0.0f, 1.5f, -0.9f}, {6.0f, 3.0f, 1.0f});

    const glm::vec3 eye = {0.0f, 1.6f, 0.0f};
    const auto held = [&](float x, SeenBy seenBy, MaterialHandle paint) {
        const EntityId id = s.scene.createEntity();
        Transform at;
        at.position = eye + glm::vec3(x, -0.2f, depth);
        at.scale    = {0.12f, 0.12f, 0.4f};
        s.scene.add(id, std::move(at));
        Mesh mesh{s.cube, paint};
        mesh.seenBy = seenBy;
        s.scene.add(id, std::move(mesh));
    };
    held(0.22f, seenBy, material(s, "golden:gunmetal", {0.6f, 0.62f, 0.66f}, 0.35f, 1.0f, glow));
    held(-0.22f, SeenBy::EyeOnly, material(s, "golden:red", {0.8f, 0.1f, 0.08f}, 0.5f, 0.0f));

    // Its light is found where it is: drawn through its own lens, at the depth of the near plane, it
    // lands in clusters this lamp - a hand's breadth over its tip, reaching 20 cm - never touches.
    Light& bulb = addLamp(s, eye + glm::vec3(0.22f, -0.08f, -0.37f), 0.15f, 0.2f, false);
    bulb.color = {1.0f, 0.55f, 0.2f};

    sunAndCamera(s, eye, eye + glm::vec3(0.0f, 0.0f, -1.0f));
    s.scene.forEach<Camera>([](EntityId, Camera& lens) {
        lens.fovY          = glm::radians(100.0f);
        lens.viewmodelFovY = glm::radians(50.0f);
        lens.zNear         = 0.1f;
    });
}

// The right block on the viewmodel layer, its tip out of the wall: drawn whole, in front of it,
// with a muzzle flash past its far end, inside the wall, on the layer too.
void buildViewmodel(Scenery& s) {
    buildHeld(s, SeenBy::Viewmodel, -0.55f);

    ParticleEmitter flash;
    flash.seenBy   = SeenBy::Viewmodel;
    flash.softness = 0.5f;
    holdLook(flash, {1.0f, 0.6f, 0.2f, 1.0f}, 4.0f);
    Particle spark;
    spark.position = glm::vec3(0.22f, 1.45f, -0.9f);
    spark.size     = 0.05f;
    addEmitter(s, std::move(flash)).push_back(spark);
}

// A card cut by its albedo alpha, in the sun on a glossy floor: its shadow is the cut
// pattern, and its holes show the floor rather than a reflection traced from inside.
void buildCutout(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:sheen", {0.3f, 0.32f, 0.3f}, 0.2f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );

    // A checker of opaque and cut squares.
    constexpr uint32_t SIZE   = 64;
    constexpr uint32_t SQUARE = 21;
    TextureAsset holes;
    holes.params.width  = SIZE;
    holes.params.height = SIZE;
    holes.pixelData.resize(SIZE * SIZE * 4);
    for (uint32_t y = 0; y < SIZE; ++y) {
        for (uint32_t x = 0; x < SIZE; ++x) {
            uint8_t* p = &holes.pixelData[(y * SIZE + x) * 4];
            p[0] = 230;
            p[1] = 200;
            p[2] = 120;
            p[3] = ((x / SQUARE + y / SQUARE) % 2 == 0) ? 255 : 0;
        }
    }
    MaterialAsset card;
    card.type          = MaterialType::AlphaMask;
    card.albedoTexture = s.resources.add(std::move(holes), "golden:holes");
    card.roughness     = 0.5f;
    place(
        s,
        s.cube,
        s.resources.add(std::move(card), "golden:card"),
        {0.0f, 1.2f, 0.0f},
        {2.4f, 2.4f, 0.05f}
    );

    // From the shadow's side, so the card's back and its shadow share the frame.
    sunAndCamera(s, {-1.8f, 3.2f, -4.6f}, {-0.4f, 0.5f, -0.6f});
}

// Two decals on the floor, one in sun and one in a wall's shadow: each as bright as its
// floor, the shaded one lit by the floor's sky light, not a flat ambient of its own.
void buildDecals(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:ground", {0.5f, 0.5f, 0.48f}, 0.8f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );
    place(
        s,
        s.cube,
        material(s, "golden:wall", {0.6f, 0.6f, 0.6f}, 0.8f, 0.0f),
        {-1.2f, 1.5f, -0.6f},
        {0.4f, 3.0f, 6.0f}
    );

    TextureAsset paint;
    paint.params.width  = 4;
    paint.params.height = 4;
    for (int i = 0; i < 16; ++i) paint.pixelData.insert(paint.pixelData.end(), {200, 200, 200, 230});
    MaterialAsset decal;
    decal.type          = MaterialType::Transparent;
    decal.albedo        = glm::vec4(0.9f, 0.15f, 0.1f, 1.0f);
    decal.albedoTexture = s.resources.add(std::move(paint), "golden:paint");
    const MaterialHandle paintMaterial = s.resources.add(std::move(decal), "golden:decal");

    // Projecting straight down: the box's forward, -Z, turned onto -Y.
    for (const glm::vec3 at : {glm::vec3(-2.0f, 0.0f, -1.2f), glm::vec3(-2.6f, 0.0f, 1.6f)}) {
        const EntityId id = s.scene.createEntity();
        Transform pose;
        pose.position = at;
        pose.rotation = glm::quatLookAt(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
        pose.scale    = {1.2f, 1.2f, 1.0f};
        s.scene.add(id, std::move(pose));
        Decal projector;
        projector.material = paintMaterial;
        s.scene.add(id, std::move(projector));
    }
    sunAndCamera(s, {-4.0f, 5.0f, 4.5f}, {-0.8f, 0.0f, -0.6f});
}

// Froxel fog over a floor, a long wall's shadow across it: the fog in shadow still
// holds the sky's light, as the wall does, rather than going black.
void buildFog(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:field", {0.4f, 0.42f, 0.38f}, 0.8f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {60.0f, 1.0f, 60.0f}
    );
    place(
        s,
        s.cube,
        material(s, "golden:rampart", {0.6f, 0.58f, 0.55f}, 0.8f, 0.0f),
        {-3.0f, 3.0f, -6.0f},
        {1.0f, 6.0f, 24.0f}
    );

    FogSettings& fog = s.scene.environment().fog;
    fog.enabled = true;
    fog.density = 0.08f;
    fog.height  = 2.0f;
    sunAndCamera(s, {6.0f, 2.0f, 8.0f}, {-4.0f, 1.0f, -10.0f});
}

// A glossy floor under a lit block with no sky to bake: the BRDF table is the
// backend's from the start, so SSR still reflects the block and the light keeps its
// multiple-scattering energy.
void buildNoSky(Scenery& s) {
    base(s);
    s.scene.environment().sky.procedural = false;
    place(
        s,
        s.cube,
        material(s, "golden:lacquer", {0.05f, 0.05f, 0.05f}, 0.08f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );
    place(
        s,
        s.cube,
        material(s, "golden:lantern", {0.9f, 0.7f, 0.2f}, 0.5f, 0.0f, {3.0f, 2.0f, 0.4f}),
        {0.0f, 1.0f, -3.0f},
        {2.0f, 2.0f, 2.0f}
    );
    place(
        s,
        s.sphere,
        material(s, "golden:brass", {0.95f, 0.78f, 0.4f}, 0.6f, 1.0f),
        {2.2f, 0.7f, -1.0f},
        glm::vec3(1.4f)
    );
    sunAndCamera(s, {0.0f, 1.8f, 4.0f}, {0.0f, 0.4f, -1.5f});
}

// A tall tower under a low, wide sun, seen at the far end of its shadow: a penumbra
// wider than the point's own cascade holds is filtered in a coarser one, and stays
// within what its blocker search covered, so the shadow neither drops out nor tears.
// The long shadow distance makes the coarse cascades coarse enough to show it.
void buildPenumbra(Scenery& s) {
    base(s);
    s.scene.environment().sky.procedural   = false;
    s.scene.environment().sky.sunElevation = 18.0f;
    s.scene.environment().sky.sunAzimuth   = 100.0f;
    place(
        s,
        s.cube,
        material(s, "golden:slate", {0.45f, 0.45f, 0.45f}, 0.8f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {80.0f, 1.0f, 80.0f}
    );
    constexpr float TOWER_HEIGHT = 8.0f;
    place(
        s,
        s.cube,
        material(s, "golden:tower", {0.6f, 0.6f, 0.62f}, 0.7f, 0.0f),
        {0.0f, TOWER_HEIGHT * 0.5f, 0.0f},
        {3.0f, TOWER_HEIGHT, 3.0f}
    );

    const glm::vec3 sun    = s.scene.environment().sunDirection();
    const glm::vec3 tip    = glm::vec3(0.0f, TOWER_HEIGHT, 0.0f) - sun * (TOWER_HEIGHT / sun.y);
    const glm::vec3 across = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), sun));
    sunAndCamera(s, tip + across * 2.0f + glm::vec3(0.0f, 1.2f, 0.0f), tip);
    s.scene.forEach<Light>([](EntityId, Light& light) {
        light.sourceRadius   = 0.05f;
        light.shadowDistance = 1000.0f;
    });
}

// A bulb-sized spot light over a box on a post, at night: the box's shadow is as soft
// as the bulb makes it, from a light that is not the sun.
void buildSpot(Scenery& s) {
    base(s);
    s.scene.environment().sky.procedural = false;
    place(
        s,
        s.cube,
        material(s, "golden:slab", {0.6f, 0.6f, 0.58f}, 0.8f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );
    place(
        s,
        s.cube,
        material(s, "golden:post", {0.5f, 0.4f, 0.3f}, 0.6f, 0.0f),
        {0.0f, 0.6f, 0.0f},
        {0.12f, 1.2f, 0.12f}
    );
    place(
        s,
        s.cube,
        material(s, "golden:crate", {0.7f, 0.5f, 0.3f}, 0.6f, 0.0f),
        {0.0f, 1.45f, 0.0f},
        {0.9f, 0.5f, 0.9f}
    );

    const EntityId lamp = s.scene.createEntity();
    Transform pose;
    pose.position = {0.6f, 4.0f, 0.4f};
    pose.rotation = glm::quatLookAt(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    s.scene.add(lamp, std::move(pose));
    Light light;
    light.type           = LightType::Spot;
    light.intensity      = 60.0f;
    light.radius         = 12.0f;
    light.outerConeAngle = 1.0f;
    light.innerConeAngle = 0.8f;
    light.sourceRadius   = 0.4f;
    s.scene.add(lamp, std::move(light));
    camera(s, {2.2f, 2.4f, 3.0f}, {-0.2f, 0.2f, -0.2f});
}

// A bulb-sized point light in an arcade, at night: round pillars lit to their terminators
// with no acne, their shadows soft as the bulb makes them and hard at contact, and a cloth
// hung 2 cm off the wall still shadowing it, which a bias too large for its gap would lose.
void buildPoint(Scenery& s) {
    base(s);
    s.scene.environment().sky.procedural = false;
    const MaterialHandle stone = material(s, "golden:stone", {0.7f, 0.66f, 0.6f}, 0.8f, 0.0f);
    const MaterialHandle cloth = material(s, "golden:cloth", {0.7f, 0.2f, 0.15f}, 0.9f, 0.0f);
    const MeshHandle pillar = s.resources.add(generateCylinder(0.5f, 1.0f, 48), "golden:pillar");
    place(s, s.cube, stone, {0.0f, -0.5f, 0.0f}, {30.0f, 1.0f, 30.0f});
    place(s, s.cube, stone, {0.0f, 3.0f, -3.0f}, {14.0f, 6.0f, 0.5f});
    place(s, s.cube, cloth, {-0.4f, 2.0f, -2.73f}, {1.2f, 2.4f, 0.02f});
    for (float x : {-2.4f, 1.6f}) place(s, pillar, stone, {x, 1.8f, -0.8f}, {0.6f, 3.6f, 0.6f});

    Light& bulb = addLamp(s, {1.4f, 2.0f, 0.9f}, 40.0f, 15.0f, true);
    bulb.color        = {1.0f, 0.78f, 0.55f};
    bulb.sourceRadius = 0.12f;
    camera(s, {-0.6f, 1.9f, 3.6f}, {-0.2f, 1.4f, -2.0f});
}

// A sphere and a box beside their mirror images (a negative x scale), under the sun: mirroring
// reverses the winding, and the mirrored pair must still draw right side out.
void buildMirror(Scenery& s) {
    base(s);
    s.scene.environment().sky.sunElevation = 35.0f;
    s.scene.environment().sky.sunAzimuth   = 40.0f;
    const MaterialHandle stone = material(s, "golden:stone", {0.7f, 0.66f, 0.6f}, 0.5f, 0.0f);
    const MaterialHandle red   = material(s, "golden:red", {0.8f, 0.2f, 0.15f}, 0.4f, 0.0f);
    place(s, s.cube, stone, {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f});
    for (float side : {-1.0f, 1.0f}) {
        place(s, s.sphere, red, {1.2f * side, 0.8f, 0.0f}, {1.6f * side, 1.6f, 1.6f});
        place(s, s.cube, stone, {1.2f * side, 2.2f, 0.0f}, {1.0f * side, 0.6f, 0.6f});
    }
    sunAndCamera(s, {0.0f, 2.0f, 5.0f}, {0.0f, 1.2f, 0.0f});
}

// Cards and glass seen from behind, under the sun. A one-sided card shows nothing from its
// back; a double-sided one is lit as its own surface; a double-sided glass sphere shows its
// near side over its far one, and the block behind through both.
void buildTwoSided(Scenery& s) {
    base(s);
    const MeshHandle card = s.resources.add(generatePlane(1.0f, 1.0f), "golden:card");
    place(
        s,
        s.cube,
        material(s, "golden:stone", {0.7f, 0.66f, 0.6f}, 0.6f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {30.0f, 1.0f, 30.0f}
    );
    place(
        s,
        s.cube,
        material(s, "golden:red", {0.8f, 0.2f, 0.15f}, 0.4f, 0.0f),
        {2.0f, 1.0f, -2.5f},
        {1.6f, 2.0f, 0.4f}
    );

    // Upright, their fronts facing away from the eye.
    for (const bool twoSided : {false, true}) {
        MaterialAsset cloth;
        cloth.albedo      = glm::vec4(0.2f, 0.45f, 0.8f, 1.0f);
        cloth.roughness   = 0.8f;
        cloth.doubleSided = twoSided;
        const EntityId id = s.scene.createEntity();
        Transform t;
        t.position = {twoSided ? -0.6f : -2.6f, 1.1f, 0.0f};
        t.rotation = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
        t.scale    = glm::vec3(1.6f);
        s.scene.add(id, std::move(t));
        Mesh m;
        m.mesh     = card;
        m.material = s.resources.add(std::move(cloth), twoSided ? "golden:cloth2" : "golden:cloth1");
        s.scene.add(id, std::move(m));
    }

    MaterialAsset glass;
    glass.albedo       = glm::vec4(0.9f, 0.95f, 1.0f, 1.0f);
    glass.roughness    = 0.05f;
    glass.transmission = 1.0f;
    glass.doubleSided  = true;
    place(
        s,
        s.sphere,
        s.resources.add(std::move(glass), "golden:glass"),
        {2.0f, 1.0f, 0.0f},
        glm::vec3(1.6f)
    );

    sunAndCamera(s, {0.0f, 1.6f, 5.0f}, {0.0f, 1.0f, -0.5f});
}

// Clear-coated spheres over a strongly bumped base, beside one with no coat, toward the
// sun's mirror: the coat's highlight is one sharp spot over the bumps, not broken by them.
void buildCoat(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:floor", {0.3f, 0.3f, 0.3f}, 0.7f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {30.0f, 1.0f, 30.0f}
    );

    // Stripes tilted 30 degrees either way along u, four texels each.
    constexpr uint32_t SIZE = 64;
    TextureAsset bumps;
    bumps.params.width          = SIZE;
    bumps.params.height         = SIZE;
    bumps.params.internalFormat = TextureInternalFormat::RG8;
    bumps.params.format         = inferFormat(2);
    bumps.params.wrapS          = TextureWrapMode::Repeat;
    bumps.params.wrapT          = TextureWrapMode::Repeat;
    bumps.pixelData.resize(SIZE * SIZE * 2);
    for (uint32_t y = 0; y < SIZE; ++y) {
        for (uint32_t x = 0; x < SIZE; ++x) {
            bumps.pixelData[(y * SIZE + x) * 2 + 0] = ((x / 4) % 2 == 0) ? 192 : 64;
            bumps.pixelData[(y * SIZE + x) * 2 + 1] = 128;
        }
    }
    const TextureHandle normal = s.resources.add(std::move(bumps), "golden:bumps");

    const glm::vec3 sun    = s.scene.environment().sunDirection();
    const glm::vec3 eye    = {0.0f, 2.0f, 0.0f};
    const glm::vec3 toward = glm::normalize(glm::vec3(sun.x, 0.0f, sun.z));
    const glm::vec3 across = glm::cross(toward, glm::vec3(0.0f, 1.0f, 0.0f));
    for (const bool coated : {false, true}) {
        MaterialAsset paint;
        paint.albedo             = glm::vec4(0.05f, 0.12f, 0.4f, 1.0f);
        paint.roughness          = 0.45f;
        paint.normalTexture      = normal;
        paint.clearcoat          = coated ? 1.0f : 0.0f;
        paint.clearcoatRoughness = 0.02f;
        const glm::vec3 at = eye + toward * 3.2f + across * (coated ? 1.05f : -1.05f);
        place(
            s,
            s.sphere,
            s.resources.add(std::move(paint), coated ? "golden:coated" : "golden:bare"),
            {at.x, 0.9f, at.z},
            glm::vec3(1.8f)
        );
    }
    sunAndCamera(s, eye, eye + toward * 3.2f + glm::vec3(0.0f, -1.1f, 0.0f));
}

// The sun four degrees up, looked toward across a floor: the horizon's glow, the sky's
// gradient above it and the long shadows, which the air's multiple scattering and ozone
// color most at a low sun.
void buildSunset(Scenery& s) {
    base(s);
    s.scene.environment().sky.sunElevation = 4.0f;
    place(
        s,
        s.cube,
        material(s, "golden:sand", {0.5f, 0.45f, 0.38f}, 0.9f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {40.0f, 1.0f, 40.0f}
    );
    const MaterialHandle stone = material(s, "golden:stone", {0.6f, 0.6f, 0.62f}, 0.7f, 0.0f);
    const MaterialHandle steel = material(s, "golden:steel", {0.9f, 0.9f, 0.9f}, 0.2f, 1.0f);
    place(s, s.cube, stone, {-2.5f, 1.0f, 0.0f}, {1.0f, 2.0f, 1.0f});
    place(s, s.sphere, steel, {1.5f, 0.8f, 1.0f}, glm::vec3(1.6f));

    const glm::vec3 sun    = s.scene.environment().sunDirection();
    const glm::vec3 toward = glm::normalize(glm::vec3(sun.x, 0.0f, sun.z));
    const glm::vec3 eye    = glm::vec3(0.0f, 1.5f, 0.0f) - toward * 7.0f;
    sunAndCamera(s, eye, eye + toward * 10.0f + glm::vec3(0.0f, 1.2f, 0.0f));
}

// A closed room under the sky and sun, an irradiance volume filling it, with a doorway in its
// +Z wall and a window in its +X wall, or neither. Inside 8 x 4 x 8 metres, walls 0.4 thick,
// standing on the open ground.
void buildRoomShell(Scenery& s, bool openings) {
    base(s);
    s.scene.environment().sky.sunElevation = 35.0f;
    s.scene.environment().sky.sunAzimuth   = 45.0f;
    const MaterialHandle ground  = material(s, "golden:ground", {0.45f, 0.42f, 0.38f}, 0.9f, 0.0f);
    const MaterialHandle plaster = material(s, "golden:plaster", {0.7f, 0.68f, 0.64f}, 0.9f, 0.0f);
    place(s, s.cube, ground, {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f});
    place(s, s.cube, plaster, {0.0f, 4.2f, 0.0f}, {8.8f, 0.4f, 8.8f});
    place(s, s.cube, plaster, {-4.2f, 2.0f, 0.0f}, {0.4f, 4.0f, 8.8f});
    place(s, s.cube, plaster, {0.0f, 2.0f, -4.2f}, {8.0f, 4.0f, 0.4f});
    if (openings) {
        // A doorway 1.6 wide and 2.6 high, and a window 2.4 wide and 1.4 high over a 1 m sill.
        place(s, s.cube, plaster, {-2.4f, 2.0f, 4.2f}, {3.2f, 4.0f, 0.4f});
        place(s, s.cube, plaster, {2.4f, 2.0f, 4.2f}, {3.2f, 4.0f, 0.4f});
        place(s, s.cube, plaster, {0.0f, 3.3f, 4.2f}, {1.6f, 1.4f, 0.4f});
        place(s, s.cube, plaster, {4.2f, 2.0f, -2.8f}, {0.4f, 4.0f, 3.2f});
        place(s, s.cube, plaster, {4.2f, 2.0f, 2.8f}, {0.4f, 4.0f, 3.2f});
        place(s, s.cube, plaster, {4.2f, 0.5f, 0.0f}, {0.4f, 1.0f, 2.4f});
        place(s, s.cube, plaster, {4.2f, 3.2f, 0.0f}, {0.4f, 1.6f, 2.4f});
    } else {
        place(s, s.cube, plaster, {0.0f, 2.0f, 4.2f}, {8.0f, 4.0f, 0.4f});
        place(s, s.cube, plaster, {4.2f, 2.0f, 0.0f}, {0.4f, 4.0f, 8.8f});
    }

    // To the middle of the walls, and fading in over less than that, so no inner face, corners
    // included, reads the sky through the fade.
    const EntityId volume = s.scene.createEntity();
    Transform box;
    box.position = {0.0f, 2.0f, 0.0f};
    s.scene.add(volume, std::move(box));
    IrradianceVolume grid;
    grid.halfExtents   = {4.2f, 2.2f, 4.2f};
    grid.resolutionX   = 8;
    grid.resolutionY   = 4;
    grid.resolutionZ   = 8;
    grid.blendDistance = 0.1f;
    s.scene.add(volume, std::move(grid));
}

// The sun, and the eye beside buildRoomShell's doorway looking toward the far corner.
void sunAndRoomCamera(Scenery& s) {
    sunAndCamera(s, {3.0f, 1.7f, 3.0f}, {-2.5f, 1.2f, -3.0f});
}

// buildRoomShell's room, with or without its @p openings, seen by sunAndRoomCamera's eye.
void buildRoomInView(Scenery& s, bool openings) {
    buildRoomShell(s, openings);
    sunAndRoomCamera(s);
}

// A chrome ball in the room with its openings, in a reflection probe filling the room.
void buildRoomBall(Scenery& s) {
    buildRoomShell(s, true);
    place(
        s,
        s.sphere,
        material(s, "golden:chrome", {0.95f, 0.95f, 0.95f}, 0.15f, 1.0f),
        {0.4f, 0.75f, 0.2f},
        glm::vec3(1.5f)
    );

    const EntityId probe = s.scene.createEntity();
    Transform at;
    at.position = {0.0f, 2.0f, 0.0f};
    s.scene.add(probe, std::move(at));
    ReflectionProbe capture;
    capture.halfExtents = {4.0f, 2.0f, 4.0f};
    capture.resolution  = 64;
    s.scene.add(probe, std::move(capture));
}

// The ball's room seen from beside the doorway toward the far corner: the walls, and the walls
// in the ball, are lit by the sunlit floor and what the openings let in, the far corners darkest,
// not by the sky through the walls.
void buildRoom(Scenery& s) {
    buildRoomBall(s);
    sunAndRoomCamera(s);
}

// Blocks 200 m, 1 km and 3 km off under the default sky, the ground running out to the horizon:
// the air between fades each further into the sky behind it.
void buildAerial(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:meadow", {0.25f, 0.3f, 0.18f}, 0.9f, 0.0f),
        {0.0f, -0.5f, -9000.0f},
        {20000.0f, 1.0f, 20000.0f}
    );
    const MaterialHandle slate = material(s, "golden:slate", {0.08f, 0.08f, 0.09f}, 0.8f, 0.0f);
    place(s, s.cube, slate, {25.0f, 20.0f, -200.0f}, {40.0f, 40.0f, 40.0f});
    place(s, s.cube, slate, {-250.0f, 75.0f, -1000.0f}, {150.0f, 150.0f, 150.0f});
    place(s, s.cube, slate, {1500.0f, 200.0f, -3000.0f}, {400.0f, 400.0f, 400.0f});
    sunAndCamera(s, {0.0f, 2.0f, 0.0f}, {0.0f, 12.0f, -100.0f});
    s.scene.forEach<Camera>([](EntityId, Camera& lens) { lens.zFar = 5000.0f; });
    s.settings.cullMaxDistance = 0.0f;
}

// The frame, rendered and read back as RenderSystem does for a screenshot: top row
// first, as a PNG stores. @p ui is the overlay UISystem would have published, if any,
// and @p splash the logo SplashSystem would have.
std::vector<unsigned char> render(
    Scenery& s,
    GLBackend& backend,
    const UIDrawData* ui = nullptr,
    const RenderSettings& settings = RenderSettings{},
    const SplashFrame* splash = nullptr,
    int frames = FRAMES,
    const std::function<void(int frame, const Visibility& seen)>& afterFrame = {}
) {
    Clock          clock;
    EventBus       events;
    WindowManager  window;   // never opened: the pbuffer is the surface
    InputMap       input;
    NetSession     net;
    HostChrome     chrome;
    RenderSettings tuning = settings;
    RenderSettings project = settings;
    FrameContext   ctx{s.scene, s.resources, clock, events, window, input, net, chrome, tuning, project};
    ctx.poses = g_poses;
    clock.requestStep(g_worldTicks);
    while (clock.consumeFixedStep()) {}

    SkySystem        sky;
    OceanSystem      ocean;
    TerrainSystem    terrain;
    VisibilitySystem visibility;
    RenderView       view;
    view.viewportWidth  = WIDTH;
    view.viewportHeight = HEIGHT;
    view.surfaceWidth   = WIDTH;
    view.surfaceHeight  = HEIGHT;
    view.settings       = tuning;
    view.frameSeconds   = FRAME_SECONDS;
    view.bakeBudget     = g_bakeBudget;
    view.worldArriving  = g_worldArriving;
    // Every frame takes the list, empty or not, as the engine's does: none changes nothing.
    view.debug.lines    = &s.debugLines;
    view.debug.seconds  = s.debugSeconds;

    for (int frame = 0; frame < frames; ++frame) {
        sky.update(ctx);
        ocean.update(ctx);
        terrain.update(ctx);
        visibility.update(ctx);
        view.build(s.scene, *ctx.visibility, ui, nullptr, splash, ctx.poses, &s.particles, ctx.ocean);
        backend.render(view, s.resources);
        if (afterFrame) afterFrame(frame, *ctx.visibility);
    }

    std::vector<unsigned char> image;
    backend.readFrame(view, image);
    return image;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    return line;
}

// Puts the fixed-function state back where a new GL context starts it.
//
// Each scene has its own backend in the one context, and Vkm::GL::Context caches state
// assuming GL starts at defaults, never reading them. What the last scene left - a
// front-face cull, depth writes off - the next backend would take for defaults and skip.
void restoreContextDefaults() {
    glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
    glClearDepth(1.0);
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// The share of pixels where two frames differ by more than a driver update would, over their
// top @p rows.
float differingShare(const std::vector<unsigned char>& frame, const unsigned char* other, int rows = HEIGHT) {
    size_t differing = 0;
    for (size_t px = 0; px < static_cast<size_t>(WIDTH) * rows; ++px) {
        for (int c = 0; c < 3; ++c) {
            if (std::abs(int(frame[px * 3 + c]) - int(other[px * 3 + c])) > CHANNEL_TOLERANCE) {
                ++differing;
                break;
            }
        }
    }
    return static_cast<float>(differing) / static_cast<float>(WIDTH * rows);
}

// How far two frames' channels are apart on average, in levels of 255.
double meanLevelsApart(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    double sum = 0.0;
    for (size_t at = 0; at < a.size(); ++at) sum += std::abs(int(a[at]) - int(b[at]));
    return sum / static_cast<double>(a.size());
}

// @p culled draws with the GPU occlusion cull on, against the same golden: it may change no frame.
void compare(
    const char* name,
    void (*build)(Scenery&),
    const std::string& renderer,
    int& failures,
    bool culled = false
) {
    const std::string           label = culled ? std::string(name) + ", culled" : std::string(name);
    const std::filesystem::path png   = GOLDEN_DIR / (std::string(name) + ".png");
    const std::filesystem::path gpu   = GOLDEN_DIR / (std::string(name) + ".gpu");
    const bool                  write = std::getenv("VKM_UPDATE_GOLDENS") != nullptr;
    if (write && culled) return;

    // Asked before anything is drawn: a golden made on another GPU is not compared against.
    if (!write) {
        const std::string madeOn = readFile(gpu);
        if (madeOn.empty()) {
            ++failures;
            const char* missing = "<-- FAILED: no golden (VKM_UPDATE_GOLDENS=1 makes one)";
            std::printf("  %-62s %s\n", label.c_str(), missing);
            return;
        }
        if (madeOn != renderer) {
            std::printf("  %-62s skipped: golden made on %s\n", label.c_str(), madeOn.c_str());
            return;
        }
    }

    // A backend of its own: each scene's asset handles restart where the last one's
    // did, which an outliving backend would take for the same assets.
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        ++failures;
        std::printf("  %-62s %s\n", label.c_str(), "<-- FAILED: the backend did not start");
        return;
    }

    Scenery scenery;
    build(scenery);
    scenery.settings.occlusionCulling = culled;
    const std::vector<unsigned char> frame = render(scenery, backend, nullptr, scenery.settings);

    if (write) {
        std::filesystem::create_directories(GOLDEN_DIR);
        stbi_write_png(png.string().c_str(), WIDTH, HEIGHT, 3, frame.data(), WIDTH * 3);
        std::ofstream(gpu) << renderer << "\n";
        std::printf("  %-62s %s\n", name, "written");
        return;
    }

    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* golden = stbi_load(png.string().c_str(), &w, &h, &channels, 3);
    if (!golden || w != WIDTH || h != HEIGHT) {
        if (golden) stbi_image_free(golden);
        ++failures;
        std::printf("  %-62s %s\n", label.c_str(), "<-- FAILED: golden unreadable or the wrong size");
        return;
    }

    const float share = differingShare(frame, golden);
    stbi_image_free(golden);

    const bool same = share <= PIXEL_TOLERANCE;
    if (!same) {
        ++failures;
        // Beside the log, so a failure can be looked at.
        const std::string seen = "/tmp/vkm_golden_" + std::string(name) + (culled ? "_culled" : "") + ".png";
        stbi_write_png(seen.c_str(), WIDTH, HEIGHT, 3, frame.data(), WIDTH * 3);
        std::printf("      %.2f%% of pixels differ; this frame is at %s\n", share * 100.0f, seen.c_str());
    }
    std::printf("  %-62s %s\n", label.c_str(), same ? "ok" : "<-- FAILED");
}

void expect(const char* what, bool ok, int& failures) {
    if (!ok) ++failures;
    std::printf("  %-62s %s\n", what, ok ? "ok" : "<-- FAILED");
}

// A reflective sphere inside a reflection probe, under the procedural sky.
void buildProbe(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:plinth", {0.4f, 0.4f, 0.4f}, 0.8f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );
    place(
        s,
        s.sphere,
        material(s, "golden:chrome", {0.95f, 0.95f, 0.95f}, 0.1f, 1.0f),
        {0.0f, 1.0f, 0.0f},
        glm::vec3(2.0f)
    );

    const EntityId probe = s.scene.createEntity();
    Transform at;
    at.position = {0.0f, 1.0f, 0.0f};
    s.scene.add(probe, std::move(at));
    ReflectionProbe capture;
    capture.resolution = PROBE_RESOLUTION;
    s.scene.add(probe, std::move(capture));
    sunAndCamera(s, {0.0f, 1.5f, 4.0f}, {0.0f, 1.0f, 0.0f});
}

// The frame a backend of its own draws of @p build's scene with @p settings, over @p frames frames;
// empty, the failure counted, when the backend does not start.
std::vector<unsigned char> freshFrame(
    const std::function<void(Scenery&)>& build,
    const RenderSettings& settings,
    int& failures,
    int frames = FRAMES
) {
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return {};
    }
    Scenery s;
    build(s);
    return render(s, backend, nullptr, settings, nullptr, frames);
}

// A backend keeps what it baked of a scene - the sky, a probe's capture - and must drop
// each once the scene stops asking, so a frame after a change matches a fresh backend's.
void sameAsAFreshBackend(
    const char* what,
    void (*build)(Scenery&),
    void (*before)(Scenery&),
    void (*after)(Scenery&),
    int& failures
) {
    restoreContextDefaults();
    WindowManager window;
    GLBackend lived;
    if (!lived.init(window)) {
        expect(what, false, failures);
        return;
    }
    Scenery changed;
    build(changed);
    before(changed);
    render(changed, lived);
    after(changed);
    // Held still past CAPTURE_SETTLE_FRAMES, so a capture the change moved has settled.
    render(changed, lived);
    render(changed, lived);
    const std::vector<unsigned char> seen = render(changed, lived);

    const auto only = [&](Scenery& s) {
        build(s);
        after(s);
    };
    const std::vector<unsigned char> want = freshFrame(only, RenderSettings{}, failures);
    if (want.empty()) return;

    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels differ\n", share * 100.0f);
    expect(what, share <= PIXEL_TOLERANCE, failures);
}

void testABackendDropsWhatTheSceneNoLongerAsksFor(int& failures) {
    std::printf("A backend that drew a scene before it changed:\n");

    sameAsAFreshBackend(
        "a scene that drops its sky is drawn without the old one",
        buildNoSky,
        [](Scenery& s) { s.scene.environment().sky.procedural = true; },
        [](Scenery& s) { s.scene.environment().sky.procedural = false; },
        failures
    );

    // A probe's capture is lit by the sun's map fitted to its box, so a resized
    // box is recaptured - here under a sunset a noon capture would not show.
    sameAsAFreshBackend(
        "a probe whose box changes is captured again",
        buildProbe,
        [](Scenery&) {},
        [](Scenery& s) {
            s.scene.environment().sky.sunElevation = 4.0f;
            s.scene.forEach<ReflectionProbe>([](EntityId, ReflectionProbe& probe) {
                probe.halfExtents = glm::vec3(6.0f);
            });
        },
        failures
    );

    // A probe's capture is lit by the irradiance volume, so a volume baked again - here under a
    // lower sun - is captured again with it, though the probe itself did not change.
    sameAsAFreshBackend(
        "a probe is captured again with a volume baked again",
        [](Scenery& s) {
            buildRoomBall(s);
            sunAndCamera(s, {1.6f, 1.3f, 1.5f}, {0.4f, 0.75f, 0.2f});
        },
        [](Scenery&) {},
        [](Scenery& s) {
            s.scene.environment().sky.sunElevation = 15.0f;
            s.scene.forEach<IrradianceVolume>([](EntityId, IrradianceVolume& volume) {
                ++volume.bakeVersion;
            });
        },
        failures
    );
}

// A sun that drifts a few degrees is followed a step a frame, and once the bake is complete the
// frame is the one a fresh backend draws, which bakes that sky at once. The sky drawn is current
// from the first frame: it comes from the sky-view table, not the bake. Low, where the sky
// changes fastest with the sun.
void testADriftingSunIsFollowedOverFrames(int& failures) {
    std::printf("A backend whose sun drifts:\n");

    restoreContextDefaults();
    WindowManager window;
    GLBackend followed;
    if (!followed.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery drifting;
    buildSunset(drifting);
    drifting.scene.environment().sky.sunElevation = 8.0f;
    render(drifting, followed);
    drifting.scene.environment().sky.sunElevation = 4.0f;
    const std::vector<unsigned char> early = render(drifting, followed);
    std::vector<unsigned char> seen = early;
    for (int frames = FRAMES; frames <= GLIBLBaker::slicedSteps(); frames += FRAMES) {
        seen = render(drifting, followed);
    }

    const std::vector<unsigned char> want = freshFrame(buildSunset, RenderSettings{}, failures);
    if (want.empty()) return;

    // The top rows are sky.
    const float sky = differingShare(early, want.data(), HEIGHT / 4);
    std::printf("      %.2f%% of the sky's pixels differ mid-bake\n", sky * 100.0f);
    expect("the sky is the new sun's while the bake runs", sky <= PIXEL_TOLERANCE, failures);

    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels differ\n", share * 100.0f);
    expect("the frame after its bake is a fresh backend's", share <= PIXEL_TOLERANCE, failures);
}

// A probe follows its light a step a frame into a layer no probe holds, copied in whole: once the
// refresh is through, the frame is the one a fresh backend draws, which captures the new light
// at once.
void testAProbeFollowsItsLight(int& failures) {
    std::printf("A reflection probe whose sun sets:\n");

    restoreContextDefaults();
    WindowManager window;
    GLBackend lived;
    if (!lived.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery setting;
    buildProbe(setting);
    render(setting, lived);
    setting.scene.environment().sky.sunElevation = 4.0f;
    // The refresh of buildProbe's probe, a step a frame.
    const int steps = GLProbeBaker::refreshSteps(PROBE_RESOLUTION);
    const std::vector<unsigned char> early = render(setting, lived);
    int frames = FRAMES;
    std::vector<unsigned char> midway;
    for (; frames + FRAMES < steps; frames += FRAMES) midway = render(setting, lived);
    std::vector<unsigned char> seen;
    for (; frames < steps + 2 * FRAMES; frames += FRAMES) seen = render(setting, lived);

    // Partway through, the probe is the capture it had: its faces are not the new light one by one.
    const float kept = differingShare(midway, early.data());
    std::printf("      %.2f%% of pixels differ partway through\n", kept * 100.0f);
    expect("until it is refreshed, the probe shows the capture it had", kept <= PIXEL_TOLERANCE, failures);

    const auto set = [&](Scenery& s) {
        buildProbe(s);
        s.scene.environment().sky.sunElevation = 4.0f;
    };
    const std::vector<unsigned char> want = freshFrame(set, RenderSettings{}, failures);
    if (want.empty()) return;

    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels differ\n", share * 100.0f);
    expect("  and once it is, the frame is a fresh backend's", share <= PIXEL_TOLERANCE, failures);
}

// A refreshed probe rests before it follows its light again (GLProbeManager::REFRESH_REST): a sun
// that rises again the frame after a refresh is captured once the rest is over, not a refresh
// later, and the sky's bake of it, which ends after that refresh, once the next rest is.
void testARefreshedProbeRests(int& failures) {
    std::printf("A reflection probe whose sun rises again after a refresh:\n");

    restoreContextDefaults();
    WindowManager window;
    GLBackend lived;
    if (!lived.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery setting;
    buildProbe(setting);
    render(setting, lived);
    expect(
        "the arrays have room for the probe there is, not every one a scene may hold",
        lived.probeManager().capacity() == 1,
        failures
    );
    // The refresh to the low sun runs a step a frame, and ends on the last of them.
    const int   steps = GLProbeBaker::refreshSteps(PROBE_RESOLUTION);
    const float noon  = setting.scene.environment().sky.sunElevation;
    setting.scene.environment().sky.sunElevation = 4.0f;
    const auto over = [&](int frames) {
        return render(setting, lived, nullptr, RenderSettings{}, nullptr, frames);
    };
    over(steps);

    setting.scene.environment().sky.sunElevation = noon;
    const int rest = static_cast<int>(std::ceil(GLProbeManager::REFRESH_REST / FRAME_SECONDS));
    const std::vector<unsigned char> early   = over(1);
    const std::vector<unsigned char> resting = over(steps);
    const std::vector<unsigned char> seen    = over(2 * (rest + steps) + FRAMES);

    // Without the rest, the refresh would be through by now.
    const float kept = differingShare(resting, early.data());
    std::printf("      %.2f%% of pixels differ a refresh's length on\n", kept * 100.0f);
    expect("a refresh's length on, the probe shows the capture it had", kept <= PIXEL_TOLERANCE, failures);

    const std::vector<unsigned char> want = freshFrame(buildProbe, RenderSettings{}, failures);
    if (want.empty()) return;
    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels differ\n", share * 100.0f);
    expect("  and after its rests, it follows", share <= PIXEL_TOLERANCE, failures);
}

// A frame with no camera gathers no probes; the scene has not lost them. A noon capture
// still shows at sunset after cameraless frames, as with a backend that drew every
// frame through one until its refresh, a step a frame, is through; dropped and baked
// again, it would show the sunset at once.
void testAFrameWithNoCameraKeepsTheProbesBaked(int& failures) {
    std::printf("A backend that drew frames with no camera:\n");

    const auto look = [](Scenery& s, bool active) {
        s.scene.forEach<Camera>([active](EntityId, Camera& camera) { camera.active = active; });
    };
    const auto sunset = [](Scenery& s) { s.scene.environment().sky.sunElevation = 4.0f; };

    restoreContextDefaults();
    WindowManager window;
    GLBackend steady;
    if (!steady.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery watched;
    buildProbe(watched);
    render(watched, steady);
    sunset(watched);
    const std::vector<unsigned char> want = render(watched, steady);

    restoreContextDefaults();
    GLBackend blinked;
    if (!blinked.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery away;
    buildProbe(away);
    render(away, blinked);
    look(away, false);
    render(away, blinked);
    look(away, true);
    sunset(away);
    const std::vector<unsigned char> seen = render(away, blinked);

    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels differ\n", share * 100.0f);
    expect("a probe baked before them is not baked again", share <= PIXEL_TOLERANCE, failures);
}

// One solid quad's six vertices over @p lo..hi, in UISystem's corner order; @p image
// says whether it samples its run's picture.
void solidQuad(UIDrawData& ui, glm::vec2 lo, glm::vec2 hi, glm::vec4 color, bool image = false) {
    const glm::vec4 shape(hi - lo, 0.0f, 0.0f);
    const auto push = [&](float x, float y, float u, float v) {
        ui.vertices.push_back(UIVertex{{x, y}, {u, v}, color, shape, glm::vec4(0.0f), image ? 1.0f : 0.0f});
    };
    push(lo.x, lo.y, 0.0f, 0.0f);
    push(hi.x, lo.y, 1.0f, 0.0f);
    push(hi.x, hi.y, 1.0f, 1.0f);
    push(lo.x, lo.y, 0.0f, 0.0f);
    push(hi.x, hi.y, 1.0f, 1.0f);
    push(lo.x, hi.y, 0.0f, 1.0f);
}

glm::ivec3 pixel(const std::vector<unsigned char>& frame, int x, int y) {
    const size_t at = (static_cast<size_t>(y) * WIDTH + static_cast<size_t>(x)) * 3;
    return {frame[at], frame[at + 1], frame[at + 2]};
}

// The frame's centre pixel as a grey: its channels' mean, 0..255.
float centreGrey(const std::vector<unsigned char>& frame) {
    const glm::ivec3 c = pixel(frame, WIDTH / 2, HEIGHT / 2);
    return static_cast<float>(c.x + c.y + c.z) / 3.0f;
}

// A floor running out past the frame, under the sun, looked across at a grazing angle.
void buildOpenFloor(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:open", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {200.0f, 1.0f, 200.0f}
    );
    sunAndCamera(s, {0.0f, 1.5f, 0.0f}, {0.0f, 0.0f, -6.0f});
}

// A bare floor at a grazing angle: nothing occludes it. Each GTAO slice integrates an
// oblique open surface to more than one and its neighbours to less, so it is only right
// once averaged - clamped to one first, the average darkens an empty floor.
void testAnOpenFloorIsNotOccluded(int& failures) {
    std::printf("Ambient occlusion over an open floor:\n");

    RenderSettings settings;
    settings.renderMode    = RenderMode::AmbientOcclusion;
    settings.msaaSamples   = 1;
    settings.gtaoIntensity = 3.0f;  // strong, so a bias shows
    settings.gtaoPower     = 4.0f;
    const std::vector<unsigned char> frame = freshFrame(buildOpenFloor, settings, failures);
    if (frame.empty()) return;

    // The floor fills the lower half of the frame.
    long sum   = 0;
    int  count = 0;
    int  least = 255;
    for (int y = HEIGHT / 2 + 4; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            const int value = pixel(frame, x, y).x;
            sum  += value;
            least = std::min(least, value);
            ++count;
        }
    }
    const float mean = static_cast<float>(sum) / static_cast<float>(count);
    std::printf("      mean %.1f, darkest %d\n", mean, least);
    expect("an open floor is not darkened", mean >= 240.0f, failures);
}

// Make the scene's camera orthographic, showing @p halfHeight metres above and below its centre.
void orthographic(Scenery& s, float halfHeight) {
    s.scene.forEach<Camera>([&](EntityId, Camera& camera) {
        camera.projection  = ProjectionType::Orthographic;
        camera.orthoHeight = halfHeight;
    });
}

// The mirror scene through an orthographic camera, its reflections traced and not: they differ.
// An orthographic camera's depth is linear, so the viewmodel's band at the front of it is
// VIEWMODEL_DEPTH_SHARE of the whole scene, and read as the viewmodel's, nothing in it would trace.
void testAnOrthographicViewTracesItsReflections(int& failures) {
    std::printf("Screen-space reflections through an orthographic camera:\n");
    const auto build = [](Scenery& s) {
        buildMirror(s);
        orthographic(s, 2.5f);
    };
    std::vector<unsigned char> frames[2];
    for (const bool ssr : {false, true}) {
        RenderSettings settings;
        settings.ssr = ssr;
        std::vector<unsigned char>& frame = frames[ssr ? 1 : 0];
        frame = freshFrame(build, settings, failures);
        if (frame.empty()) return;
    }
    const double mean = meanLevelsApart(frames[1], frames[0]);
    std::printf("      the traced frame differs by %.2f a channel on average\n", mean);
    expect("an orthographic view traces its reflections", mean > 0.05, failures);
}

// A reflection probe dragged to a new place keeps its last bake while it moves and bakes once it
// holds still, so a probe crossing the scene costs one bake, not one a frame. render() draws
// FRAMES frames: three calls hold it still past CAPTURE_SETTLE_FRAMES.
void testAMovedProbeBakesOnceItSettles(int& failures) {
    std::printf("A reflection probe moved:\n");
    const glm::vec3 from = {0.0f, 1.0f, 0.0f};
    const glm::vec3 to   = {-3.0f, 1.0f, 1.0f};
    const auto build = [](Scenery& s, glm::vec3 at) {
        buildProbe(s);
        place(
            s,
            s.cube,
            material(s, "golden:red", {0.8f, 0.1f, 0.08f}, 0.5f, 0.0f),
            {-6.0f, 1.5f, 0.0f},
            {1.0f, 3.0f, 8.0f}
        );
        s.scene.forEach<ReflectionProbe>([&](EntityId id, ReflectionProbe&) {
            s.scene.tryGet<Transform>(id)->position = at;
        });
    };

    WindowManager window;
    const auto there = [&](Scenery& s) { build(s, to); };
    const std::vector<unsigned char> want = freshFrame(there, RenderSettings{}, failures);
    if (want.empty()) return;

    restoreContextDefaults();
    GLBackend dragged;
    if (!dragged.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery moved;
    build(moved, from);
    render(moved, dragged);
    moved.scene.forEach<ReflectionProbe>([&](EntityId id, ReflectionProbe&) {
        moved.scene.tryGet<Transform>(id)->position = to;
    });
    const float moving = differingShare(render(moved, dragged), want.data());
    render(moved, dragged);
    const float settled = differingShare(render(moved, dragged), want.data());
    std::printf(
        "      %.2f%% of pixels differ while it moves, %.2f%% once it settles\n",
        moving * 100.0f,
        settled * 100.0f
    );
    expect("a probe being moved keeps its last bake", moving > PIXEL_TOLERANCE, failures);
    expect("  and bakes where it settles", settled <= PIXEL_TOLERANCE, failures);
}

// A wide open wall faced square on, in the AO view: nothing occludes it, so it is as open at the
// frame's edges as at its centre. Slices turned evenly on the screen would bunch about an
// off-centre pixel's view ray and darken the wall toward the edges.
void testAnOpenWallIsOpenToTheFramesEdge(int& failures) {
    std::printf("Ambient occlusion across an open wall:\n");
    const auto build = [](Scenery& s) {
        base(s);
        place(
            s,
            s.cube,
            material(s, "golden:grey", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f),
            {0.0f, 1.5f, -0.5f},
            {40.0f, 30.0f, 1.0f}
        );
        sunAndCamera(s, {0.0f, 1.5f, 3.0f}, {0.0f, 1.5f, 0.0f});
    };

    RenderSettings settings;
    settings.renderMode    = RenderMode::AmbientOcclusion;
    settings.msaaSamples   = 1;
    settings.gtaoIntensity = 3.0f;  // strong, so a bias shows
    const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
    if (frame.empty()) return;

    const auto meanOver = [&](int from, int to) {
        long sum = 0;
        int  n   = 0;
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = from; x < to; ++x) {
                sum += pixel(frame, x, y).x;
                ++n;
            }
        }
        return static_cast<float>(sum) / static_cast<float>(n);
    };
    const float centre = meanOver(WIDTH / 2 - 20, WIDTH / 2 + 20);
    const float edges  = 0.5f * (meanOver(0, 20) + meanOver(WIDTH - 20, WIDTH));
    std::printf("      %.1f at the centre, %.1f at the edges\n", centre, edges);
    expect("an open wall is as open at the frame's edges", edges >= 0.97f * centre, failures);
}

// The mean of every channel of a frame, 0..255.
float meanValue(const std::vector<unsigned char>& frame) {
    long sum = 0;
    for (const unsigned char value : frame) sum += value;
    return static_cast<float>(sum) / static_cast<float>(frame.size());
}

// A room with no opening, under a bright sky and sun, stores no light in its volume: its walls
// are lit only by what reaches them, and nothing does. Lit by the sky as the frame lights
// open ground, the walls would light every probe inside through solid plaster.
void testASealedRoomHoldsNoSkyLight(int& failures) {
    std::printf("An irradiance volume inside a room, its indirect light alone:\n");

    RenderSettings settings;
    settings.renderMode = RenderMode::GiOnly;
    float means[2] = {};
    for (const bool openings : {false, true}) {
        const auto room = [openings](Scenery& s) { buildRoomInView(s, openings); };
        const std::vector<unsigned char> frame = freshFrame(room, settings, failures);
        if (frame.empty()) return;
        means[openings ? 1 : 0] = meanValue(frame);
    }
    std::printf("      mean %.2f sealed, %.2f with a doorway and a window\n", means[0], means[1]);
    expect("a sealed room is dark inside", means[0] < 1.0f, failures);
    expect("  and one with openings keeps the light they let in", means[1] > 8.0f, failures);
}

// A point lamp under buildRoomShell's ceiling, casting no shadow.
void switchOnTheLamp(Scenery& s) {
    addLamp(s, {-2.0f, 3.2f, -2.0f}, 60.0f, 12.0f, false);
}

// A volume's light follows its lights and its sky without a new bake version: its surfels are lit
// again, and the frame then is the one a fresh bake under the new light draws. The room's own
// direct light changes too, but the bounce it sends round the room is what a stale grid would
// keep.
void testAnIrradianceVolumeFollowsItsLight(int& failures) {
    std::printf("An irradiance volume whose lighting changes:\n");

    sameAsAFreshBackend(
        "a room whose sun moves is lit again as a fresh bake lights it",
        [](Scenery& s) { buildRoomInView(s, true); },
        [](Scenery&) {},
        [](Scenery& s) {
            s.scene.environment().sky.sunElevation = 20.0f;
            s.scene.environment().sky.sunAzimuth   = 80.0f;
        },
        failures
    );

    // Sealed, so all the light inside is the lamp's, and the room's bounce all of what a grid
    // baked in the dark would not hold.
    sameAsAFreshBackend(
        "a lamp switched on in a sealed room lights its bounce",
        [](Scenery& s) { buildRoomInView(s, false); },
        [](Scenery&) {},
        switchOnTheLamp,
        failures
    );
}

// A volume's first bake comes a slice a frame, for a loading screen to show through, and ends as
// the bake made whole; while it is under way the backend says how far it has come.
void testAVolumeBakedOverFramesEndsAsOneBakedWhole(int& failures) {
    std::printf("An irradiance volume baked a slice a frame:\n");
    restoreContextDefaults();
    WindowManager window;
    GLBackend sliced;
    if (!sliced.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery room;
    buildRoomInView(room, true);

    // No time at all: a probe a frame, the least a slice takes.
    g_bakeBudget = 1e-9f;
    render(room, sliced, nullptr, RenderSettings{}, nullptr, 1);
    const RenderReadiness partway = sliced.readiness();
    expect(
        "part-way, the backend says how far it has come",
        !partway.ready() && partway.done > 0 && partway.done < partway.total,
        failures
    );
    render(room, sliced, nullptr, RenderSettings{}, nullptr, static_cast<int>(partway.total));
    g_bakeBudget = 0.0f;
    const std::vector<unsigned char> seen = render(room, sliced);
    expect("once through, it is ready", sliced.readiness().ready(), failures);

    const auto built = [](Scenery& s) { buildRoomInView(s, true); };
    const std::vector<unsigned char> want = freshFrame(built, RenderSettings{}, failures);
    if (want.empty()) return;
    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels differ from a bake made whole\n", share * 100.0f);
    expect("  and it lights the room as the bake made whole does", share <= PIXEL_TOLERANCE, failures);

    // Asked for again, the frames keep the grid they had until the new one is in, rather than
    // going without for the whole of the bake.
    g_bakeBudget = 1e-9f;
    room.scene.forEach<IrradianceVolume>([](EntityId, IrradianceVolume& volume) { ++volume.bakeVersion; });
    const std::vector<unsigned char> during = render(room, sliced, nullptr, RenderSettings{}, nullptr, 2);
    g_bakeBudget = 0.0f;
    expect("baked again, part-way", !sliced.readiness().ready(), failures);
    const float kept = differingShare(during, seen.data());
    std::printf("      %.2f%% of pixels differ meanwhile\n", kept * 100.0f);
    expect("  the room keeps the light it had", kept <= PIXEL_TOLERANCE, failures);

    // Moved, the box waits to settle before it bakes again; meanwhile the grid lies where its
    // light was gathered, not stretched over the box as it now stands.
    const std::vector<unsigned char> settled = render(room, sliced);
    room.scene.forEach<IrradianceVolume, Transform>([](EntityId, IrradianceVolume&, Transform& at) {
        at.position.x += 2.0f;
    });
    const std::vector<unsigned char> moving = render(room, sliced, nullptr, RenderSettings{}, nullptr, 2);
    const float stays = differingShare(moving, settled.data());
    std::printf("      %.2f%% of pixels differ while the box moves\n", stays * 100.0f);
    expect(
        "moved, the grid stays where it was baked until it bakes again",
        stays <= PIXEL_TOLERANCE,
        failures
    );

    // Another world in its place - the room sealed - is not lit by the last one's grid while
    // its own bakes: it shows none, as a backend that never had one does.
    const auto sealedPartway = [](GLBackend& backend, Scenery& s) {
        g_bakeBudget = 1e-9f;
        const std::vector<unsigned char> image = render(s, backend, nullptr, RenderSettings{}, nullptr, 2);
        g_bakeBudget = 0.0f;
        return image;
    };
    room.scene.clear();
    buildRoomInView(room, false);
    const std::vector<unsigned char> replaced = sealedPartway(sliced, room);
    GLBackend fresh;
    if (!fresh.init(window)) return;
    Scenery sealed;
    buildRoomInView(sealed, false);
    const float stale = differingShare(replaced, sealedPartway(fresh, sealed).data());
    std::printf("      %.2f%% of pixels differ from a backend with no grid yet\n", stale * 100.0f);
    expect("a world replaced shows none of the last one's light", stale <= PIXEL_TOLERANCE, failures);
}

// A world whose meshes are still decoding bakes none of its light until they land: a bake begun
// then would capture it part-made, and nothing bakes it again when they arrive.
void testAWorldsBakesWaitForItsMeshes(int& failures) {
    std::printf("A world whose meshes are still arriving:\n");
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery room;
    buildRoomInView(room, true);
    // All but the ground on a mesh still decoding, which draws nothing until it lands.
    MeshAsset decoding;
    decoding.loading = true;
    const MeshHandle pending = room.resources.add(std::move(decoding), "golden:decoding");
    std::vector<EntityId> walls;
    room.scene.forEach<Mesh, Transform>([&](EntityId e, Mesh& drawn, const Transform& at) {
        if (at.position.y <= 0.0f) return;
        walls.push_back(e);
        drawn.mesh = pending;
    });

    g_worldArriving = true;
    render(room, backend);
    g_worldArriving = false;
    const RenderReadiness waiting = backend.readiness();
    expect(
        "while they arrive no bake begins, and the world's is owed",
        !waiting.ready() && waiting.done == 0,
        failures
    );

    for (const EntityId e : walls) room.scene.get<Mesh>(e).mesh = room.cube;
    const std::vector<unsigned char> seen = render(room, backend);
    const std::vector<unsigned char> want =
        freshFrame([](Scenery& s) { buildRoomInView(s, true); }, RenderSettings{}, failures);
    if (want.empty()) return;
    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels differ from a world baked whole\n", share * 100.0f);
    expect("once they land, it is lit as the world it is", share <= PIXEL_TOLERANCE, failures);
}

// What the backend says it owes is what its frames go on to bake: the reflection probes wait for
// the volume that lights them, probes turned off owe nothing, and with no camera nothing is baked.
void testReadinessOwesWhatTheFramesBake(int& failures) {
    std::printf("A backend's readiness:\n");
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }

    // A probe a frame, the least a slice takes: the first frame's one step is the volume's.
    g_bakeBudget = 1e-9f;
    Scenery room;
    buildRoom(room);
    render(room, backend, nullptr, RenderSettings{}, nullptr, 1);
    g_bakeBudget = 0.0f;
    const RenderReadiness first = backend.readiness();
    expect("the probe waits for the volume part-way", first.done == 1 && !first.ready(), failures);

    GLBackend off;
    if (!off.init(window)) return;
    Scenery unlit;
    buildRoom(unlit);
    RenderSettings noProbes;
    noProbes.probes = false;
    // Past the lightmap's settling, which is owed whatever the probes.
    render(unlit, off, nullptr, noProbes, nullptr, SETTLED);
    expect("probes turned off are not owed", off.readiness().ready(), failures);

    GLBackend blind;
    if (!blind.init(window)) return;
    Scenery shell;
    buildRoomShell(shell, true);
    render(shell, blind);
    expect("with no camera nothing is owed", blind.readiness().ready(), failures);
}

// A backend whose programs compile on the driver's threads owes them until they have, and once
// they have draws the world as a backend that waited for each as it was built.
void testProgramsCompiledInParallelDrawAsWaitedFor(int& failures) {
    std::printf("A backend whose programs compile in parallel:\n");
    restoreContextDefaults();
    if (!Vkm::GL::ShaderBase::compileInParallel()) {
        std::printf("  (this driver compiles one at a time - nothing to check)\n");
        return;
    }
    using Wall = std::chrono::steady_clock;
    const Wall::time_point begun = Wall::now();
    WindowManager window;
    GLBackend parallel;
    const bool started = parallel.init(window);
    const double built = std::chrono::duration<double>(Wall::now() - begun).count();
    const uint32_t programs = parallel.readiness().programs;
    // A driver that never says a program is done fails here, not hangs the suite.
    constexpr double PATIENCE = 120.0;
    Vkm::GL::ShaderCount count = Vkm::GL::countShaders();
    while (count.compiling > 0 && std::chrono::duration<double>(Wall::now() - begun).count() < PATIENCE) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        count = Vkm::GL::countShaders();
    }
    expect("every program finishes compiling", count.compiling == 0, failures);
    const double compiled = std::chrono::duration<double>(Wall::now() - begun).count();
    std::printf("      %u programs, built in %.2f s and compiled in %.2f s\n", programs, built, compiled);
    expect("it starts, owing its programs", started && programs > 0, failures);

    Scenery room;
    buildRoom(room);
    const RenderSettings settings;
    const std::vector<unsigned char> drawn = render(room, parallel, nullptr, settings, nullptr, SETTLED);
    expect("  and once drawn owes none", parallel.readiness().programs == 0, failures);
    Vkm::GL::ShaderBase::compileInParallel(false);

    GLBackend serial;
    if (!serial.init(window)) return;
    Scenery same;
    buildRoom(same);
    const std::vector<unsigned char> waited = render(same, serial, nullptr, settings, nullptr, SETTLED);
    const float differ = differingShare(drawn, waited.data());
    std::printf("      %.2f%% of pixels differ from one built waiting\n", differ * 100.0f);
    expect("the world draws as one whose programs were waited for", differ <= PIXEL_TOLERANCE, failures);
}

// A skinned mesh in its rest pose - one bone where it was bound, every vertex wholly on it - draws
// as the mesh does unskinned: a vertex's weights are shares of its bones, read as 0..1.
void testARestPoseSkinsAsTheMeshDraws(int& failures) {
    std::printf("A skinned mesh at rest:\n");

    PoseBuffer poses;
    const auto cube = [&poses](bool rigged) {
        return [&poses, rigged](Scenery& s) {
            base(s);
            MeshAsset asset = generateCube();
            if (rigged) {
                asset.skin.assign(asset.vertices.size(), SkinVertex{{0, 0, 0, 0}, {255, 0, 0, 0}});
                asset.skinRadius = 1.0f;
            }
            const MeshHandle mesh = s.resources.add(std::move(asset), "golden:rest_cube");
            const EntityId   id   = s.scene.createEntity();
            Transform at;
            at.position = {0.0f, 0.5f, 0.0f};
            s.scene.add(id, std::move(at));
            Mesh drawn;
            drawn.mesh     = mesh;
            drawn.material = material(s, "golden:rest", {0.7f, 0.3f, 0.2f}, 0.5f, 0.0f);
            s.scene.add(id, std::move(drawn));
            sunAndCamera(s, {2.5f, 2.0f, 3.0f}, {0.0f, 0.5f, 0.0f});

            poses.clear();
            if (!rigged) return;
            const uint32_t  slice = poses.addSlice(1);
            const PoseWrite write = poses.writeTo(slice);
            write.global[0]  = glm::mat4(1.0f);
            write.palette[0] = glm::mat4(1.0f);
            finishSlice(write);
            poses.mapEntity(id, slice);
        };
    };

    const std::vector<unsigned char> plain = freshFrame(cube(false), RenderSettings{}, failures);
    g_poses = &poses;
    const std::vector<unsigned char> skinned = freshFrame(cube(true), RenderSettings{}, failures);
    g_poses = nullptr;
    if (plain.empty() || skinned.empty()) return;
    const float share = differingShare(skinned, plain.data());
    std::printf("      %.2f%% of pixels differ from the mesh unskinned\n", share * 100.0f);
    expect("it draws where the mesh does", share <= PIXEL_TOLERANCE, failures);
}

// A relight cut short: the lamp switched on starts one, and the volume nudged and put back before
// it settles ends it. No bake runs - the box is the baked one again - so the relight has to be
// asked for anew, or the room keeps the dark it was baked in.
void testACutShortRelightIsAskedForAgain(int& failures) {
    std::printf("An irradiance volume whose relight was cut short:\n");
    const auto nudge = [](Scenery& s, float by) {
        s.scene.forEach<IrradianceVolume, Transform>([by](EntityId, IrradianceVolume&, Transform& t) {
            t.position.x += by;
        });
    };

    restoreContextDefaults();
    WindowManager window;
    GLBackend lived;
    if (!lived.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery room;
    buildRoomInView(room, false);
    render(room, lived);
    switchOnTheLamp(room);
    render(room, lived, nullptr, RenderSettings{}, nullptr, 1);
    nudge(room, 0.5f);
    render(room, lived, nullptr, RenderSettings{}, nullptr, 1);
    nudge(room, -0.5f);
    render(room, lived);
    render(room, lived);
    const std::vector<unsigned char> seen = render(room, lived);

    const auto lit = [&](Scenery& s) {
        buildRoomInView(s, false);
        switchOnTheLamp(s);
    };
    const std::vector<unsigned char> want = freshFrame(lit, RenderSettings{}, failures);
    if (want.empty()) return;

    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels differ from a fresh bake\n", share * 100.0f);
    expect("the lamp's bounce comes, as a fresh bake has it", share <= PIXEL_TOLERANCE, failures);
}

// A relight lights grids of its own, a gather a frame, and the volume takes them only once the
// last is done, so a room never steps through its bounces. Seen as GI alone, the frames before
// then are the grid baked in the dark, and the ones after are not.
void testAVolumeShowsItsGridUntilItsRelightIsThrough(int& failures) {
    std::printf("An irradiance volume partway through its relight:\n");
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery room;
    buildRoomInView(room, false);
    RenderSettings gi;
    gi.renderMode = RenderMode::GiOnly;
    gi.ssr        = false;
    const std::vector<unsigned char> dark = render(room, backend, nullptr, gi);
    switchOnTheLamp(room);
    const std::vector<unsigned char> midway =
        render(room, backend, nullptr, gi, nullptr, GLIrradianceBaker::BOUNCES - 2);
    render(room, backend, nullptr, gi);
    const std::vector<unsigned char> relit = render(room, backend, nullptr, gi);

    const float kept    = differingShare(midway, dark.data());
    const float changed = differingShare(relit, dark.data());
    std::printf("      %.2f%% of pixels differ partway through\n", kept * 100.0f);
    std::printf("      %.2f%% once through\n", changed * 100.0f);
    expect("until its relight is through, the volume shows its old grid", kept <= PIXEL_TOLERANCE, failures);
    expect("  and then the lamp's", changed > 0.25f, failures);
}

// The overlay drawn by the real pass over a real frame, asserted per pixel, not against
// a golden: where its edges land is arithmetic that holds on any GPU.
// A mirror facing the eye, and a red ball behind the eye: no ray the screen holds reaches the ball,
// so only a planar reflector shows it in the mirror - and with it off, the mirror shows none. A
// particle behind the eye shows in the mirror with the ball.
void testAMirrorShowsWhatIsBehindTheEye(int& failures) {
    std::printf("A planar mirror facing the eye:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    // Bare, reflecting, and reflecting with a green glow between the eye and the ball.
    float red[3]   = {};
    float green[3] = {};
    for (const int shown : {0, 1, 2}) {
        const bool reflector = shown > 0;
        const auto build = [reflector, shown](Scenery& s) {
            base(s);
            place(
                s,
                s.cube,
                material(s, "golden:floor", {0.4f, 0.4f, 0.4f}, 0.8f, 0.0f),
                {0.0f, -0.5f, 0.0f},
                {20.0f, 1.0f, 20.0f}
            );
            place(
                s,
                s.cube,
                material(s, "golden:mirror", {0.95f, 0.95f, 0.95f}, 0.02f, 1.0f),
                {0.0f, 1.5f, -3.0f},
                {4.0f, 3.0f, 0.05f}
            );
            place(
                s,
                s.sphere,
                material(s, "golden:red", {0.9f, 0.05f, 0.05f}, 0.6f, 0.0f),
                {0.0f, 1.5f, 6.0f},
                glm::vec3(3.0f)
            );
            const EntityId glass = s.scene.createEntity();
            Transform at;
            at.position = {0.0f, 1.5f, -2.975f};
            at.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
            s.scene.add(glass, std::move(at));
            PlanarReflector mirror;
            mirror.enabled     = reflector;
            mirror.halfExtents = {2.0f, 1.5f};
            s.scene.add(glass, std::move(mirror));
            if (shown == 2) {
                ParticleEmitter glow;
                glow.blend = ParticleBlend::Additive;
                holdLook(glow, {0.1f, 1.0f, 0.1f, 1.0f}, 2.0f);
                Particle p;
                p.position = {0.0f, 1.5f, 4.2f};
                p.size     = 0.6f;
                addEmitter(s, glow).push_back(p);
            }
            sunAndCamera(s, {0.0f, 1.5f, 3.0f}, {0.0f, 1.5f, -3.0f});
        };
        const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
        if (frame.empty()) return;
        const glm::ivec3 c = pixel(frame, WIDTH / 2, HEIGHT / 2);
        red[shown]   = static_cast<float>(c.x);
        green[shown] = static_cast<float>(c.y);
    }
    std::printf(
        "      mirror centre: %.0f/%.0f red/green bare, %.0f/%.0f reflecting, %.0f/%.0f with a glow\n",
        red[0],
        green[0],
        red[1],
        green[1],
        red[2],
        green[2]
    );
    expect("a planar reflector shows the ball behind the eye", red[1] > 2.0f * green[1] + 20.0f, failures);
    expect("  which the trace and the sky alone do not", red[0] < 1.5f * green[0] + 10.0f, failures);
    expect("  and a particle behind the eye too", green[2] > green[1] + 40.0f, failures);
}

// A one-sided card behind the eye, mirrored by a negative scale and facing a planar mirror. The
// mirror turns the winding once and the card's scale again: drawn with either turn alone, the card
// shows the mirror its back and is culled from it.
void testAMirroredCardShowsInAMirror(int& failures) {
    std::printf("A mirrored card in a planar mirror:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    float red[2] = {};
    for (const float side : {1.0f, -1.0f}) {
        const auto build = [side](Scenery& s) {
            base(s);
            place(
                s,
                s.cube,
                material(s, "golden:mirror", {0.95f, 0.95f, 0.95f}, 0.02f, 1.0f),
                {0.0f, 1.5f, -3.0f},
                {4.0f, 3.0f, 0.05f}
            );
            const EntityId glass = s.scene.createEntity();
            Transform at;
            at.position = {0.0f, 1.5f, -2.975f};
            at.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
            s.scene.add(glass, std::move(at));
            PlanarReflector mirror;
            mirror.halfExtents = {2.0f, 1.5f};
            s.scene.add(glass, std::move(mirror));

            // Its front turned to the mirror, away from the eye.
            const EntityId card = s.scene.createEntity();
            Transform t;
            t.position = {0.0f, 1.5f, 6.0f};
            t.rotation = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
            t.scale    = {6.0f * side, 6.0f, 6.0f};
            s.scene.add(card, std::move(t));
            Mesh m;
            m.mesh     = s.resources.add(generatePlane(1.0f, 1.0f), "golden:card");
            m.material = material(s, "golden:red", {0.9f, 0.05f, 0.05f}, 0.6f, 0.0f);
            s.scene.add(card, std::move(m));
            sunAndCamera(s, {0.0f, 1.5f, 3.0f}, {0.0f, 1.5f, -3.0f});
        };
        const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
        if (frame.empty()) return;
        const glm::ivec3 c = pixel(frame, WIDTH / 2, HEIGHT / 2);
        red[side > 0.0f ? 0 : 1] = static_cast<float>(c.x) - static_cast<float>(c.y);
    }
    std::printf("      mirror centre, red over green: %.0f plain, %.0f mirrored\n", red[0], red[1]);
    expect("the card shows in the mirror", red[0] > 40.0f, failures);
    expect("  and mirrored by its scale, still does", red[1] > 40.0f, failures);
}

// A mirror shows the sun's shadow where the camera's cascades hold it: a point's cascade, and how far
// the shadow reaches, are by its depth along the camera the cascades were fitted to, not the mirror's.
// An eye looking steeply down at a wall mirror sees the floor in front of it at depths the mirror's
// camera puts past the shadow's reach; a canopy out of view shades that floor.
void testAMirrorShowsTheShadowTheCascadesHold(int& failures) {
    std::printf("A mirror over a shaded floor the cascades hold:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    float level[2] = {};
    for (const bool canopy : {false, true}) {
        const auto build = [canopy](Scenery& s) {
            base(s);
            s.scene.environment().sky.procedural   = false;
            s.scene.environment().sky.sunElevation = 70.0f;
            s.scene.environment().sky.sunAzimuth   = 20.0f;
            const MaterialHandle grey   = material(s, "golden:floor", {0.6f, 0.6f, 0.6f}, 0.8f, 0.0f);
            const MaterialHandle silver = material(s, "golden:mirror", {0.95f, 0.95f, 0.95f}, 0.02f, 1.0f);
            place(s, s.cube, grey, {0.0f, -0.5f, 0.0f}, {30.0f, 1.0f, 30.0f});
            place(s, s.cube, silver, {0.0f, 1.5f, -3.0f}, {6.0f, 3.0f, 0.05f});
            if (canopy) place(s, s.cube, grey, {0.0f, 14.0f, -2.0f}, {30.0f, 0.5f, 12.0f});
            const EntityId glass = s.scene.createEntity();
            Transform at;
            at.position = {0.0f, 1.5f, -2.975f};
            at.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
            s.scene.add(glass, std::move(at));
            PlanarReflector mirror;
            mirror.halfExtents = {3.0f, 1.5f};
            s.scene.add(glass, std::move(mirror));
            sunAndCamera(s, {0.0f, 6.0f, 3.0f}, {0.0f, 1.0f, -3.0f});
            s.scene.forEach<Light>([](EntityId, Light& light) { light.shadowDistance = 10.0f; });
        };
        const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
        if (frame.empty()) return;
        const glm::ivec3 c = pixel(frame, WIDTH / 2, HEIGHT / 2);
        level[canopy ? 1 : 0] = static_cast<float>(c.x + c.y + c.z);
    }
    std::printf("      mirror centre: %.0f open, %.0f under the canopy\n", level[0], level[1]);
    expect("the mirror shows the canopy's shadow on the floor", level[1] < 0.6f * level[0], failures);
}

// A mirror reads the lightmaps as the frame does: a fixed sun's shadow baked on a fixed floor far
// behind the eye, where the camera's cascades hold nothing and a live shadow reads lit, shows in a
// mirror the eye faces. A fixed canopy over that floor shades it.
void testAMirrorShowsWhatAFixedSunBaked(int& failures) {
    std::printf("A mirror over a floor a fixed sun baked:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    float level[2] = {};
    for (const bool canopy : {false, true}) {
        const auto build = [canopy](Scenery& s) {
            base(s);
            s.scene.environment().sky.procedural   = false;
            s.scene.environment().sky.sunElevation = 70.0f;
            s.scene.environment().sky.sunAzimuth   = 20.0f;
            MeshAsset plane = generatePlane(1.0f, 1.0f, 1, 1);
            for (const Vertex& v : plane.vertices) plane.lightmapUVs.push_back(v.uv);
            const MeshHandle     floor  = s.resources.add(std::move(plane), "golden:mirror_baked_floor");
            const MaterialHandle grey   = material(s, "golden:floor", {0.6f, 0.6f, 0.6f}, 0.8f, 0.0f);
            const MaterialHandle silver = material(s, "golden:mirror", {0.95f, 0.95f, 0.95f}, 0.02f, 1.0f);
            place(s, floor, grey, {0.0f, 0.0f, 0.0f}, {80.0f, 1.0f, 80.0f});
            place(s, s.cube, silver, {0.0f, 1.5f, -3.0f}, {6.0f, 3.0f, 0.05f});
            // Over where the mirror's centre sees the floor, eighteen metres behind the eye.
            if (canopy) place(s, s.cube, grey, {0.0f, 6.0f, 21.0f}, {30.0f, 0.5f, 30.0f});
            s.scene.forEach<Mesh>([](EntityId, Mesh& mesh) { mesh.movable = false; });
            const EntityId glass = s.scene.createEntity();
            Transform at;
            at.position = {0.0f, 1.5f, -2.975f};
            at.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
            s.scene.add(glass, std::move(at));
            PlanarReflector mirror;
            mirror.halfExtents = {3.0f, 1.5f};
            s.scene.add(glass, std::move(mirror));
            sunAndCamera(s, {0.0f, 1.5f, 3.0f}, {0.0f, 1.2f, -3.0f});
            s.scene.forEach<Light>([](EntityId, Light& light) {
                light.movable        = false;
                light.shadowDistance = 10.0f;
            });
        };
        const std::vector<unsigned char> frame = freshFrame(build, settings, failures, SETTLED);
        if (frame.empty()) return;
        const glm::ivec3 c = pixel(frame, WIDTH / 2, HEIGHT / 2);
        level[canopy ? 1 : 0] = static_cast<float>(c.x + c.y + c.z);
    }
    std::printf("      mirror centre: %.0f open, %.0f under the canopy\n", level[0], level[1]);
    expect("the mirror shows the baked shadow behind the eye", level[1] < 0.6f * level[0], failures);
}

// An orthographic eye's rays are parallel, so the sky in a mirror it faces would be one direction of
// it across the whole glass; it takes the sky a default eye sees, as the frame's own sky does, and
// the horizon runs across the mirror.
void testAnOrthographicEyesMirrorShowsTheHorizon(int& failures) {
    std::printf("A mirror facing an orthographic eye:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto build = [](Scenery& s) {
        base(s);
        const MaterialHandle silver = material(s, "golden:mirror", {0.95f, 0.95f, 0.95f}, 0.02f, 1.0f);
        place(s, s.cube, silver, {0.0f, 0.0f, -3.0f}, {12.0f, 12.0f, 0.05f});
        const EntityId glass = s.scene.createEntity();
        Transform at;
        at.position = {0.0f, 0.0f, -2.975f};
        at.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
        s.scene.add(glass, std::move(at));
        PlanarReflector mirror;
        mirror.halfExtents = {6.0f, 6.0f};
        s.scene.add(glass, std::move(mirror));
        sunAndCamera(s, {0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, -3.0f});
        s.scene.forEach<Camera>([](EntityId, Camera& lens) {
            lens.projection  = ProjectionType::Orthographic;
            lens.orthoHeight = 4.0f;
        });
    };
    const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
    if (frame.empty()) return;
    const glm::ivec3 top    = pixel(frame, WIDTH / 2, HEIGHT / 8);
    const glm::ivec3 bottom = pixel(frame, WIDTH / 2, 7 * HEIGHT / 8);
    const int apart = std::abs(top.x - bottom.x) + std::abs(top.y - bottom.y) + std::abs(top.z - bottom.z);
    std::printf("      top %d %d %d, bottom %d %d %d\n", top.x, top.y, top.z, bottom.x, bottom.y, bottom.z);
    expect("the mirror's sky changes from top to bottom", apart > 30, failures);
}

// A mirror shows the world behind the eye as the frame would: where the scene hides its skybox, the
// mirror shows none either, the frame's black.
void testAMirrorHidesTheSkyTheSceneHides(int& failures) {
    std::printf("A mirror facing an empty sky the scene hides:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto build = [](Scenery& s) {
        base(s);
        const MaterialHandle silver = material(s, "golden:mirror", {0.95f, 0.95f, 0.95f}, 0.02f, 1.0f);
        place(s, s.cube, silver, {0.0f, 0.0f, -3.0f}, {12.0f, 12.0f, 0.05f});
        const EntityId glass = s.scene.createEntity();
        Transform at;
        at.position = {0.0f, 0.0f, -2.975f};
        at.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
        s.scene.add(glass, std::move(at));
        PlanarReflector mirror;
        mirror.halfExtents = {6.0f, 6.0f};
        s.scene.add(glass, std::move(mirror));
        sunAndCamera(s, {0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, -3.0f});
        s.scene.environment().sky.showSkybox = false;
    };
    const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
    if (frame.empty()) return;
    const glm::ivec3 top = pixel(frame, WIDTH / 2, HEIGHT / 8);
    std::printf("      the mirror's top shows %d %d %d\n", top.x, top.y, top.z);
    expect("it shows no sky", top.x + top.y + top.z < 30, failures);
}

// Each drawn mirror's picture is sized by its renderScale: one that changes rank as the eye moves
// keeps the storage it drew into, rather than swapping, and reallocating, with the other.
void testAMirrorKeepsItsPictureAsTheEyeMoves(int& failures) {
    std::printf("Two mirrors trading places on the screen:\n");

    restoreContextDefaults();
    WindowManager window;
    GLBackend lived;
    if (!lived.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery setting;
    base(setting);
    for (const float x : {-2.5f, 2.5f}) {
        const EntityId glass = setting.scene.createEntity();
        Transform at;
        at.position = {x, 1.0f, -3.0f};
        at.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
        setting.scene.add(glass, std::move(at));
        PlanarReflector mirror;
        mirror.halfExtents = {2.0f, 1.0f};
        mirror.renderScale = x < 0.0f ? 0.5f : 1.0f;
        setting.scene.add(glass, std::move(mirror));
    }
    sunAndCamera(setting, {-2.5f, 1.0f, 1.0f}, {-2.5f, 1.0f, -3.0f});
    render(setting, lived);
    const GLPlanarReflectionPass& pass = lived.planarReflections();
    const int left  = pass.slotOf(0);
    const int right = pass.slotOf(1);
    const bool apart = left >= 0 && right >= 0 && left != right;
    expect("both mirrors draw, each into its own storage", apart, failures);

    setting.scene.forEach<Camera, Transform>([](EntityId, Camera&, Transform& pose) {
        pose.position = {2.5f, 1.0f, 1.0f};
    });
    render(setting, lived);
    expect(
        "  and once the other covers more, each keeps its own",
        pass.slotOf(0) == left && pass.slotOf(1) == right,
        failures
    );
}

// A particle's card faces its eye, built on the eye's axes; in a floor mirror its picture is upside
// down, as the scene's is: of a card in two colors over a floor mirror, the mirror shows the other
// color on top.
void testAMirrorTurnsAParticleOver(int& failures) {
    std::printf("A particle over a floor mirror:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto build = [](Scenery& s) {
        base(s);
        const MaterialHandle silver = material(s, "golden:mirror", {0.95f, 0.95f, 0.95f}, 0.02f, 1.0f);
        place(s, s.cube, silver, {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f});
        const EntityId glass = s.scene.createEntity();
        Transform at;
        at.position = {0.0f, 0.001f, 0.0f};
        s.scene.add(glass, std::move(at));
        PlanarReflector mirror;
        mirror.halfExtents = {10.0f, 10.0f};
        s.scene.add(glass, std::move(mirror));

        TextureAsset halves;
        halves.params.width  = 2;
        halves.params.height = 2;
        halves.pixelData = {255, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255};
        ParticleEmitter card;
        card.blend   = ParticleBlend::Additive;
        card.texture = s.resources.add(std::move(halves), "golden:halves");
        holdLook(card, glm::vec4(1.0f), 1.0f);
        Particle p;
        p.position = {0.0f, 0.9f, 0.0f};
        p.size     = 0.35f;
        addEmitter(s, card).push_back(p);
        sunAndCamera(s, {0.0f, 0.8f, 4.0f}, {0.0f, 0.0f, 0.0f});
    };
    const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
    if (frame.empty()) return;

    // Down the middle column: where red and where green, above and below the card's own middle.
    std::vector<int> red;
    std::vector<int> green;
    for (int y = 0; y < HEIGHT; ++y) {
        const glm::ivec3 c = pixel(frame, WIDTH / 2, y);
        if (c.x > c.y + 30) red.push_back(y);
        if (c.y > c.x + 30) green.push_back(y);
    }
    if (red.empty() || green.empty()) {
        expect("the card and its reflection are seen", false, failures);
        return;
    }
    const int top    = std::min(red.front(), green.front());
    const int bottom = std::max(red.back(), green.back());
    const int split  = (top + bottom) / 2;
    const auto mean = [](const std::vector<int>& rows, int from, int to) {
        double sum = 0.0;
        int    n   = 0;
        for (int y : rows) {
            if (y < from || y >= to) continue;
            sum += y;
            ++n;
        }
        return n > 0 ? sum / n : -1.0;
    };
    const double redAbove   = mean(red, 0, split);
    const double greenAbove = mean(green, 0, split);
    const double redBelow   = mean(red, split, HEIGHT);
    const double greenBelow = mean(green, split, HEIGHT);
    std::printf(
        "      seen: red %.0f green %.0f; mirrored: red %.0f green %.0f\n",
        redAbove,
        greenAbove,
        redBelow,
        greenBelow
    );
    const bool seen  = redAbove >= 0.0 && greenAbove >= 0.0;
    const bool shown = redBelow >= 0.0 && greenBelow >= 0.0;
    expect("the card and its reflection are seen", seen && shown, failures);
    expect(
        "  the reflection upside down",
        seen && shown && (redAbove < greenAbove) != (redBelow < greenBelow),
        failures
    );
}

// A spot wider than one perspective map can hold casts through a cube, as a point light does: the
// rim of its cone, past what a map reaches, is shadowed too. A spot straight down with an 88-degree
// cone, and a block between it and floor 86 degrees off its axis.
void testAWideSpotShadowsItsRim(int& failures) {
    std::printf("A spot wider than one shadow map:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    float level[2] = {};
    for (const bool blocked : {false, true}) {
        const auto build = [blocked](Scenery& s) {
            base(s);
            s.scene.environment().sky.procedural = false;
            s.scene.environment().sky.intensity  = 0.02f;
            const MaterialHandle grey = material(s, "golden:floor", {0.7f, 0.7f, 0.7f}, 0.9f, 0.0f);
            place(s, s.cube, grey, {0.0f, -0.5f, 0.0f}, {160.0f, 1.0f, 160.0f});
            if (blocked) place(s, s.cube, grey, {16.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 3.0f});
            const EntityId lamp = s.scene.createEntity();
            Transform at;
            at.position = {0.0f, 2.0f, 0.0f};
            at.rotation = glm::quatLookAt(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
            s.scene.add(lamp, std::move(at));
            Light spot;
            spot.type           = LightType::Spot;
            spot.intensity      = 20000.0f;
            spot.radius         = 80.0f;
            spot.innerConeAngle = glm::radians(80.0f);
            spot.outerConeAngle = glm::radians(88.0f);
            spot.castShadows    = true;
            s.scene.add(lamp, std::move(spot));
            camera(s, {26.0f, 4.0f, 8.0f}, {32.0f, 0.0f, 0.0f});
        };
        const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
        if (frame.empty()) return;
        const glm::ivec3 c = pixel(frame, WIDTH / 2, HEIGHT / 2);
        level[blocked ? 1 : 0] = static_cast<float>(c.x + c.y + c.z);
    }
    std::printf("      floor past the map's reach: %.0f open, %.0f behind the block\n", level[0], level[1]);
    expect("the floor behind the block is in its shadow", level[1] < 0.5f * level[0], failures);
}

// A material preview borrows the forward pass's program, which the pass leaves set for its own
// frame: a preview drawn after a frame in a debug view is the one drawn after the plain frame.
void testAPreviewIsNotTheFramesDebugView(int& failures) {
    std::printf("A material preview after a frame in a debug view:\n");
    const auto preview = [](GLBackend& backend) {
        ResourceManager resources;
        MaterialAsset red;
        red.albedo = glm::vec4(0.8f, 0.2f, 0.15f, 1.0f);
        PreviewRequest request;
        request.key      = 7;
        request.width    = 64;
        request.height   = 64;
        const MeshHandle ball = resources.add(generateSphere(), "golden:preview_ball");
        request.draws    = {{ball, resources.add(std::move(red), "golden:preview_red")}};
        const GpuTextureId id = backend.editorHooks()->renderPreview(request, resources);
        std::vector<unsigned char> pixels(64 * 64 * 4, 0);
        if (id == 0) return pixels;
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    };

    // The preview is lit as the last frame's scene is, so both draw the same scene first.
    const auto afterAFrameIn = [&](RenderMode mode) {
        restoreContextDefaults();
        WindowManager window;
        GLBackend backend;
        if (!backend.init(window)) {
            expect("the backend starts", false, failures);
            return std::vector<unsigned char>{};
        }
        RenderSettings settings;
        settings.msaaSamples = 1;
        settings.renderMode  = mode;
        Scenery s;
        buildMirror(s);
        render(s, backend, nullptr, settings);
        return preview(backend);
    };
    const std::vector<unsigned char> want = afterAFrameIn(RenderMode::Default);
    const std::vector<unsigned char> seen = afterAFrameIn(RenderMode::LightingOnly);
    if (want.empty() || seen.empty()) return;

    size_t differing = 0;
    for (size_t i = 0; i < want.size(); i += 4) {
        for (size_t c = 0; c < 3; ++c) {
            if (std::abs(int(want[i + c]) - int(seen[i + c])) > 2) {
                ++differing;
                break;
            }
        }
    }
    const float share = static_cast<float>(differing) / static_cast<float>(want.size() / 4);
    std::printf("      %.2f%% of the preview's pixels differ\n", share * 100.0f);
    expect("the preview is drawn as its own studio, not the frame's", share <= PIXEL_TOLERANCE, failures);
}

// A viewmodel's depth is squashed against the near plane, so read back as a place it is a point at
// the eye: a decal whose box takes in the eye would paint it. No decal lands on it.
void testNoDecalLandsOnTheViewmodel(int& failures) {
    std::printf("A decal around the eye, and the weapon held there:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto frameWith = [&](bool decal) {
        return freshFrame(
            [decal](Scenery& s) {
                buildHeld(s, SeenBy::Viewmodel, -0.55f);
                if (!decal) return;
                MaterialAsset paint;
                paint.type   = MaterialType::Transparent;
                paint.albedo = glm::vec4(0.9f, 0.15f, 0.1f, 1.0f);
                const EntityId id = s.scene.createEntity();
                Transform pose;
                pose.position = {0.0f, 1.6f, 0.0f};
                pose.scale    = glm::vec3(0.6f);
                s.scene.add(id, std::move(pose));
                Decal projector;
                projector.material = s.resources.add(std::move(paint), "golden:eye_paint");
                s.scene.add(id, std::move(projector));
            },
            settings,
            failures
        );
    };
    const std::vector<unsigned char> bare    = frameWith(false);
    const std::vector<unsigned char> painted = frameWith(true);
    if (bare.empty() || painted.empty()) return;
    const float share = differingShare(painted, bare.data());
    std::printf("      %.2f%% of pixels differ\n", share * 100.0f);
    expect("the weapon is drawn as it is without the decal", share <= PIXEL_TOLERANCE, failures);
}

// GTAO reads a viewmodel's squashed depth as sky: as near as it is drawn, it would shade the wall
// it is held against with a halo. Against the same wall with the block hidden in it, the wall
// round the block's outline keeps its shade.
void testAViewmodelShadesNothingBehindIt(int& failures) {
    std::printf("A weapon held into a wall, and the wall round it:\n");

    // No bloom: the marked block's glow would spread over the wall measured.
    RenderSettings noBloom;
    noBloom.bloom = false;
    const auto frameWith = [&](SeenBy seenBy, glm::vec3 glow) {
        return freshFrame([&](Scenery& s) { buildHeld(s, seenBy, -0.75f, glow); }, noBloom, failures);
    };
    const std::vector<unsigned char> held = frameWith(SeenBy::Viewmodel, glm::vec3(0.0f));
    if (held.empty()) return;
    const std::vector<unsigned char> hidden = frameWith(SeenBy::EyeOnly, glm::vec3(0.0f));
    if (hidden.empty()) return;
    // The block glowing green: where it is drawn, however like the wall its own shade.
    const std::vector<unsigned char> marked = frameWith(SeenBy::Viewmodel, glm::vec3(0.0f, 0.5f, 0.0f));
    if (marked.empty()) return;

    const auto shade = [&](const std::vector<unsigned char>& f, int x, int y) {
        const glm::ivec3 p = pixel(f, x, y);
        return p.x + p.y + p.z;
    };
    const auto outline = [&](int x, int y) { return pixel(marked, x, y).y - pixel(held, x, y).y > 8; };
    // Whether the outline comes within @p reach of a pixel.
    const auto within = [&](int x, int y, int reach) {
        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dx = -reach; dx <= reach; ++dx) {
                if (outline(x + dx, y + dy)) return true;
            }
        }
        return false;
    };
    // Wall pixels two to five from the block's outline - past the edge its own pixels blend - and
    // how much darker the block left them.
    constexpr int RING = 5;
    int ring    = 0;
    int darkest = 0;
    for (int y = RING; y < HEIGHT - RING; ++y) {
        for (int x = RING; x < WIDTH - RING; ++x) {
            if (within(x, y, 1) || !within(x, y, RING)) continue;
            ++ring;
            darkest = std::max(darkest, shade(hidden, x, y) - shade(held, x, y));
        }
    }
    std::printf("      %d wall pixels round it, the most it darkened one: %d of 765\n", ring, darkest);
    expect("the block shows", ring > 50, failures);
    expect("  and darkens no wall round it", darkest <= 3, failures);
}

// The penumbra scene with soft shadows off, against the same scene with a sun of no size: every
// shadow takes the hard path whatever the light's source. With them on, the penumbra shows.
void testSoftShadowsOffDrawTheHardPath(int& failures) {
    std::printf("Soft shadows turned off:\n");
    RenderSettings hard;
    hard.msaaSamples = 1;
    hard.softShadows = false;
    RenderSettings soft;
    soft.msaaSamples = 1;

    std::vector<unsigned char> frames[3];
    for (int i = 0; i < 3; ++i) {
        const auto build = [i](Scenery& s) {
            buildPenumbra(s);
            if (i == 2) s.scene.forEach<Light>([](EntityId, Light& light) { light.sourceRadius = 0.0f; });
        };
        frames[i] = freshFrame(build, i == 0 ? hard : soft, failures);
        if (frames[i].empty()) return;
    }
    const float off = differingShare(frames[0], frames[2].data());
    const float on  = differingShare(frames[1], frames[2].data());
    std::printf("      off %.2f%%, on %.2f%% of pixels unlike a sun of no size\n", off * 100.0f, on * 100.0f);
    expect("soft shadows off draw the frame a sun of no size draws", off <= PIXEL_TOLERANCE, failures);
    expect("  where soft ones draw another", on > PIXEL_TOLERANCE, failures);
}

// A tower on a floor that reads a lightmap (its plane's own coordinates are a fair unwrap), under
// a sun that is fixed (@p fixedSun) or live; and a red ball, which moves, standing in the light.
void buildBakedTower(Scenery& s, bool fixedSun, bool tower, bool ball) {
    base(s);
    s.scene.environment().sky.procedural   = false;
    s.scene.environment().sky.sunElevation = 18.0f;
    s.scene.environment().sky.sunAzimuth   = 100.0f;
    MeshAsset plane = generatePlane(1.0f, 1.0f, 1, 1);
    for (const Vertex& v : plane.vertices) plane.lightmapUVs.push_back(v.uv);
    const MeshHandle floor = s.resources.add(std::move(plane), "golden:baked_floor");
    const MaterialHandle slate = material(s, "golden:baked_slate", {0.45f, 0.45f, 0.45f}, 0.8f, 0.0f);
    place(s, floor, slate, {0.0f, 0.0f, 0.0f}, {80.0f, 1.0f, 80.0f});
    constexpr float TOWER_HEIGHT = 8.0f;
    if (tower) {
        place(
            s,
            s.cube,
            material(s, "golden:baked_tower", {0.6f, 0.6f, 0.62f}, 0.7f, 0.0f),
            {0.0f, TOWER_HEIGHT * 0.5f, 0.0f},
            {3.0f, TOWER_HEIGHT, 3.0f}
        );
    }
    s.scene.forEach<Mesh>([](EntityId, Mesh& mesh) { mesh.movable = false; });

    const glm::vec3 sun    = s.scene.environment().sunDirection();
    const glm::vec3 tip    = glm::vec3(0.0f, TOWER_HEIGHT, 0.0f) - sun * (TOWER_HEIGHT / sun.y);
    const glm::vec3 across = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), sun));
    if (ball) {
        const MaterialHandle red = material(s, "golden:baked_ball", {0.8f, 0.1f, 0.1f}, 0.5f, 0.0f);
        place(s, s.sphere, red, tip + across * 4.0f + glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(1.0f));
    }
    sunAndCamera(s, tip + across * 8.0f + glm::vec3(0.0f, 3.0f, 0.0f), tip + across * 2.0f);
    // A sun nearly a point: where the shadow falls is what the bake must agree on; a soft edge
    // is each filter's own.
    s.scene.forEach<Light>([fixedSun](EntityId, Light& light) {
        light.sourceRadius   = 0.002f;
        light.shadowDistance = 1000.0f;
        light.movable        = !fixedSun;
    });
}

// A fixed sun's shadow on a fixed floor is the floor's lightmap's, baked once the scene has held
// still: the frame is all but the one the live shadow draws, the tower's shadow is in it, and a
// ball that moves still casts on the baked floor.
void testAFixedSunsShadowIsBaked(int& failures) {
    std::printf("A fixed sun over a fixed floor:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto frame = [&](bool fixedSun, bool tower, bool ball) {
        return freshFrame(
            [=](Scenery& s) { buildBakedTower(s, fixedSun, tower, ball); },
            settings,
            failures,
            SETTLED
        );
    };
    const std::vector<unsigned char> baked    = frame(true, true, false);
    const std::vector<unsigned char> live     = frame(false, true, false);
    const std::vector<unsigned char> bare     = frame(true, false, false);
    const std::vector<unsigned char> withBall = frame(true, true, true);
    if (baked.empty() || live.empty() || bare.empty() || withBall.empty()) return;

    const float fromLive = differingShare(baked, live.data());
    const float fromBare = differingShare(baked, bare.data());
    const float fromBall = differingShare(withBall, baked.data());
    std::printf("      %.2f%% of pixels unlike the live shadow's frame\n", fromLive * 100.0f);
    std::printf("      %.2f%% unlike no tower's\n", fromBare * 100.0f);
    std::printf("      %.2f%% unlike the ball's\n", fromBall * 100.0f);
    expect("the baked shadow draws nearly the live one's frame", fromLive <= BAKED_TOLERANCE, failures);
    expect("  with the tower's shadow in it", fromBare > 0.05f, failures);
    expect("  and a ball that moves still casts on the baked floor", fromBall > 0.01f, failures);
}

// A fixed mesh that stops casting, is given another mesh or has its own rebuilt changes what the
// bake holds: a backend that baked the tower before draws, once settled, what a fresh one draws of
// the changed scene.
void testABakeFollowsWhatItsCastersBecome(int& failures) {
    std::printf("A fixed caster changed after its bake:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto tower = [](Scenery& s, const std::function<void(Mesh&)>& change) {
        s.scene.forEach<Mesh>([&](EntityId, Mesh& mesh) {
            if (mesh.mesh == s.cube) change(mesh);
        });
    };

    const auto changedAfterBaking = [&](const char* what, const std::function<void(Scenery&)>& change) {
        restoreContextDefaults();
        WindowManager window;
        GLBackend lived;
        if (!lived.init(window)) {
            expect("the backend starts", false, failures);
            return;
        }
        Scenery s;
        buildBakedTower(s, true, true, false);
        render(s, lived, nullptr, settings, nullptr, SETTLED);
        change(s);
        const std::vector<unsigned char> seen = render(s, lived, nullptr, settings, nullptr, SETTLED);
        const std::vector<unsigned char> want = freshFrame(
            [&](Scenery& fresh) {
                buildBakedTower(fresh, true, true, false);
                change(fresh);
            },
            settings,
            failures,
            SETTLED
        );
        if (seen.empty() || want.empty()) return;
        const float share = differingShare(seen, want.data());
        std::printf("      %s: %.2f%% of pixels differ\n", what, share * 100.0f);
        expect(what, share <= PIXEL_TOLERANCE, failures);
    };

    changedAfterBaking("a tower that stops casting takes its baked shadow with it", [&](Scenery& s) {
        tower(s, [](Mesh& mesh) { mesh.castShadows = false; });
    });
    changedAfterBaking("a tower given another mesh casts that mesh's shadow", [&](Scenery& s) {
        tower(s, [&](Mesh& mesh) { mesh.mesh = s.sphere; });
    });
    changedAfterBaking("a tower whose mesh is rebuilt in place casts its new shape", [&](Scenery& s) {
        MeshAsset rebuilt = generateSphere();
        s.resources.swapValue(s.cube, rebuilt);
    });
}

// The baked tower under cascades that reach a metre: the tower's shadow in the frame is the bake's
// alone.
void buildBakeOnlyTower(Scenery& s) {
    buildBakedTower(s, true, true, false);
    s.scene.forEach<Light>([](EntityId, Light& light) { light.shadowDistance = 1.0f; });
}

// Apply @p change to the baked tower's floor.
void changeBakedFloor(Scenery& s, const std::function<void(EntityId, Mesh&)>& change) {
    const MeshHandle floor = s.resources.findByName<MeshAsset>("golden:baked_floor");
    s.scene.forEach<Mesh>([&](EntityId id, Mesh& mesh) {
        if (mesh.mesh == floor) change(id, mesh);
    });
}

// Move every camera @p back metres behind where it looks from; negative brings it forward again.
void moveEyeBack(Scenery& s, float back) {
    s.scene.forEach<Camera>([&](EntityId id, Camera&) {
        Transform& eye = s.scene.get<Transform>(id);
        eye.position -= eye.rotation * glm::vec3(0.0f, 0.0f, -1.0f) * back;
    });
}

// A bake is made from the meshes a scene gave its fixed objects, not the levels of detail the eye
// picks: a tower whose far level is a block four times as wide, baked with the eye far off and then
// brought near, draws what a backend that baked it near draws - the tower's own shadow. The far
// level casts where the tower does not: one inside the tower would bake nothing a frame could tell
// from the tower's own shadow. The tower's own mesh is its alone and carries no lightmap
// coordinates: nothing the eye sees far off draws it. Seen near for fewer frames than a bake
// settles in, so the bake made far off is the one shown.
void testABakeIsMadeFromTheGivenMeshWhereverTheEyeIs(int& failures) {
    std::printf("A fixed caster with a level of detail, baked far off and seen near:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    // The tower draws the wide block past NEAR_REACH; FAR_BACK puts the eye well past it to bake.
    constexpr float NEAR_REACH = 100.0f;
    constexpr float FAR_BACK   = 400.0f;
    const auto build = [](Scenery& s) {
        buildBakeOnlyTower(s);
        MeshAsset block = generateCube();
        for (Vertex& v : block.vertices) v.position *= glm::vec3(4.0f, 1.0f, 4.0f);
        block.computeAndSetBounds();
        const MeshHandle wide = s.resources.add(std::move(block), "golden:baked_tower_far");
        const MeshHandle own  = s.resources.add(generateCube(), "golden:baked_tower_own");
        s.scene.forEach<Mesh>([&](EntityId id, Mesh& mesh) {
            if (mesh.mesh != s.cube) return;
            mesh.mesh = own;
            LOD lod;
            lod.levels = {{own, NEAR_REACH}, {wide, std::numeric_limits<float>::max()}};
            s.scene.add(id, std::move(lod));
        });
    };
    restoreContextDefaults();
    WindowManager window;
    GLBackend lived;
    if (!lived.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    build(s);
    moveEyeBack(s, FAR_BACK);
    render(s, lived, nullptr, settings, nullptr, SETTLED);
    moveEyeBack(s, -FAR_BACK);
    static_assert(FRAMES < static_cast<int>(CAPTURE_SETTLE_FRAMES), "no bake settles while seen near");
    const std::vector<unsigned char> seen = render(s, lived, nullptr, settings, nullptr, FRAMES);
    const std::vector<unsigned char> want = freshFrame(build, settings, failures, SETTLED);
    if (seen.empty() || want.empty()) return;
    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels unlike the bake made near\n", share * 100.0f);
    expect("the bake made far off is the tower's, not its far level's", share <= PIXEL_TOLERANCE, failures);
}

// A fixed receiver reads its lightmap only while it stands as it was baked: one made movable, or
// moved, draws from its next frame what it would with no bake.
void testAReceiverReadsItsBakeOnlyAsItWasBaked(int& failures) {
    std::printf("A fixed receiver changed after its bake:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto changedAfterBaking = [&](const char* what, const std::function<void(Scenery&)>& change) {
        restoreContextDefaults();
        WindowManager window;
        GLBackend lived;
        if (!lived.init(window)) {
            expect("the backend starts", false, failures);
            return;
        }
        Scenery s;
        buildBakeOnlyTower(s);
        const std::vector<unsigned char> baked = render(s, lived, nullptr, settings, nullptr, SETTLED);
        change(s);
        static_assert(FRAMES < static_cast<int>(CAPTURE_SETTLE_FRAMES), "no bake settles after the change");
        const std::vector<unsigned char> seen = render(s, lived, nullptr, settings, nullptr, FRAMES);
        const std::vector<unsigned char> want = freshFrame(
            [&](Scenery& fresh) {
                buildBakeOnlyTower(fresh);
                change(fresh);
            },
            settings,
            failures,
            FRAMES
        );
        if (baked.empty() || seen.empty() || want.empty()) return;
        const float before = differingShare(baked, want.data());
        const float share  = differingShare(seen, want.data());
        std::printf(
            "      %s: %.2f%% of pixels unlike no bake's, %.2f%% before\n",
            what,
            share * 100.0f,
            before * 100.0f
        );
        expect(what, share <= PIXEL_TOLERANCE, failures);
        expect("  where its bake had drawn the tower's shadow", before > 0.01f, failures);
    };

    changedAfterBaking("a floor made movable reads no bake", [](Scenery& s) {
        changeBakedFloor(s, [](EntityId, Mesh& mesh) { mesh.movable = true; });
    });
    changedAfterBaking("a fixed floor moved reads no bake", [](Scenery& s) {
        changeBakedFloor(s, [&](EntityId id, Mesh&) { s.scene.get<Transform>(id).position.x += 3.0f; });
    });
}

// A fixed floor reads its lightmap through a level of detail only while the level's lightmap
// coordinates are laid out as its own: one whose coordinates are the floor's, as a decimation keeps
// them, draws the floor's baked shadow; one unwrapped apart, into the middle of the square, draws
// what a floor with no bake draws.
void testALevelReadsTheBakeOnlyThroughItsGivenMeshsCharts(int& failures) {
    std::printf("A fixed floor drawn as a level of detail, against its bake:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto withLevel = [](bool ownUnwrap) {
        return [ownUnwrap](Scenery& s) {
            buildBakeOnlyTower(s);
            MeshAsset plane = generatePlane(1.0f, 1.0f, 1, 1);
            for (const Vertex& v : plane.vertices) {
                plane.lightmapUVs.push_back(ownUnwrap ? v.uv * 0.5f + glm::vec2(0.25f) : v.uv);
            }
            const MeshHandle level = s.resources.add(std::move(plane), "golden:baked_floor_level");
            const MeshHandle floor = s.resources.findByName<MeshAsset>("golden:baked_floor");
            // Past a metre from its centre, which the eye is, the floor draws the level.
            changeBakedFloor(s, [&](EntityId id, Mesh&) {
                LOD lod;
                lod.levels = {{floor, 1.0f}, {level, std::numeric_limits<float>::max()}};
                s.scene.add(id, std::move(lod));
            });
        };
    };
    const auto unbaked = [](Scenery& s) {
        buildBakeOnlyTower(s);
        changeBakedFloor(s, [](EntityId, Mesh& mesh) { mesh.movable = true; });
    };
    const std::vector<unsigned char> plain  = freshFrame(buildBakeOnlyTower, settings, failures, SETTLED);
    const std::vector<unsigned char> kept   = freshFrame(withLevel(false), settings, failures, SETTLED);
    const std::vector<unsigned char> apart  = freshFrame(withLevel(true), settings, failures, SETTLED);
    const std::vector<unsigned char> noBake = freshFrame(unbaked, settings, failures, SETTLED);
    if (plain.empty() || kept.empty() || apart.empty() || noBake.empty()) return;
    const float fromPlain  = differingShare(kept, plain.data());
    const float fromNoBake = differingShare(apart, noBake.data());
    const float baked      = differingShare(plain, noBake.data());
    std::printf("      its own coordinates: %.2f%% of pixels unlike the floor's frame\n", fromPlain * 100.0f);
    std::printf("      unwrapped apart: %.2f%% unlike no bake's\n", fromNoBake * 100.0f);
    expect("a level laid out as the floor reads its bake", fromPlain <= PIXEL_TOLERANCE, failures);
    expect("  one unwrapped apart reads none", fromNoBake <= PIXEL_TOLERANCE, failures);
    expect("  where the bake draws the tower's shadow", baked > 0.01f, failures);
}

// A fixed floor is baked as its own mesh into its own square whatever level it draws: baked with
// the eye far off, where it draws a level unwrapped apart that reads none of its bake, and then seen
// near, where it draws its own mesh, it draws what a backend that baked it near draws - the tower's
// shadow where it falls. Seen near for fewer frames than a bake settles in, so the bake made far off
// is the one shown.
void testAReceiverIsBakedAsItsGivenMeshWhateverLevelItDraws(int& failures) {
    std::printf("A fixed floor baked while it draws a level unwrapped apart, then seen near:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    // The floor draws the level past NEAR_REACH; FAR_BACK puts the eye well past it to bake.
    constexpr float NEAR_REACH = 100.0f;
    constexpr float FAR_BACK   = 400.0f;
    const auto build = [](Scenery& s) {
        buildBakeOnlyTower(s);
        MeshAsset plane = generatePlane(1.0f, 1.0f, 1, 1);
        for (const Vertex& v : plane.vertices) plane.lightmapUVs.push_back(v.uv * 0.5f + glm::vec2(0.25f));
        const MeshHandle level = s.resources.add(std::move(plane), "golden:baked_floor_apart");
        const MeshHandle floor = s.resources.findByName<MeshAsset>("golden:baked_floor");
        changeBakedFloor(s, [&](EntityId id, Mesh&) {
            LOD lod;
            lod.levels = {{floor, NEAR_REACH}, {level, std::numeric_limits<float>::max()}};
            s.scene.add(id, std::move(lod));
        });
    };

    restoreContextDefaults();
    WindowManager window;
    GLBackend lived;
    if (!lived.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    build(s);
    moveEyeBack(s, FAR_BACK);
    render(s, lived, nullptr, settings, nullptr, SETTLED);
    moveEyeBack(s, -FAR_BACK);
    static_assert(FRAMES < static_cast<int>(CAPTURE_SETTLE_FRAMES), "no bake settles while seen near");
    const std::vector<unsigned char> seen = render(s, lived, nullptr, settings, nullptr, FRAMES);
    const std::vector<unsigned char> want = freshFrame(build, settings, failures, SETTLED);
    if (seen.empty() || want.empty()) return;
    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels unlike the bake made near\n", share * 100.0f);
    expect("the floor baked as a level apart is baked as its own mesh", share <= PIXEL_TOLERANCE, failures);
}

// A wall far down the view and a ball before it, both in the sun's last cascade, over a floor; the
// sun moves, so nothing is baked. With @p wallFixed false every mesh moves.
void buildFarWall(Scenery& s, bool wall, bool wallFixed, float ballX) {
    base(s);
    s.scene.environment().sky.procedural   = false;
    s.scene.environment().sky.sunElevation = 30.0f;
    s.scene.environment().sky.sunAzimuth   = 80.0f;
    const MaterialHandle slate = material(s, "golden:far_slate", {0.45f, 0.45f, 0.45f}, 0.8f, 0.0f);
    place(s, s.cube, slate, {0.0f, -0.5f, -40.0f}, {120.0f, 1.0f, 120.0f});
    if (wall) place(s, s.cube, slate, {0.0f, 4.0f, -45.0f}, {12.0f, 8.0f, 1.0f});
    s.scene.forEach<Mesh>([wallFixed](EntityId, Mesh& mesh) { mesh.movable = !wallFixed; });
    const MaterialHandle red = material(s, "golden:far_ball", {0.8f, 0.1f, 0.1f}, 0.5f, 0.0f);
    place(s, s.sphere, red, {ballX, 3.5f, -28.0f}, glm::vec3(7.0f));
    sunAndCamera(s, {0.0f, 6.0f, 0.0f}, {0.0f, 0.0f, -40.0f});
    s.scene.forEach<Light>([](EntityId, Light& light) {
        light.movable        = true;
        light.shadowDistance = 60.0f;
    });
}

// The sun's last cascade starts from the layer its fixed casters hold and draws over it only what
// moves: a backend that kept the layer while the ball moved draws what a fresh one draws with every
// mesh moving, and the far wall's shadow is in it.
void testTheLastCascadeDrawsWhatItsLayerHolds(int& failures) {
    std::printf("The sun's last cascade, from its layer:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;

    restoreContextDefaults();
    WindowManager window;
    GLBackend lived;
    if (!lived.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    buildFarWall(s, true, true, 12.0f);
    render(s, lived, nullptr, settings);
    s.scene.forEach<Mesh>([&](EntityId id, Mesh& mesh) {
        if (mesh.mesh == s.sphere) s.scene.get<Transform>(id).position.x = -12.0f;
    });
    const std::vector<unsigned char> seen = render(s, lived, nullptr, settings);

    const auto fresh = [&](bool wall) {
        return freshFrame([=](Scenery& f) { buildFarWall(f, wall, false, -12.0f); }, settings, failures);
    };
    const std::vector<unsigned char> want = fresh(true);
    const std::vector<unsigned char> bare = fresh(false);
    if (seen.empty() || want.empty() || bare.empty()) return;

    const float fromWant = differingShare(seen, want.data());
    const float fromBare = differingShare(seen, bare.data());
    std::printf("      %.2f%% of pixels unlike every mesh moving\n", fromWant * 100.0f);
    std::printf("      %.2f%% unlike no wall's\n", fromBare * 100.0f);
    expect("the held layer draws what moving meshes draw", fromWant <= PIXEL_TOLERANCE, failures);
    expect("  with the far wall's shadow in it", fromBare > 0.01f, failures);
}

// A pond in a scene with no sea: water (MaterialAsset::water) on a plane, its ripples @p ripples of
// the scene's, running at @p flow along the plane's +U, under a low sun.
void buildPond(Scenery& s, float ripples, float flow) {
    base(s);
    s.scene.environment().sky.procedural   = false;
    s.scene.environment().sky.sunElevation = 25.0f;
    s.scene.environment().sky.sunAzimuth   = 200.0f;
    MaterialAsset water;
    water.water           = WaterKind::Body;
    water.rippleScale     = ripples;
    water.flowSpeed       = flow;
    water.albedo          = glm::vec4(0.02f, 0.05f, 0.06f, 1.0f);
    water.roughness       = 0.04f;
    water.subsurface      = 0.6f;
    water.subsurfaceColor = glm::vec3(0.06f, 0.22f, 0.24f);
    const MaterialHandle pond  = s.resources.add(std::move(water), "golden:pond");
    const MeshHandle     sheet = s.resources.add(generatePlane(1.0f, 1.0f, 16, 16), "golden:pond_sheet");
    place(s, sheet, pond, {0.0f, 0.0f, -10.0f}, {60.0f, 1.0f, 60.0f});
    sunAndCamera(s, {0.0f, 3.0f, 4.0f}, {0.0f, 0.0f, -8.0f});
}

// Water that is not the sea shows the scene's ripples with no sea in it, and its flow runs them
// along it.
void testAPondRipplesAndARiverRunsWithNoSea(int& failures) {
    std::printf("A pond, with no sea in the scene:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    g_worldTicks = 30;
    const auto pond = [&](float ripples, float flow) {
        return freshFrame([=](Scenery& s) { buildPond(s, ripples, flow); }, settings, failures);
    };
    const std::vector<unsigned char> glassy  = pond(0.0f, 0.0f);
    const std::vector<unsigned char> rippled = pond(1.0f, 0.0f);
    const std::vector<unsigned char> running = pond(1.0f, 3.0f);
    g_worldTicks = 90;
    const std::vector<unsigned char> later = pond(1.0f, 0.0f);
    g_worldTicks = 0;
    if (glassy.empty() || rippled.empty() || running.empty() || later.empty()) return;

    const float fromGlassy = differingShare(rippled, glassy.data());
    const float fromStill  = differingShare(running, rippled.data());
    const float fromBefore = differingShare(later, rippled.data());
    std::printf(
        "      %.2f%% of pixels unlike glassy water, %.2f%% a second on, %.2f%% of flowing unlike still\n",
        fromGlassy * 100.0f,
        fromBefore * 100.0f,
        fromStill * 100.0f
    );
    expect("its ripples show though the scene has no sea", fromGlassy > 0.05f, failures);
    expect("  moving as time goes", fromBefore > 0.05f, failures);
    expect("  and its flow runs them", fromStill > 0.05f, failures);
}

// A fixed light reads the mask baked for it only while it stands as it was baked: one moved, one
// that stopped casting, or another put in a gone light's slot, shines live until a bake of it lands.
void testABakedLightIsReadOnlyAsItWasBaked(int& failures) {
    std::printf("A light against the mask baked for it:\n");
    restoreContextDefaults();
    LightData lamp;
    lamp.type        = LightType::Point;
    lamp.entitySlot  = 5;
    lamp.position    = glm::vec3(1.0f, 2.0f, 3.0f);
    lamp.radius      = 8.0f;
    lamp.movable     = false;
    lamp.castShadows = true;

    GLLightmaps lightmaps;
    RenderObjects none;
    GLView mirror;
    lightmaps.beginBake(none, mirror, {}, 0, {{lamp.entitySlot, 0, GLLightmaps::shapeOf(lamp)}});
    lightmaps.finishBake();
    expect("the light as baked reads its channel", lightmaps.channels({lamp})[0] == 0, failures);
    LightData moved = lamp;
    moved.position.x += 1.0f;
    expect("  moved, it shines live", lightmaps.channels({moved})[0] == -1, failures);
    LightData other = lamp;
    other.type = LightType::Spot;
    expect("  and so does another in its slot", lightmaps.channels({other})[0] == -1, failures);
    LightData dark = lamp;
    dark.castShadows = false;
    expect("  and one that stopped casting", lightmaps.channels({dark})[0] == -1, failures);
}

// The lights a bake takes are the fixed casting ones, the first GLLightmaps::MAX_CHANNELS by entity
// slot whatever their order in the frame's list, so removing an unrelated light keeps each channel.
void testTheBakedLightsAreTheLowestSlots(int& failures) {
    std::printf("The lights a lightmap bake takes:\n");
    std::vector<LightData> lights;
    const auto channels = static_cast<size_t>(GLLightmaps::MAX_CHANNELS);
    for (uint32_t i = 0; i < channels + 4; ++i) {
        LightData lamp;
        lamp.type        = LightType::Point;
        lamp.entitySlot  = 20 - i;
        lamp.radius      = 8.0f;
        lamp.movable     = false;
        lamp.castShadows = true;
        lights.push_back(lamp);
    }
    lights[0].movable     = true;
    lights[1].castShadows = false;
    lights[2].type        = LightType::Rect;
    std::vector<uint32_t> baked;
    GLLightmaps::bakedLights(lights, baked);
    bool lowest = baked.size() == channels;
    for (size_t k = 0; lowest && k < baked.size(); ++k) lowest = baked[k] == lights.size() - 1 - k;
    expect("the lowest slots first, a movable, dark or rect light passed over", lowest, failures);
}

// A lightmap bake comes a step a frame - a view of a light's maps over a page - and ends as the bake
// made whole; one under way leaves the frame the lightmaps it had, and the backend counts its steps.
void testALightmapBakedOverFramesEndsAsOneBakedWhole(int& failures) {
    std::printf("Lightmaps baked a step a frame:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    // Two fixed lights, a step each: the sun's one tile and a lamp's six faces.
    const auto build = [](Scenery& s, bool tower) {
        buildBakedTower(s, true, tower, false);
        Light& lamp = addLamp(s, {4.0f, 3.0f, 4.0f}, 40.0f, 12.0f, true);
        lamp.movable = false;
    };

    restoreContextDefaults();
    WindowManager window;
    GLBackend lived;
    if (!lived.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    build(s, true);
    // A budget the CPU never spends: the steps are the GPU's, so it is still one a frame.
    g_bakeBudget = 1.0f;
    // A frame at a time until one step of the two is done; the frame after draws before the
    // second, which ends the bake. Then on until the backend owes nothing.
    const auto halfway = [&](RenderReadiness& seen) {
        for (int frame = 0; frame < 4 * SETTLED; ++frame) {
            render(s, lived, nullptr, settings, nullptr, 1);
            seen = lived.readiness();
            if (seen.done == 1 && seen.total == 2) {
                return render(s, lived, nullptr, settings, nullptr, 1);
            }
        }
        return std::vector<unsigned char>{};
    };
    const auto untilReady = [&] {
        std::vector<unsigned char> image;
        for (int frame = 0; frame < 4 * SETTLED; ++frame) {
            image = render(s, lived, nullptr, settings, nullptr, 1);
            if (lived.readiness().ready()) break;
        }
        return image;
    };
    RenderReadiness partway;
    halfway(partway);
    expect("part-way, the steps done and owed", partway.done == 1 && partway.total == 2, failures);
    const std::vector<unsigned char> sliced = untilReady();
    render(s, lived, nullptr, settings, nullptr, SETTLED);
    expect("once baked, the backend owes nothing more", lived.readiness().ready(), failures);

    // The tower stops casting: until the bake that follows ends, the frame keeps the shadow shown,
    // as the frames that wait for the change to settle do.
    s.scene.forEach<Mesh>([&](EntityId, Mesh& mesh) {
        if (mesh.mesh == s.cube) mesh.castShadows = false;
    });
    const std::vector<unsigned char> settling = render(s, lived, nullptr, settings, nullptr, 1);
    expect("a change settling is owed a bake", !lived.readiness().ready(), failures);
    RenderReadiness again;
    const std::vector<unsigned char> during = halfway(again);
    const bool underWay = again.done == 1 && !during.empty();
    const std::vector<unsigned char> after = untilReady();

    // The same world again, in its place, while a bake is under way: the bake goes, and what it
    // drew with; the lightmaps were of the last one, so it bakes anew.
    s.scene.forEach<Mesh>([&](EntityId, Mesh& mesh) {
        if (mesh.mesh == s.cube) mesh.castShadows = true;
    });
    bool heldWhileBaking = false;
    for (int frame = 0; frame < 4 * SETTLED && !heldWhileBaking; ++frame) {
        render(s, lived, nullptr, settings, nullptr, 1);
        const RenderReadiness owed = lived.readiness();
        heldWhileBaking = owed.done == 1 && owed.total == 2 && lived.lightmapBaker().holdsScratch();
    }
    s.scene.clear();
    build(s, true);
    s.scene.forEach<Mesh>([&](EntityId, Mesh& mesh) {
        if (mesh.mesh == s.cube) mesh.castShadows = false;
    });
    render(s, lived, nullptr, settings, nullptr, 1);
    expect("a world replaced by its like is owed a bake", !lived.readiness().ready(), failures);
    expect(
        "  and the bake under way lets go of its targets",
        heldWhileBaking && !lived.lightmapBaker().holdsScratch(),
        failures
    );
    g_bakeBudget = 0.0f;

    const auto whole = [&](bool tower) {
        return freshFrame(
            [&](Scenery& fresh) {
                build(fresh, true);
                if (tower) return;
                fresh.scene.forEach<Mesh>([&](EntityId, Mesh& mesh) {
                    if (mesh.mesh == fresh.cube) mesh.castShadows = false;
                });
            },
            settings,
            failures,
            SETTLED
        );
    };
    const std::vector<unsigned char> casting = whole(true);
    const std::vector<unsigned char> stopped = whole(false);
    if (sliced.empty() || casting.empty() || stopped.empty() || after.empty()) return;
    const float first = differingShare(sliced, casting.data());
    const float kept  = underWay ? differingShare(during, settling.data()) : 1.0f;
    const float last  = differingShare(after, stopped.data());
    const float moved = differingShare(stopped, settling.data());
    std::printf("      %.2f%% of pixels unlike the whole bake's\n", first * 100.0f);
    std::printf("      %.2f%% unlike the frame before the next bake, while it bakes\n", kept * 100.0f);
    std::printf("      %.2f%% unlike the new world's whole bake after\n", last * 100.0f);
    expect("it ends as the bake made whole", first <= PIXEL_TOLERANCE, failures);
    expect("a bake under way leaves the lightmaps shown", underWay && kept <= PIXEL_TOLERANCE, failures);
    expect("  until it ends as the new world's", last <= PIXEL_TOLERANCE, failures);
    expect("  which is not the old", moved > PIXEL_TOLERANCE, failures);
}

// A floor that casts nothing, under a fixed roof far above it, takes the roof's shadow into its
// bake: the floor lies past every caster along the sun, and the bake's depth reaches it. Past the
// sun's live reach, the bake is all that shades it: as it does when the floor casts too.
void testAReceiverBelowEveryCasterIsBakedInShadow(int& failures) {
    std::printf("A floor that casts nothing, far under a fixed roof:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto frame = [&](bool floorCasts) {
        return freshFrame(
            [=](Scenery& s) {
                buildBakedTower(s, true, false, false);
                s.scene.forEach<Mesh>([=](EntityId, Mesh& mesh) { mesh.castShadows = floorCasts; });
                const MaterialHandle roof = material(s, "golden:roof", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f);
                place(s, s.cube, roof, {0.0f, 6.0f, 0.0f}, {8.0f, 0.4f, 8.0f});
                s.scene.forEach<Mesh>([](EntityId, Mesh& mesh) { mesh.movable = false; });
                s.scene.forEach<Light>([](EntityId, Light& light) { light.shadowDistance = 2.0f; });
                // Over where the roof's shadow falls, looking down on it.
                const glm::vec3 sun    = s.scene.environment().sunDirection();
                const glm::vec3 shadow = glm::vec3(0.0f, 6.0f, 0.0f) - sun * (6.0f / sun.y);
                s.scene.forEach<Camera>([&](EntityId id, Camera&) {
                    Transform& eye = s.scene.get<Transform>(id);
                    eye.position   = shadow + glm::vec3(0.0f, 14.0f, 10.0f);
                    eye.rotation   = glm::quatLookAt(glm::normalize(shadow - eye.position), Math::WORLD_UP);
                });
            },
            settings,
            failures,
            SETTLED
        );
    };
    const std::vector<unsigned char> casting = frame(true);
    const std::vector<unsigned char> quiet   = frame(false);
    if (casting.empty() || quiet.empty()) return;
    const float apart = differingShare(quiet, casting.data());
    std::printf("      %.2f%% of pixels unlike the casting floor's\n", apart * 100.0f);
    expect("the roof's shadow is in the floor's bake", apart <= PIXEL_TOLERANCE, failures);
}

// The bakes draw into targets of their own at the end of a frame; whatever draws after it - an
// editor's UI - must find the window bound, not a bake's target, which a bake leaves incomplete.
void testAFrameEndsWithTheWindowBound(int& failures) {
    std::printf("What a frame that bakes leaves bound:\n");
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    buildBakedTower(s, true, true, false);
    RenderSettings settings;
    settings.msaaSamples = 1;
    int strayFrames = 0;
    const auto check = [&](int, const Visibility&) {
        GLint drawing = -1;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawing);
        if (drawing != 0) ++strayFrames;
    };
    render(s, backend, nullptr, settings, nullptr, SETTLED, check);
    std::printf("      %d frame(s) ended with another target bound\n", strayFrames);
    expect("every frame, the bake's among them, ends with the window bound", strayFrames == 0, failures);
}

// A soft fixed sun's bake past its live reach lights open ground as the live sun does: a wide
// search and filter at a grazing light compare against the ground's own plane, not its texel's
// depth, so it does not find itself nearer the light and shade itself.
void testASoftBakeLeavesOpenGroundLit(int& failures) {
    std::printf("A soft fixed sun over open ground, past its live reach:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto frame = [&](bool fixedSun) {
        return freshFrame(
            [=](Scenery& s) {
                buildBakedTower(s, fixedSun, true, false);
                s.scene.forEach<Light>([](EntityId, Light& light) {
                    light.sourceRadius   = 0.08f;
                    light.shadowDistance = 1.0f;
                });
            },
            settings,
            failures,
            SETTLED
        );
    };
    const std::vector<unsigned char> baked = frame(true);
    const std::vector<unsigned char> live  = frame(false);
    if (baked.empty() || live.empty()) return;
    int worst = 0;
    for (const glm::ivec2 at : {glm::ivec2(WIDTH - 20, HEIGHT - 10), glm::ivec2(20, HEIGHT - 10)}) {
        worst = std::max(worst, std::abs(pixel(baked, at.x, at.y).x - pixel(live, at.x, at.y).x));
    }
    std::printf("      open ground: %d levels apart at most\n", worst);
    expect("the bake lights open ground as the live sun does", worst <= 2, failures);
}

// A second fixed sun has no cascades, so its shadow is its bake alone, penumbra and all. Lit as the
// first sun is past its live reach, where the bake alone shades it too: a soft sun as the second,
// beside a first that gives no light, draws what the same sun draws as the first.
void testASecondFixedSunKeepsItsBakedPenumbra(int& failures) {
    std::printf("A second fixed sun, soft, beside the first:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto soft = [](Scenery& s, Light& light, Transform& pose) {
        // Near the scene's sun, so its shadow falls where the camera looks.
        const glm::vec3 first = -s.scene.environment().sunDirection();
        const glm::vec3 aim   = glm::angleAxis(glm::radians(20.0f), Math::WORLD_UP) * first;
        pose.rotation = glm::quatLookAt(aim, Math::WORLD_UP);
        light.intensity      = 2.0f;
        light.movable        = false;
        light.sourceRadius   = 0.08f;
        light.shadowDistance = 1.0f;
    };
    const auto frame = [&](bool asSecond) {
        return freshFrame(
            [=](Scenery& s) {
                buildBakedTower(s, true, true, false);
                s.scene.forEach<Light>([&](EntityId id, Light& light) {
                    if (asSecond) light.intensity = 0.0f;
                    else          soft(s, light, s.scene.get<Transform>(id));
                });
                if (!asSecond) return;
                const EntityId id = s.scene.createEntity();
                s.scene.add(id, Transform{});
                Light light;
                light.type        = LightType::Directional;
                light.castShadows = true;
                soft(s, light, s.scene.get<Transform>(id));
                s.scene.add(id, std::move(light));
            },
            settings,
            failures,
            SETTLED
        );
    };
    const std::vector<unsigned char> first  = frame(false);
    const std::vector<unsigned char> second = frame(true);
    if (first.empty() || second.empty()) return;
    const float apart = differingShare(second, first.data());
    std::printf("      %.2f%% of pixels unlike the same sun's as the first\n", apart * 100.0f);
    expect("its baked penumbra is drawn as the first sun's is", apart <= BAKED_TOLERANCE, failures);
}

// The penumbra scene at half the viewport's resolution on each axis: the composite scales it up
// over the whole frame, close to the full-resolution one but not it. A UI clip over it lands on
// the viewport's own pixels, as at full resolution.
void testAHalfScaleSceneFillsTheFrame(int& failures) {
    std::printf("A scene rendered at half scale:\n");
    RenderSettings full;
    full.msaaSamples = 1;
    RenderSettings half = full;
    half.renderScale = 0.5f;

    UIDrawData ui;
    solidQuad(ui, {0.0f, 0.0f}, {float(WIDTH), float(HEIGHT)}, glm::vec4(1.0f));
    ui.commands.push_back(UIDrawCmd{0, 6, UIRect{{10.5f, 20.5f}, {30.2f, 40.2f}}, {}});

    const std::vector<unsigned char> atFull = freshFrame(buildPenumbra, full, failures);
    if (atFull.empty()) return;

    // The half-scale scene keeps its backend: the clip is drawn over the frames after it.
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    buildPenumbra(s);
    const std::vector<unsigned char> atHalf  = render(s, backend, nullptr, half);
    const std::vector<unsigned char> clipped = render(s, backend, &ui, half);

    const double mean = meanLevelsApart(atFull, atHalf);
    const float differing = differingShare(atHalf, atFull.data());
    std::printf("      %.2f levels apart on average, %.2f%% of pixels differ\n", mean, 100.0f * differing);
    expect("it fills the frame, close to the full-resolution one", mean < 4.0, failures);
    expect("  and is the scene at fewer pixels, not that one", mean > 0.0, failures);

    const auto white = [&](int x, int y) { return pixel(clipped, x, y) == glm::ivec3(255); };
    expect(
        "a UI clip over it keeps the viewport's own pixels",
        white(10, 20) && white(40, 60) && !white(41, 30) && !white(20, 61) && !white(9, 30) && !white(20, 19),
        failures
    );
}

// Two walls a centimetre apart, a kilometre out, through a 10 cm near plane: the nearer is seen
// at every pixel. A 24-bit depth buffer, reversed or not, rounds the two to one depth there, and
// whichever draws last is seen; the eye's float depth, reversed, holds them apart
// (convention/gl_depth.h). Made in both orders, so neither wins by being drawn last.
void testTwoWallsACentimetreApartAKilometreOutDoNotFight(int& failures) {
    std::printf("Two walls a centimetre apart, a kilometre out:\n");
    RenderSettings plain;
    plain.msaaSamples     = 1;
    plain.cullMaxDistance = 0.0f;  // drawn however far

    int farther = 0;
    for (const bool nearFirst : {true, false}) {
        const auto build = [nearFirst](Scenery& s) {
            base(s);
            const auto wall = [&](const char* name, glm::vec3 emission, float z) {
                const MaterialHandle lit = material(s, name, glm::vec3(0.0f), 1.0f, 0.0f, emission);
                place(s, s.cube, lit, {0.0f, 0.0f, z}, {4000.0f, 3000.0f, 0.001f});
            };
            if (nearFirst) wall("golden:near_wall", {0.0f, 4.0f, 0.0f}, -999.99f);
            wall("golden:far_wall", {4.0f, 0.0f, 0.0f}, -1000.0f);
            if (!nearFirst) wall("golden:near_wall", {0.0f, 4.0f, 0.0f}, -999.99f);
            camera(s, glm::vec3(0.0f), {0.0f, 0.0f, -1.0f});
            s.scene.forEach<Camera>([](EntityId, Camera& lens) {
                lens.zNear = 0.1f;
                lens.zFar  = 2000.0f;
            });
        };
        const std::vector<unsigned char> frame = freshFrame(build, plain, failures);
        if (frame.empty()) return;
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                const glm::ivec3 p = pixel(frame, x, y);
                if (p.r >= p.g) ++farther;
            }
        }
    }
    std::printf("      %d of %d pixels show the farther wall\n", farther, 2 * WIDTH * HEIGHT);
    expect("the nearer wall is seen at every pixel, made first or last", farther == 0, failures);
}

// The reflections scene with a ray every two by two pixels: the resolve gathers each pixel's
// reflection from the hits on its own surface, so the frame is close to the full-resolution
// one, though not it.
void testHalfResolutionReflectionsAreCloseToFull(int& failures) {
    std::printf("Reflections traced at half resolution:\n");
    RenderSettings full;
    full.msaaSamples = 1;
    RenderSettings half = full;
    half.ssrHalfResolution = true;

    std::vector<unsigned char> frames[2];
    for (int i = 0; i < 2; ++i) {
        frames[i] = freshFrame(buildReflections, i == 0 ? full : half, failures);
        if (frames[i].empty()) return;
    }
    const double mean = meanLevelsApart(frames[0], frames[1]);
    const float differing = differingShare(frames[1], frames[0].data());
    std::printf("      %.2f levels apart on average, %.2f%% of pixels differ\n", mean, 100.0f * differing);
    expect("they are close to the full-resolution ones", mean < 1.0 && differing < 0.02f, failures);
    expect("  and are traced at fewer pixels, not those", mean > 0.0, failures);
}

// The plain program draws a plain material as the whole one would: the materials scene as it is,
// every material plain, and again with each given a clearcoat too faint for the shader to draw,
// which takes the whole program (GLForwardPrograms::programFor). The frames are the same but for a few
// highlight pixels the two compilations round apart; a feature lost from the plain program is
// thousands of pixels.
void testAPlainMaterialDrawsAsTheWholeProgramWould(int& failures) {
    std::printf("Plain materials by the plain program and by the whole one:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;

    std::vector<unsigned char> frames[2];
    for (int i = 0; i < 2; ++i) {
        const auto build = [i](Scenery& s) {
            buildMaterials(s);
            if (i == 1) {
                s.scene.forEach<Mesh>([&](EntityId, Mesh& mesh) {
                    s.resources.edit(mesh.material).clearcoat = 1e-6f;
                });
            }
        };
        frames[i] = freshFrame(build, settings, failures);
        if (frames[i].empty()) return;
    }
    const double mean = meanLevelsApart(frames[0], frames[1]);
    const float differing = differingShare(frames[1], frames[0].data());
    std::printf("      %.3f levels apart on average, %.3f%% of pixels differ\n", mean, 100.0f * differing);
    expect("the frames are the same", mean < 0.01 && differing < 0.0005f, failures);
}

// Each optional feature alone takes a material off the plain program, which would drop it; with
// none the material stays on it.
void testEveryOptionalFeatureTakesTheWholeProgram(int& failures) {
    std::printf("Which materials the plain program draws:\n");
    Scenery s;
    TextureAsset bumps;
    bumps.params.width  = 1;
    bumps.params.height = 1;
    bumps.pixelData     = {128, 128, 128, 255};
    const TextureHandle height = s.resources.add(std::move(bumps), "golden:bumps");

    const auto plain = [](const MaterialAsset& asset) { return GLMaterial(asset).plain(); };
    expect("a material with none is plain", plain(MaterialAsset{}), failures);
    GLForwardPrograms programs;
    programs.build();
    const Vkm::GL::Shader* wholeProgram = &programs.programFor(nullptr, false, false);
    const auto drawsWhole = [&](const GLMaterial& material) {
        return &programs.programFor(&material, false, false) == wholeProgram;
    };
    const GLMaterial none{MaterialAsset{}};
    expect("  and draws with the plain program, not a bare run's", !drawsWhole(none), failures);

    const std::pair<const char*, void (*)(MaterialAsset&)> features[] = {
        {"anisotropy",   [](MaterialAsset& m) { m.anisotropy = 0.5f; }},
        {"subsurface",   [](MaterialAsset& m) { m.subsurface = 0.5f; }},
        {"transmission", [](MaterialAsset& m) { m.transmission = 0.5f; }},
        {"clearcoat",    [](MaterialAsset& m) { m.clearcoat = 0.5f; }},
        {"sheen",        [](MaterialAsset& m) { m.sheenColor = {0.0f, 0.0f, 0.5f}; }},
    };
    for (const auto& [name, add] : features) {
        MaterialAsset asset;
        add(asset);
        const GLMaterial whole{asset};
        expect((std::string("  one with ") + name + " is not").c_str(), !whole.plain(), failures);
        expect("    and draws with the whole program", drawsWhole(whole), failures);
    }

    MaterialAsset parallax;
    parallax.heightTexture = height;
    expect("  one with a height map at no scale is", plain(parallax), failures);
    parallax.heightScale = 0.05f;
    expect("  one with a height map at a scale is not", !plain(parallax), failures);
    parallax.heightTexture = {};
    expect("  one with a scale and no map is", plain(parallax), failures);
}

// A floor and a row of five cubes in one stone, every mesh so far marked as never moving.
void buildFixedRow(Scenery& s) {
    base(s);
    const MaterialHandle stone = material(s, "golden:fixed_stone", {0.6f, 0.6f, 0.6f}, 0.8f, 0.0f);
    place(s, s.cube, stone, {0.0f, -0.5f, 0.0f}, {12.0f, 1.0f, 12.0f});
    for (int i = -2; i <= 2; ++i) {
        place(s, s.cube, stone, {static_cast<float>(i) * 1.5f, 0.5f, -2.0f}, glm::vec3(1.0f));
    }
    s.scene.forEach<Mesh>([](EntityId, Mesh& mesh) { mesh.movable = false; });
}

// Meshes that never move beside one that does, which moves mid-session: the backend refreshes the
// moved object's copy alone (RenderObjects::changed), and the frame must be the one a fresh backend
// draws of the moved scene.
void testAMovedObjectIsDrawnWhereItWent(int& failures) {
    std::printf("A mesh moved among fixed ones, mid-session:\n");
    const auto build = [](Scenery& s, float moverX) {
        buildFixedRow(s);
        const MaterialHandle red = material(s, "golden:mover_red", {0.8f, 0.1f, 0.1f}, 0.5f, 0.0f);
        place(s, s.sphere, red, {moverX, 0.5f, 1.0f}, glm::vec3(1.0f));
        sunAndCamera(s, {0.0f, 4.0f, 8.0f}, {0.0f, 0.0f, 0.0f});
    };
    RenderSettings plain;
    plain.msaaSamples = 1;

    WindowManager window;
    const auto there = [&](Scenery& s) { build(s, 2.0f); };
    const std::vector<unsigned char> want = freshFrame(there, plain, failures);
    if (want.empty()) return;

    restoreContextDefaults();
    GLBackend moved;
    if (!moved.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    build(s, -2.0f);
    // Moved after FRAMES frames, then watched in the frame that sees it.
    bool named = false;
    const auto moveAndWatch = [&](int frame, const Visibility& seen) {
        if (frame == FRAMES - 1) {
            s.scene.forEach<Mesh>([&](EntityId entity, Mesh& mesh) {
                if (mesh.movable) s.scene.get<Transform>(entity).position.x = 2.0f;
            });
        }
        if (frame == FRAMES) named = !seen.objects.allChanged && seen.objects.changed.size() == 1;
    };
    const std::vector<unsigned char> got = render(
        s,
        moved,
        nullptr,
        plain,
        nullptr,
        2 * FRAMES,
        moveAndWatch
    );

    const float differing = differingShare(got, want.data());
    std::printf("      %.2f%% of pixels unlike a fresh backend's frame\n", differing * 100.0f);
    expect("the move is named alone, the rest kept", named, failures);
    expect("  and the frame is the fresh backend's", differing <= PIXEL_TOLERANCE, failures);
}

// A fixed mesh destroyed and two spawned mid-session: the backend's copies grow, the mesh moved
// into the destroyed one's place and the spawned ones are refreshed alone
// (RenderObjects::changed), and the frame must be the one a fresh backend draws of the scene as it
// ends.
void testASpawnAndARemovalAreDrawnAsTheyEnd(int& failures) {
    std::printf("A fixed mesh destroyed and two spawned, mid-session:\n");
    const auto build = [](Scenery& s) {
        buildFixedRow(s);
        material(s, "golden:spawned_red", {0.8f, 0.1f, 0.1f}, 0.5f, 0.0f);
        sunAndCamera(s, {0.0f, 4.0f, 8.0f}, {0.0f, 0.0f, 0.0f});
    };
    // The leftmost cube goes, so the last in the storage moves into its place; two spheres come.
    const auto change = [](Scenery& s) {
        EntityId gone;
        s.scene.forEach<Mesh>([&](EntityId entity, Mesh&) {
            if (s.scene.get<Transform>(entity).position.x == -3.0f) gone = entity;
        });
        s.scene.destroyEntity(gone);
        const MaterialHandle red = s.resources.findByName<MaterialAsset>("golden:spawned_red");
        place(s, s.sphere, red, {1.0f, 0.5f, 1.0f}, glm::vec3(1.0f));
        place(s, s.sphere, red, {-1.5f, 0.5f, 1.0f}, glm::vec3(1.0f));
    };
    // Twice: the second with the occlusion cull on, whose world boxes grow in place too.
    for (const bool culled : {false, true}) {
        RenderSettings settings;
        settings.msaaSamples      = 1;
        settings.occlusionCulling = culled;

        WindowManager window;
        const auto ended = [&](Scenery& s) {
            build(s);
            change(s);
        };
        const std::vector<unsigned char> want = freshFrame(ended, settings, failures);
        if (want.empty()) return;

        restoreContextDefaults();
        GLBackend changed;
        if (!changed.init(window)) {
            expect("the backend starts", false, failures);
            return;
        }
        Scenery s;
        build(s);
        bool named = false;
        const auto changeAndWatch = [&](int frame, const Visibility& seen) {
            if (frame == FRAMES - 1) change(s);
            if (frame == FRAMES) named = !seen.objects.allChanged && seen.objects.changed.size() == 3;
        };
        const std::vector<unsigned char> got = render(
            s,
            changed,
            nullptr,
            settings,
            nullptr,
            2 * FRAMES,
            changeAndWatch
        );

        const float differing = differingShare(got, want.data());
        const char* how = culled ? ", culled" : "";
        std::printf("      %.2f%% of pixels unlike a fresh backend's frame%s\n", differing * 100.0f, how);
        expect("the moved and the spawned are named alone, the rest kept", named, failures);
        expect("  and the frame is the fresh backend's", differing <= PIXEL_TOLERANCE, failures);
    }
}

// A sphere moving through fixed scenery under the sun, a spot and a bulb: the tiles and faces it
// crosses are signed anew and drawn again, the rest keep last frame's batch. Once it stops, the
// frame must be the one a fresh backend draws of the scene as it ends.
void testAMoverOverFixedScenery(int& failures) {
    std::printf("A sphere moving over fixed scenery, under the sun, a spot and a bulb:\n");
    constexpr float START = -1.2f;
    constexpr float STEP  = 0.15f;
    constexpr int   MOVES = 2 * FRAMES - 2;  // The last two frames hold still.
    const auto build = [](Scenery& s, float sphereX) {
        base(s);
        const MaterialHandle stone = material(s, "golden:fixed_stone", {0.7f, 0.66f, 0.6f}, 0.8f, 0.0f);
        const MaterialHandle red   = material(s, "golden:mover_red", {0.8f, 0.1f, 0.1f}, 0.5f, 0.0f);
        place(s, s.cube, stone, {0.0f, -0.5f, 0.0f}, {14.0f, 1.0f, 14.0f});
        place(s, s.cube, stone, {0.0f, 1.5f, -2.5f}, {8.0f, 3.0f, 0.4f});
        for (float x : {-2.0f, 0.0f, 2.0f}) place(s, s.cube, stone, {x, 0.6f, -0.8f}, {0.5f, 1.2f, 0.5f});
        s.scene.forEach<Mesh>([](EntityId, Mesh& mesh) { mesh.movable = false; });
        place(s, s.sphere, red, {sphereX, 0.6f, 0.6f}, glm::vec3(0.8f));

        const EntityId lamp = s.scene.createEntity();
        Transform lampPose;
        lampPose.position = {0.0f, 4.0f, 0.5f};
        lampPose.rotation = glm::quatLookAt(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
        s.scene.add(lamp, std::move(lampPose));
        Light spot;
        spot.type           = LightType::Spot;
        spot.intensity      = 40.0f;
        spot.radius         = 10.0f;
        spot.outerConeAngle = 1.0f;
        spot.innerConeAngle = 0.8f;
        spot.castShadows    = true;
        s.scene.add(lamp, std::move(spot));

        addLamp(s, {1.5f, 1.4f, 1.6f}, 20.0f, 8.0f, true);
        sunAndCamera(s, {0.0f, 4.0f, 7.0f}, {0.0f, 0.5f, 0.0f});
    };
    RenderSettings plain;
    plain.msaaSamples = 1;

    WindowManager window;
    const auto ended = [&](Scenery& s) { build(s, START + STEP * static_cast<float>(MOVES)); };
    const std::vector<unsigned char> want = freshFrame(ended, plain, failures);
    if (want.empty()) return;

    restoreContextDefaults();
    GLBackend lived;
    if (!lived.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    build(s, START);
    const auto move = [&](int frame, const Visibility&) {
        if (frame >= MOVES) return;
        const float x = START + STEP * static_cast<float>(frame + 1);
        s.scene.forEach<Mesh>([&](EntityId entity, Mesh& mesh) {
            if (mesh.movable) s.scene.get<Transform>(entity).position.x = x;
        });
    };
    const std::vector<unsigned char> got = render(s, lived, nullptr, plain, nullptr, 2 * FRAMES, move);

    const float differing = differingShare(got, want.data());
    std::printf("      %.2f%% of pixels unlike a fresh backend's frame\n", differing * 100.0f);
    expect("the frame is the fresh backend's", differing <= PIXEL_TOLERANCE, failures);
}

void testTheUIOverlayLandsOnItsPixels(int& failures) {
    std::printf("The UI overlay, drawn:\n");

    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }

    // A white quad over the whole view, clipped to a rect whose every edge is half-way
    // across a pixel.
    UIDrawData ui;
    solidQuad(ui, {0.0f, 0.0f}, {float(WIDTH), float(HEIGHT)}, glm::vec4(1.0f));
    ui.commands.push_back(UIDrawCmd{0, 6, UIRect{{10.5f, 20.5f}, {30.2f, 40.2f}}, {}});

    Scenery scenery;
    const std::vector<unsigned char> frame = render(scenery, backend, &ui);
    const auto white = [&](int x, int y) { return pixel(frame, x, y) == glm::ivec3(255); };

    expect("a clip keeps the pixel its left and top edges cross", white(10, 20), failures);
    expect("  and the pixel its right and bottom edges cross", white(40, 60), failures);
    expect(
        "  and nothing past them",
        !white(41, 30) && !white(20, 61) && !white(9, 30) && !white(20, 19),
        failures
    );

    // A 2x2 picture, bottom row first (GL's order): red and green on top, blue and grey
    // below. sRGB, so grey 128 returns as 128 only if the pass encodes what was decoded.
    TextureAsset picture;
    picture.params.width          = 2;
    picture.params.height         = 2;
    picture.params.internalFormat = TextureInternalFormat::SRGBA8;
    picture.pixelData = {
          0,   0, 255, 255,   128, 128, 128, 255,
        255,   0,   0, 255,     0, 255,   0, 255,
    };
    UIDrawData pictured;
    solidQuad(pictured, {100.0f, 40.0f}, {180.0f, 120.0f}, glm::vec4(1.0f), true);
    solidQuad(pictured, {200.0f, 40.0f}, {280.0f, 120.0f}, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), false);
    Scenery framed;
    const TextureHandle handle = framed.resources.add(std::move(picture), "golden:picture");
    pictured.commands.push_back(
        UIDrawCmd{0, 12, UIRect{{0.0f, 0.0f}, {float(WIDTH), float(HEIGHT)}}, {}, handle}
    );

    const std::vector<unsigned char> shown = render(framed, backend, &pictured);
    const auto shows = [&](int x, int y, glm::ivec3 want) {
        const glm::ivec3 got = pixel(shown, x, y);
        return glm::all(glm::lessThanEqual(glm::abs(got - want), glm::ivec3(2)));
    };
    expect("a picture's top-left texel lands at the quad's top-left", shows(104, 44, {255, 0, 0}), failures);
    expect("  its top-right at the top-right", shows(175, 44, {0, 255, 0}), failures);
    expect("  and its bottom-left at the bottom-left", shows(104, 115, {0, 0, 255}), failures);
    expect(
        "  with an sRGB texel shown as the value it was stored as",
        shows(175, 115, {128, 128, 128}),
        failures
    );
    expect("a flat quad in the same run is not tinted by it", shows(240, 80, {255, 255, 255}), failures);
}

// A white mark half-way through its fade is half-way to white on the glass: the fade
// scales the encoded value, so it is linear in what is seen.
void testASplashFadesLinearlyOnTheGlass(int& failures) {
    std::printf("The splash, half faded:\n");

    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }

    const std::filesystem::path logo = std::filesystem::temp_directory_path() / "vkm_golden_splash.png";
    const unsigned char white[4 * 4 * 4] = {
        255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255,
        255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255,
        255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255,
        255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255,
    };
    stbi_write_png(logo.string().c_str(), 4, 4, 4, white, 4 * 4);

    SplashFrame splash;
    splash.key     = logo.string();
    splash.opacity = 0.5f;
    Scenery nothing;
    const std::vector<unsigned char> frame = render(nothing, backend, nullptr, RenderSettings{}, &splash);
    std::filesystem::remove(logo);

    const glm::ivec3 centre = pixel(frame, WIDTH / 2, HEIGHT / 2);
    std::printf("      the mark's centre is %d\n", centre.x);
    expect("the mark is half-way to white", std::abs(centre.x - 128) <= 2, failures);
    expect("  and the ground around it black", pixel(frame, 4, 4) == glm::ivec3(0), failures);
}

// A frame the loading screen covers runs no world pass, but its bakes after the frame draw the forward
// programs, which declare the shadow units as shadow samplers: the frame binds its atlas on them
// first, or the driver is asked to sample nothing through a compare.
void testACoveredFramesBakesFindTheShadowUnitsBound(int& failures) {
    std::printf("The shadow units under a covered frame:\n");

    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    // Empty, as a fresh context's are.
    const auto atlasUnit = static_cast<GLuint>(GLBindings::ShadowTextureSlots::ATLAS_2D);
    const auto cubesUnit = static_cast<GLuint>(GLBindings::ShadowTextureSlots::CUBE);
    glBindTextureUnit(atlasUnit, 0);
    glBindTextureUnit(cubesUnit, 0);

    Scenery    nothing;
    Visibility none;
    RenderView view;
    view.viewportWidth  = WIDTH;
    view.viewportHeight = HEIGHT;
    view.surfaceWidth   = WIDTH;
    view.surfaceHeight  = HEIGHT;
    view.build(nothing.scene, none, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    view.loadingOpacity = 1.0f;
    backend.render(view, nothing.resources);

    GLint atlas = 0;
    GLint cubes = 0;
    glActiveTexture(GL_TEXTURE0 + atlasUnit);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &atlas);
    glActiveTexture(GL_TEXTURE0 + cubesUnit);
    glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP_ARRAY, &cubes);
    glActiveTexture(GL_TEXTURE0);
    expect("the atlas is on its unit", atlas != 0, failures);
    expect("  and the point lights' cubes on theirs", cubes != 0, failures);
    expect("with no GL error behind it", glGetError() == GL_NO_ERROR, failures);
}

// The engine's ident is dark metal over black, seen only by a comet's light: dark while it is far
// behind, lit as it passes in front, and at the end red hot from its strike - warm, not white.
// The loading screen draws its own backdrop, water a current carries and the pointer stirs, and its
// bar fills with the progress; a project's own screen goes between them, over black if it asks.
void testTheIdentIsLitByItsCometAndTheLoadingScreenFlows(int& failures) {
    std::printf("The ident and the loading screen:\n");

    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    // The brightest pixel of a band of columns across the middle third of the rows.
    const auto brightest = [](const std::vector<unsigned char>& frame, int fromX, int toX) {
        int most = 0;
        for (int y = HEIGHT / 3; y < 2 * HEIGHT / 3; ++y) {
            for (int x = fromX; x < toX; ++x) most = std::max(most, pixel(frame, x, y).x);
        }
        return most;
    };

    Scenery     nothing;
    SplashFrame splash;
    splash.key     = "assets/logo/vkm_engine_logo_mono.png";
    splash.reveal  = true;
    splash.opacity = 1.0f;
    const auto at = [&](float seconds) {
        splash.time = seconds;
        return render(nothing, backend, nullptr, RenderSettings{}, &splash, 1);
    };
    // Moments on gl_splash_pass.cpp's timeline: before the comet comes round, as it passes in front,
    // and after its strike (STRUCK_AT).
    const std::vector<unsigned char> early = at(0.3f);
    const std::vector<unsigned char> lit   = at(3.0f);
    const std::vector<unsigned char> hot   = at(9.0f);
    const int darkest = brightest(early, WIDTH / 3, 2 * WIDTH / 3);
    const int shown   = brightest(lit, WIDTH / 3, 2 * WIDTH / 3);
    // How much redder than blue the logo's middle is, summed: hot metal glows red, a light is white.
    long warmth = 0;
    for (int y = HEIGHT / 3; y < 2 * HEIGHT / 3; ++y) {
        for (int x = WIDTH / 3; x < 2 * WIDTH / 3; ++x) warmth += pixel(hot, x, y).x - pixel(hot, x, y).z;
    }
    std::printf(
        "      brightest: with the comet far behind %d, as it passes %d; red over blue when hot %ld\n",
        darkest,
        shown,
        warmth
    );
    expect("the ident is dark while the comet is far behind it", darkest + 150 < shown, failures);
    expect("  shows its logo where the comet's light is", shown > 150, failures);
    expect("  and glows red hot once struck", warmth > 1000, failures);
    expect("  over black", glm::all(glm::lessThanEqual(pixel(lit, 2, 2), glm::ivec3(2))), failures);

    // The loading screen, which a frame with no camera still shows.
    RenderView view;
    view.viewportWidth  = WIDTH;
    view.viewportHeight = HEIGHT;
    view.surfaceWidth   = WIDTH;
    view.surfaceHeight  = HEIGHT;
    view.frameSeconds   = FRAME_SECONDS;
    view.pointer        = glm::vec2(-1e4f);
    const UIDrawData* cover = nullptr;
    const auto loading = [&](float shown, float progress = 0.0f) {
        Visibility none;
        view.build(nothing.scene, none, nullptr, cover, nullptr, nullptr, nullptr, nullptr);
        view.loadingOpacity  = shown;
        view.loadingContent  = shown;
        view.loadingProgress = progress;
        backend.render(view, nothing.resources);
        std::vector<unsigned char> image;
        backend.readFrame(view, image);
        return image;
    };
    const std::vector<unsigned char> bare = loading(0.0f);
    std::vector<unsigned char> covered;
    for (int i = 0; i < 4; ++i) covered = loading(1.0f);
    std::vector<unsigned char> later;
    for (int i = 0; i < 8; ++i) later = loading(1.0f);
    expect("the loading screen draws its water", differingShare(bare, covered.data()) > 0.005f, failures);
    const float carried = differingShare(covered, later.data());
    std::printf("      %.2f%% of pixels changed as its current carried it\n", carried * 100.0f);
    expect("  and its current carries it", carried > 0.0005f, failures);
    // On a strip far wider than high too: the grid stops widening where any backend's textures hold it.
    view.viewportHeight = 1;
    loading(0.0f);
    loading(1.0f);
    expect("  and on a strip far wider than high, with no GL error", glGetError() == GL_NO_ERROR, failures);
    view.viewportHeight = HEIGHT;

    // Started afresh each time it comes up, so two runs differ only by the pointer: one off the
    // screen, one sweeping across its middle and then gone, its wake still on the water after it.
    const auto run = [&](bool stirred) {
        loading(0.0f);
        std::vector<unsigned char> image;
        for (int i = 0; i < 18; ++i) {
            const float     across = static_cast<float>(i) / 12.0f;
            const glm::vec2 middle(WIDTH * (0.3f + 0.4f * across), HEIGHT * 0.5f);
            view.pointer = stirred && i < 12 ? middle : glm::vec2(-1e4f);
            image = loading(1.0f);
        }
        return image;
    };
    const std::vector<unsigned char> still   = run(false);
    const std::vector<unsigned char> stirred = run(true);
    const float stir = differingShare(still, stirred.data());
    std::printf("      %.2f%% of pixels differ where the pointer stirred it\n", stir * 100.0f);
    expect("  and the pointer leaves a wake", stir > 0.0005f, failures);

    // A screen that comes up filled shows its bar filled: fresh, the fill does not ease in.
    view.pointer = glm::vec2(-1e4f);
    loading(0.0f);
    const std::vector<unsigned char> empty = loading(1.0f, 0.0f);
    loading(0.0f);
    const std::vector<unsigned char> full = loading(1.0f, 1.0f);
    const float filled = differingShare(empty, full.data());
    std::printf("      %.2f%% of pixels differ between its bar empty and full\n", filled * 100.0f);
    expect("  and its bar fills with the progress", filled > 0.0005f, failures);

    // A project's own screen, one red quad over it all, on black where it turns the water off.
    view.loadingWater = false;
    loading(0.0f);
    const std::vector<unsigned char> black = loading(1.0f);
    const bool dark = glm::all(glm::lessThanEqual(pixel(black, WIDTH / 2, HEIGHT / 2), glm::ivec3(2)));
    expect("  plain black where a project turns the water off", dark, failures);
    UIDrawData screen;
    const glm::vec2 size(WIDTH, HEIGHT);
    // Two triangles over it, corner by corner.
    const glm::vec2 corners[] = {
        glm::vec2(0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f),
        glm::vec2(0.0f), glm::vec2(1.0f),       glm::vec2(0.0f, 1.0f),
    };
    for (const glm::vec2 corner : corners) {
        UIVertex vertex{};
        vertex.pos   = corner * size;
        vertex.uv    = corner;
        vertex.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        vertex.shape = glm::vec4(size, 0.0f, 0.0f);
        screen.vertices.push_back(vertex);
    }
    screen.commands.push_back(UIDrawCmd{0, 6, UIRect{glm::vec2(0.0f), size}, {}, {}});
    cover = &screen;
    loading(0.0f);
    const std::vector<unsigned char> under = loading(1.0f, 0.0f);
    loading(0.0f);
    const std::vector<unsigned char> over = loading(1.0f, 1.0f);
    const glm::ivec3 red = pixel(over, WIDTH / 2, HEIGHT / 3);
    expect("  a project's screen drawn over it", red.x > 200 && red.y < 30 && red.z < 30, failures);
    expect("    and the bar over that", differingShare(under, over.data()) > 0.0005f, failures);
    cover             = nullptr;
    view.loadingWater = true;
}

// A transparent pane's shadow is as deep as the pane is opaque: half clear casts about half a
// shadow, and two half-clear panes stacked about three quarters, each pane its own dither. Read off
// the direct light alone, whose display curve (Reinhard, then sRGB) the test undoes.
void testATransparentPanesShadowIsAsDeepAsItIsOpaque(int& failures) {
    std::printf("Shadows of transparent panes:\n");

    // Looking down at a slant, so no pane stands between the eye and a shadow it is read at.
    const glm::vec3 eye    = {0.0f, 16.0f, 12.0f};
    const glm::vec3 target = {1.0f, 0.0f, 0.0f};
    const auto build = [&](Scenery& s) {
        base(s);
        const MaterialHandle ground = material(s, "golden:ground", {0.3f, 0.3f, 0.3f}, 0.9f, 0.0f);
        place(s, s.cube, ground, {0.0f, -0.5f, 0.0f}, {80.0f, 1.0f, 80.0f});
        const auto pane = [&](const char* name, float opacity) {
            MaterialAsset m;
            m.type      = MaterialType::Transparent;
            m.albedo    = glm::vec4(0.8f, 0.8f, 0.8f, opacity);
            m.roughness = 0.5f;
            return s.resources.add(std::move(m), name);
        };
        // The sun 45 degrees up from +X: a pane 4 m up shades the floor 4 m toward -X of itself.
        s.scene.environment().sky.sunElevation = 45.0f;
        s.scene.environment().sky.sunAzimuth   = 90.0f;
        const glm::vec3 slab = {3.0f, 0.1f, 3.0f};
        place(s, s.cube, pane("golden:opaque", 1.0f), {6.0f, 4.0f, -5.0f}, slab);
        place(s, s.cube, pane("golden:half", 0.5f), {6.0f, 4.0f, 0.0f}, slab);
        place(s, s.cube, pane("golden:lower", 0.5f), {6.0f, 4.0f, 5.0f}, slab);
        place(s, s.cube, pane("golden:upper", 0.5f), {6.0f, 4.05f, 5.0f}, slab);
        sunAndCamera(s, eye, target);
    };

    RenderSettings settings;
    settings.renderMode  = RenderMode::DirectOnly;
    settings.tonemap     = Tonemap::Reinhard;
    settings.msaaSamples = 1;
    settings.bloom       = false;
    settings.ssr         = false;
    settings.gtao        = false;
    const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
    if (frame.empty()) return;

    // Linear light under a floor point, as the frame drew it: a 5x5 mean, the curves undone.
    Camera lens;
    lens.aspect = static_cast<float>(WIDTH) / static_cast<float>(HEIGHT);
    Transform pose;
    pose.position = eye;
    pose.rotation = glm::quatLookAt(glm::normalize(target - eye), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection     = Camera::computeProjection(lens, lens.aspect);
    const glm::mat4 viewProjection = projection * Transform::computeView(pose);
    const auto lightAt = [&](glm::vec3 at) {
        const glm::vec4 clip = viewProjection * glm::vec4(at, 1.0f);
        const int       cx   = static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * WIDTH);
        const int       cy   = static_cast<int>((0.5f - clip.y / clip.w * 0.5f) * HEIGHT);
        float sum = 0.0f;
        for (int y = cy - 2; y <= cy + 2; ++y) {
            for (int x = cx - 2; x <= cx + 2; ++x) {
                const float encoded = static_cast<float>(pixel(frame, x, y).y) / 255.0f;
                const float shown   = encoded <= 0.04045f
                    ? encoded / 12.92f
                    : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
                sum += shown / std::max(1.0f - shown, 1e-3f);
            }
        }
        return sum / 25.0f;
    };
    const float open  = lightAt({-8.0f, 0.0f, 0.0f});
    // How deep the shadow under the pane at depth @p z is, by the light the open floor has.
    const auto depthAt = [&](float z) { return open > 0.0f ? 1.0f - lightAt({2.0f, 0.0f, z}) / open : 0.0f; };
    const float full  = depthAt(-5.0f);
    const float shade = depthAt(0.0f);
    const float two   = depthAt(5.0f);
    std::printf("      shadow depth: opaque %.2f, half clear %.2f, two half clear %.2f\n", full, shade, two);
    expect("a pane wholly opaque casts a whole shadow", full > 0.9f, failures);
    expect("  a half-clear pane's about half", shade > 0.35f && shade < 0.65f, failures);
    expect("  and two of them stacked about three quarters", two > 0.6f && two < 0.9f, failures);
}

// The ocean's waterline foam is read off its shore, drawn in the world: water rings a post the
// camera cannot see, standing on a seabed that hides its foot, and nothing hovering clear of the
// water foams it. Asserted per pixel, foam colored green over dark water, looking straight down.
void testTheShoreRingsWhatBreaksTheWater(int& failures) {
    std::printf("The ocean's waterline foam:\n");

    const auto build = [](Scenery& s) {
        base(s);
        MaterialAsset sea;
        sea.albedo    = glm::vec4(0.01f, 0.02f, 0.04f, 1.0f);
        sea.roughness = 0.3f;
        sea.water     = WaterKind::Sea;
        const MaterialHandle water = s.resources.add(std::move(sea), "golden:sea");
        place(s, s.cube, water, {0.0f, -0.5f, 0.0f}, {200.0f, 1.0f, 200.0f});

        const EntityId oceanId = s.scene.createEntity();
        s.scene.add(oceanId, Transform{});
        Ocean ocean;
        ocean.windSpeed     = 2.0f;
        ocean.foam          = 0.0f;
        ocean.waterlineFoam = 1.0f;
        ocean.foamColor     = glm::vec3(0.0f, 1.0f, 0.0f);
        s.scene.add(oceanId, ocean);

        // Looking down from six metres the frame spans seven and a half either way across: the
        // post stands just past its right edge, its top too low to show, and the block hovers
        // clear of the water on the left.
        const MaterialHandle grey = material(s, "golden:post", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f);
        place(s, s.cube, grey, {8.0f, -0.6f, 0.0f}, {0.5f, 1.8f, 0.5f});
        place(s, s.cube, grey, {8.0f, -1.5f, 0.0f}, {6.0f, 1.0f, 6.0f});
        place(s, s.cube, grey, {-3.5f, 1.0f, 0.0f}, {1.0f, 0.6f, 1.0f});
        sunAndCamera(s, {0.0f, 6.0f, 0.01f}, {0.0f, 0.0f, 0.0f});
    };

    const std::vector<unsigned char> frame = freshFrame(build, RenderSettings{}, failures);
    if (frame.empty()) return;
    const auto foamIn = [&](glm::ivec2 from, glm::ivec2 to) {
        int foam = 0;
        for (int y = from.y; y < to.y; ++y) {
            for (int x = from.x; x < to.x; ++x) {
                const glm::ivec3 p = pixel(frame, x, y);
                if (p.y > 40 && p.y > 2 * p.x && p.y > 2 * p.z) ++foam;
            }
        }
        return static_cast<float>(foam) / static_cast<float>((to.x - from.x) * (to.y - from.y));
    };
    // The right edge's last half metre; the whole left half.
    const float right = foamIn({WIDTH - 10, 0}, {WIDTH, HEIGHT});
    const float left  = foamIn({0, 0}, {WIDTH / 2, HEIGHT});
    std::printf(
        "      foam over %.1f%% of the right edge, %.2f%% of the left half\n",
        100.0f * right,
        100.0f * left
    );
    expect("water foams round a post the camera cannot see", right > 0.02f, failures);
    expect("  and nowhere else: not round a block hovering clear of it", left == 0.0f, failures);
}

// A mirror shows the sea as the frame does, its waterline foam included: a post's ring, seen in a
// mirror standing in the water across from it, in the band of the frame only the mirror reaches.
void testAMirrorShowsTheWaterlineFoam(int& failures) {
    std::printf("The waterline foam, in a mirror:\n");

    const auto build = [](Scenery& s) {
        base(s);
        MaterialAsset sea;
        sea.albedo    = glm::vec4(0.01f, 0.02f, 0.04f, 1.0f);
        sea.roughness = 0.3f;
        sea.water     = WaterKind::Sea;
        const MaterialHandle water = s.resources.add(std::move(sea), "golden:sea");
        place(s, s.cube, water, {0.0f, -0.5f, 0.0f}, {200.0f, 1.0f, 200.0f});

        const EntityId oceanId = s.scene.createEntity();
        s.scene.add(oceanId, Transform{});
        Ocean ocean;
        ocean.windSpeed     = 2.0f;
        ocean.foam          = 0.0f;
        ocean.waterlineFoam = 1.0f;
        ocean.foamColor     = glm::vec3(0.0f, 1.0f, 0.0f);
        s.scene.add(oceanId, ocean);

        const MaterialHandle silver = material(s, "golden:mirror", {0.95f, 0.95f, 0.95f}, 0.02f, 1.0f);
        place(s, s.cube, silver, {0.0f, 3.0f, -3.0f}, {12.0f, 12.0f, 0.05f});
        const EntityId glass = s.scene.createEntity();
        Transform at;
        at.position = {0.0f, 3.0f, -2.975f};
        at.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
        s.scene.add(glass, std::move(at));
        PlanarReflector mirror;
        mirror.halfExtents = {6.0f, 6.0f};
        s.scene.add(glass, std::move(mirror));

        const MaterialHandle grey = material(s, "golden:post", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f);
        place(s, s.cube, grey, {0.0f, 0.4f, 0.0f}, {0.5f, 1.8f, 0.5f});
        sunAndCamera(s, {0.0f, 3.0f, 6.0f}, {0.0f, 3.0f, -3.0f});
    };
    const std::vector<unsigned char> frame = freshFrame(build, RenderSettings{}, failures);
    if (frame.empty()) return;
    int foam = 0;
    for (int y = 6 * HEIGHT / 10; y < 72 * HEIGHT / 100; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            const glm::ivec3 p = pixel(frame, x, y);
            if (p.y > 40 && p.y > 2 * p.x && p.y > 2 * p.z) ++foam;
        }
    }
    std::printf("      %d foam pixels where only the mirror shows the post's foot\n", foam);
    expect("the mirror shows the post's waterline foam", foam > 0, failures);
}

// What the occlusion cull hid draws on the very frame it comes out: the prepass's second draw
// catches what its first, from the last frame's verdicts, left out. A sphere behind a wall, the
// wall moved aside: that one frame against a backend with no cull.
void testWhatTheCullHidDrawsTheFrameItComesOut(int& failures) {
    std::printf("A sphere the occlusion cull hid, when the wall in front of it moves aside:\n");

    // The wall is the only box 16 wide.
    const auto build = [](Scenery& s) {
        base(s);
        const MaterialHandle ground  = material(s, "golden:ground", {0.45f, 0.42f, 0.38f}, 0.9f, 0.0f);
        const MaterialHandle red     = material(s, "golden:red", {0.8f, 0.1f, 0.1f}, 0.4f, 0.0f);
        const MaterialHandle plaster = material(s, "golden:plaster", {0.7f, 0.68f, 0.64f}, 0.9f, 0.0f);
        place(s, s.cube, ground, {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f});
        place(s, s.sphere, red, {0.0f, 1.75f, -4.0f}, glm::vec3(3.5f));
        place(s, s.cube, plaster, {0.0f, 3.0f, 0.0f}, {16.0f, 6.0f, 0.4f});
        sunAndCamera(s, {0.0f, 1.5f, 6.0f}, {0.0f, 1.0f, -4.0f});
    };
    const auto moveWall = [](Scenery& s) {
        s.scene.forEach<Transform>([](EntityId, Transform& t) {
            if (t.scale.x == 16.0f) t.position.x = 24.0f;
        });
    };

    RenderSettings culling;
    culling.occlusionCulling = true;

    restoreContextDefaults();
    WindowManager window;
    GLBackend culled;
    if (!culled.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    using Set = GLOcclusionCull::Set;
    Scenery hidden;
    build(hidden);
    render(hidden, culled, nullptr, culling);
    const uint32_t shownBehind = culled.occlusionCull().countIn(Set::Shown);
    moveWall(hidden);
    const std::vector<unsigned char> seen = render(hidden, culled, nullptr, culling, nullptr, 1);
    const uint32_t revealed = culled.occlusionCull().countIn(Set::Revealed);
    const uint32_t shown    = culled.occlusionCull().countIn(Set::Shown);
    std::printf("      %u shown behind the wall; then %u revealed, %u shown\n", shownBehind, revealed, shown);
    expect("behind the wall, the sphere is culled and the ground and wall shown", shownBehind == 2, failures);
    // The wall moved out of the view: the ground and the sphere.
    expect("  and out from behind it, the second draw takes it", revealed == 1 && shown == 2, failures);

    restoreContextDefaults();
    GLBackend plain;
    if (!plain.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery open;
    build(open);
    moveWall(open);
    const std::vector<unsigned char> want = render(open, plain);

    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels differ\n", share * 100.0f);
    expect("its first frame out is the frame without the cull", share <= PIXEL_TOLERANCE, failures);
}

// A weapon held into a wall: its world box lies wholly behind the wall's face, yet the viewmodel
// draws through its own lens in front of everything, so the occlusion cull shows it whatever the
// depth says - at four samples and at the one the Low tier renders with.
void testAWeaponHeldIntoAWallIsNotCulled(int& failures) {
    std::printf("A weapon held into a wall, with the occlusion cull on:\n");
    const auto held = [](Scenery& s) { buildHeld(s, SeenBy::Viewmodel, -0.75f); };
    for (const uint32_t samples : {4u, 1u}) {
        std::vector<unsigned char> frames[2];
        for (const bool culled : {false, true}) {
            RenderSettings settings;
            settings.msaaSamples      = samples;
            settings.occlusionCulling = culled;
            std::vector<unsigned char>& frame = frames[culled ? 1 : 0];
            frame = freshFrame(held, settings, failures);
            if (frame.empty()) return;
        }
        const float share = differingShare(frames[1], frames[0].data());
        std::printf("      %u sample(s): %.2f%% of pixels differ, culled\n", samples, share * 100.0f);
        const char* what = samples > 1 ? "the weapon shows as without the cull" : "  and at one sample";
        expect(what, share <= PIXEL_TOLERANCE, failures);
    }
}

// A pool of the ocean's water behind a wall: its box is the CPU cull's, reaching as far as the sea
// moves a wave, so the occlusion cull hides it as it hides anything else.
void testWaterBehindAWallIsCulled(int& failures) {
    std::printf("A pool of moving water behind a wall, with the occlusion cull on:\n");

    restoreContextDefaults();
    WindowManager window;
    GLBackend culled;
    if (!culled.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    base(s);
    const MaterialHandle ground  = material(s, "golden:ground", {0.45f, 0.42f, 0.38f}, 0.9f, 0.0f);
    const MaterialHandle plaster = material(s, "golden:plaster", {0.7f, 0.68f, 0.64f}, 0.9f, 0.0f);
    MaterialAsset sea;
    sea.albedo = glm::vec4(0.01f, 0.02f, 0.04f, 1.0f);
    sea.water  = WaterKind::Sea;
    const MaterialHandle water = s.resources.add(std::move(sea), "golden:pool");
    place(s, s.cube, ground, {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f});
    place(s, s.cube, water, {0.0f, 0.1f, -5.0f}, {3.0f, 0.2f, 3.0f});
    place(s, s.cube, plaster, {0.0f, 3.0f, 0.0f}, {16.0f, 6.0f, 0.4f});
    const EntityId oceanId = s.scene.createEntity();
    s.scene.add(oceanId, Transform{});
    Ocean ocean;
    ocean.windSpeed = 2.0f;
    s.scene.add(oceanId, ocean);
    sunAndCamera(s, {0.0f, 1.5f, 6.0f}, {0.0f, 1.0f, -4.0f});

    RenderSettings culling;
    culling.occlusionCulling = true;
    render(s, culled, nullptr, culling);
    const uint32_t shown = culled.occlusionCull().countIn(GLOcclusionCull::Set::Shown);
    std::printf("      %u shown\n", shown);
    expect("the ground and the wall are shown, the water behind it not", shown == 2, failures);
}

// A box seen face-on, its face filling an orthographic frame, is tested against the depth it laid
// itself: the box's nearest point and the depth buffer's rounding of the same face must not hide
// it. Frame by frame, as the verdict alternating would show only some of them. Without the cull's
// DEPTH_SLACK it fails only where the depth buffer rounds the hiding way.
void testAFaceFillingTheFrameIsNotCulledByItsOwnDepth(int& failures) {
    std::printf("A wall face-on, filling the frame, with the occlusion cull on:\n");

    const auto build = [](Scenery& s) {
        base(s);
        const MaterialHandle plaster = material(s, "golden:plaster", {0.7f, 0.68f, 0.64f}, 0.9f, 0.0f);
        place(s, s.cube, plaster, {0.0f, 2.0f, 0.0f}, {40.0f, 30.0f, 1.0f});
        sunAndCamera(s, {0.0f, 2.0f, 3.5f}, {0.0f, 2.0f, 0.0f});
        // Orthographic, as the editor's side views are: every pixel of the face at one depth.
        orthographic(s, 5.0f);
    };

    RenderSettings culling;
    culling.occlusionCulling = true;

    restoreContextDefaults();
    WindowManager window;
    GLBackend plain;
    if (!plain.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery seen;
    build(seen);
    const std::vector<unsigned char> want = render(seen, plain);

    restoreContextDefaults();
    GLBackend culled;
    if (!culled.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery wall;
    build(wall);
    constexpr int WATCHED = 8;
    float worst = 0.0f;
    for (int frame = 0; frame < WATCHED; ++frame) {
        const std::vector<unsigned char> frameSeen = render(wall, culled, nullptr, culling, nullptr, 1);
        worst = std::max(worst, differingShare(frameSeen, want.data()));
    }
    std::printf("      at most %.2f%% of pixels differ over %d frames\n", worst * 100.0f, WATCHED);
    expect("every frame is the frame without the cull", worst <= PIXEL_TOLERANCE, failures);
}

// A crease 35 cm away, near enough that GTAO caps its reach, seen level and with the camera
// rolled a quarter turn: the same occlusion either way. The cap is in pixels; one in screen UV
// would reach 1.78 times as far across a 16:9 frame as up it, and the crease would darken by
// which way it ran. Over the frame's central square, which the roll turns onto itself.
void testGTAOReachesAsFarEveryWay(int& failures) {
    std::printf("Ambient occlusion up close, level and rolled:\n");
    RenderSettings settings;
    settings.renderMode    = RenderMode::AmbientOcclusion;
    settings.msaaSamples   = 1;
    settings.gtaoIntensity = 3.0f;
    float darkness[2] = {};
    for (const bool rolled : {false, true}) {
        const auto build = [rolled](Scenery& s) {
            base(s);
            const MaterialHandle grey = material(s, "golden:grey", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f);
            place(s, s.cube, grey, {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f});
            place(s, s.cube, grey, {0.0f, 2.0f, -0.5f}, {20.0f, 4.0f, 1.0f});
            sunAndCamera(s, {0.0f, 0.25f, 0.25f}, {0.0f, 0.0f, 0.0f});
            if (rolled) {
                s.scene.forEach<Camera>([&](EntityId id, Camera&) {
                    Transform& eye = s.scene.get<Transform>(id);
                    const glm::quat roll = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
                    eye.rotation         = eye.rotation * roll;
                });
            }
        };
        const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
        if (frame.empty()) return;
        long sum = 0;
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = (WIDTH - HEIGHT) / 2; x < (WIDTH + HEIGHT) / 2; ++x) {
                sum += 255 - pixel(frame, x, y).x;
            }
        }
        darkness[rolled ? 1 : 0] = static_cast<float>(sum) / static_cast<float>(HEIGHT * HEIGHT);
    }
    std::printf("      darkness %.2f level, %.2f rolled\n", darkness[0], darkness[1]);
    const bool even = std::abs(darkness[1] - darkness[0]) <= 0.1f * darkness[0];
    expect("a crease up close is as occluded rolled a quarter turn", even, failures);
}

// An open wall beside the eye, through a wide lens, in the AO view: a flat wall occludes nothing,
// up close too. Near the lens a slice's radius can reach past the eye's plane, where it projects
// back the wrong way; a slice drawn by it would read the wall's own horizons on the wrong side and
// darken it.
void testAWallBesideTheEyeIsOpen(int& failures) {
    std::printf("Ambient occlusion of an open wall beside the eye:\n");
    const auto build = [](Scenery& s) {
        base(s);
        const MaterialHandle grey = material(s, "golden:grey", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f);
        place(s, s.cube, grey, {0.3f, 1.0f, -10.0f}, {0.2f, 6.0f, 22.0f});
        sunAndCamera(s, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, -1.0f});
        s.scene.forEach<Camera>([](EntityId, Camera& lens) { lens.fovY = glm::radians(100.0f); });
    };

    RenderSettings settings;
    settings.renderMode    = RenderMode::AmbientOcclusion;
    settings.msaaSamples   = 1;
    settings.gtaoIntensity = 3.0f;
    const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
    if (frame.empty()) return;
    // The right fifth of the frame is the wall, from a metre off down to the lens.
    long sum = 0;
    int  count = 0;
    for (int y = HEIGHT / 4; y < 3 * HEIGHT / 4; ++y) {
        for (int x = 4 * WIDTH / 5; x < WIDTH; ++x) {
            sum += 255 - pixel(frame, x, y).x;
            ++count;
        }
    }
    const float darkness = static_cast<float>(sum) / static_cast<float>(count);
    std::printf("      darkness %.2f on the wall\n", darkness);
    expect("an open wall beside the eye is not occluded", darkness <= 2.0f, failures);
}

// A courtyard by day and by night, under a lamp: as lit, day is many times the brighter on the
// screen; with auto-exposure each is brought toward the same brightness, as an eye adapts, and
// a view adapted to the day comes to the night's brightness over seconds, not at once.
void testAutoExposureBringsDayAndNightTogether(int& failures) {
    std::printf("A courtyard by day and by night, exposed as lit and adapted:\n");
    // The sun at day or night, its light posed from it.
    const auto sunAt = [](Scenery& s, bool day) {
        s.scene.environment().sky.sunElevation = day ? 40.0f : -30.0f;
        const glm::vec3 toSun = s.scene.environment().sunDirection();
        s.scene.forEach<Light>([&](EntityId id, Light& light) {
            if (light.type != LightType::Directional) return;
            s.scene.get<Transform>(id).rotation = glm::quatLookAt(-toSun, glm::vec3(0.0f, 1.0f, 0.0f));
        });
    };
    const auto courtyard = [&](Scenery& s, bool day) {
        base(s);
        const MaterialHandle stone = material(s, "golden:court_stone", {0.6f, 0.58f, 0.55f}, 0.8f, 0.0f);
        place(s, s.cube, stone, {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f});
        place(s, s.cube, stone, {0.0f, 1.5f, -3.0f}, {8.0f, 3.0f, 0.5f});
        place(s, s.cube, stone, {0.0f, 0.5f, 0.0f}, glm::vec3(1.0f));
        addLamp(s, {1.5f, 2.0f, 1.0f}, 3.0f, 8.0f, true);
        sunAndCamera(s, {0.0f, 2.5f, 6.0f}, {0.0f, 0.5f, 0.0f});
        sunAt(s, day);
    };
    // A frame's mean luma, 0..255.
    const auto meanLuma = [](const std::vector<unsigned char>& frame) {
        double sum = 0.0;
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                const glm::ivec3 c = pixel(frame, x, y);
                sum += 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z;
            }
        }
        return static_cast<float>(sum / (static_cast<double>(WIDTH) * HEIGHT));
    };
    const auto settingsFor = [](bool adapted) {
        RenderSettings settings;
        settings.msaaSamples  = 1;
        settings.autoExposure = adapted;
        return settings;
    };
    const auto exposed = [&](bool day, bool adapted) {
        const auto build = [&](Scenery& s) { courtyard(s, day); };
        const std::vector<unsigned char> seen = freshFrame(build, settingsFor(adapted), failures, 2 * FRAMES);
        return seen.empty() ? -1.0f : meanLuma(seen);
    };
    const float dayLit       = exposed(true, false);
    const float nightLit     = exposed(false, false);
    const float dayAdapted   = exposed(true, true);
    const float nightAdapted = exposed(false, true);
    std::printf(
        "      as lit day %.1f, night %.1f; adapted day %.1f, night %.1f\n",
        dayLit,
        nightLit,
        dayAdapted,
        nightAdapted
    );
    expect("as lit, the day is many times the brighter", dayLit > 4.0f * nightLit, failures);
    const float dimmer = std::max(std::min(dayAdapted, nightAdapted), 1.0f);
    const float apart  = std::max(dayAdapted, nightAdapted) / dimmer;
    expect("  adapted, the two land near one brightness", apart < 2.0f, failures);
    expect("  and the night is brought up where it was dark", nightAdapted > 2.0f * nightLit, failures);

    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    courtyard(s, true);
    render(s, backend, nullptr, settingsFor(true), nullptr, 2 * FRAMES);
    sunAt(s, false);
    const float fallen  = meanLuma(render(s, backend, nullptr, settingsFor(true), nullptr, 2));
    const float settled = meanLuma(render(s, backend, nullptr, settingsFor(true), nullptr, 60));
    std::printf("      night falling on the day's eye %.1f, seconds on %.1f\n", fallen, settled);
    expect("night on a view adapted to the day is dark at first", fallen < 0.6f * nightAdapted, failures);
    const bool settledNear = std::abs(settled - nightAdapted) < 0.15f * nightAdapted;
    expect("  and comes to the night's brightness in seconds", settledNear, failures);
}

// A box on a floor through an orthographic camera, in the AO view: the crease where they meet is
// occluded. An orthographic camera's depth is linear, so read as the viewmodel's band, the nearest
// VIEWMODEL_DEPTH_SHARE of its range would be taken for sky and occlude nothing.
void testAnOrthographicViewIsOccluded(int& failures) {
    std::printf("Ambient occlusion through an orthographic camera:\n");
    const auto build = [](Scenery& s) {
        base(s);
        const MaterialHandle grey = material(s, "golden:grey", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f);
        place(s, s.cube, grey, {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f});
        place(s, s.cube, grey, {0.0f, 0.5f, 0.0f}, glm::vec3(1.0f));
        sunAndCamera(s, {0.0f, 3.0f, 5.0f}, {0.0f, 0.0f, 0.0f});
        orthographic(s, 2.0f);
    };

    RenderSettings settings;
    settings.renderMode    = RenderMode::AmbientOcclusion;
    settings.msaaSamples   = 1;
    settings.gtaoIntensity = 3.0f;
    const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
    if (frame.empty()) return;
    int darkest = 255;
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) darkest = std::min(darkest, pixel(frame, x, y).x);
    }
    std::printf("      darkest %d\n", darkest);
    expect("an orthographic view finds the crease", darkest < 200, failures);
}

// A reflection probe baked while the frame shows a debug view: the forward pass and the capture
// draw with one PBR program, so the capture puts back what the pass left set on it, or the probe
// would hold the albedo view for good.
void testAProbeBakedUnderADebugViewHoldsTheScene(int& failures) {
    std::printf("A probe baked while a debug view shows:\n");
    RenderSettings albedo;
    albedo.renderMode = RenderMode::Albedo;

    restoreContextDefaults();
    WindowManager window;
    GLBackend viewed;
    if (!viewed.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery debugged;
    buildProbe(debugged);
    render(debugged, viewed, nullptr, albedo);
    const std::vector<unsigned char> seen = render(debugged, viewed);

    const std::vector<unsigned char> want = freshFrame(buildProbe, RenderSettings{}, failures);
    if (want.empty()) return;

    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels differ\n", share * 100.0f);
    expect("a probe baked under a debug view holds the scene", share <= PIXEL_TOLERANCE, failures);
}

// The share of a frame's pixels darker than @p below in its first channel, 0..1.
float shareDarker(const std::vector<unsigned char>& frame, int below) {
    int dark = 0;
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) dark += pixel(frame, x, y).x < below ? 1 : 0;
    }
    return static_cast<float>(dark) / static_cast<float>(WIDTH * HEIGHT);
}

// Screen-space shadows over an open floor at a grazing angle, the sun high and low, and through
// an orthographic camera: nothing casts on it, so nothing is shadowed. A flat surface's own depth
// steps, read as occluders, would stripe it.
void testAnOpenFloorCastsNoScreenShadow(int& failures) {
    std::printf("Screen-space shadows over an open floor:\n");
    struct Case {
        float elevation;
        bool  orthographic;
    };
    RenderSettings settings;
    settings.renderMode  = RenderMode::ScreenShadows;
    settings.msaaSamples = 1;
    for (const Case c : {Case{50.0f, false}, Case{12.0f, false}, Case{12.0f, true}}) {
        const auto build = [c](Scenery& s) {
            s.scene.environment().sky.procedural   = false;
            s.scene.environment().sky.sunElevation = c.elevation;
            s.scene.environment().sky.sunAzimuth   = 30.0f;
            buildOpenFloor(s);
            if (c.orthographic) orthographic(s, 3.0f);
        };
        const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
        if (frame.empty()) return;
        const float dark = shareDarker(frame, 250);
        std::printf(
            "      sun at %.0f degrees%s: %.3f%% of the frame shadowed\n",
            c.elevation,
            c.orthographic ? ", orthographic" : "",
            100.0f * dark
        );
        expect("an open floor is not shadowed", dark == 0.0f, failures);
    }
}

// A 6 cm pebble on a floor, under a low sun standing toward +X and no sky; the eye is the caller's.
void buildPebble(Scenery& s) {
    base(s);
    s.scene.environment().sky.procedural   = false;
    s.scene.environment().sky.sunElevation = 20.0f;
    s.scene.environment().sky.sunAzimuth   = 90.0f;
    const MaterialHandle open   = material(s, "golden:open", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f);
    const MaterialHandle pebble = material(s, "golden:pebble", {0.4f, 0.4f, 0.4f}, 0.8f, 0.0f);
    place(s, s.cube, open, {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f});
    place(s, s.cube, pebble, {0.0f, 0.03f, 0.0f}, glm::vec3(0.06f));
}

// A post 12 m off under a low sun, its shadow running metres across the frame, through the
// screen-shadow view: the pass grounds the post at its foot and leaves the rest of the shadow to
// the cascades. A reach fixed in pixels would trace a far shadow metres long, and a thin thing's
// as a slab beside it.
void testAFarPostIsGroundedNotTraced(int& failures) {
    std::printf("Screen-space shadows from a far post:\n");
    const auto build = [](Scenery& s) {
        base(s);
        s.scene.environment().sky.sunElevation = 20.0f;
        s.scene.environment().sky.sunAzimuth   = 90.0f;
        const MaterialHandle open = material(s, "golden:open", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f);
        const MaterialHandle post = material(s, "golden:post", {0.4f, 0.42f, 0.48f}, 0.6f, 0.0f);
        place(s, s.cube, open, {0.0f, -0.5f, 0.0f}, {60.0f, 1.0f, 60.0f});
        place(s, s.cube, post, {0.0f, 1.0f, 0.0f}, {0.15f, 2.0f, 0.15f});
        sunAndCamera(s, {0.0f, 1.7f, 12.0f}, {0.0f, 0.5f, 0.0f});
    };
    RenderSettings settings;
    settings.renderMode = RenderMode::ScreenShadows;
    const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
    if (frame.empty()) return;

    // The sun stands toward +X, the camera's right, so the shadow runs left from the post's foot.
    int reach = 0;
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH / 2; ++x) {
            if (pixel(frame, x, y).x < 128) reach = std::max(reach, WIDTH / 2 - x);
        }
    }
    std::printf("      screen shadow %d px left of the post's middle\n", reach);
    expect("the post is grounded", reach > 0, failures);
    expect("  and its shadow not traced metres out", reach < WIDTH / 20, failures);
}

// A pebble on the floor under a low sun: a shadow a few centimetres long, which the screen-space
// pass finds whether GTAO, whose target it shares, ran or not, and through an orthographic camera;
// with the pass off, none.
void testAPebbleCastsAScreenShadow(int& failures) {
    std::printf("Screen-space shadows from a pebble:\n");
    struct Case {
        bool        gtao;
        bool        screenShadows;
        bool        orthographic;
        const char* what;
    };
    const Case cases[] = {
        {true,  true,  false, "a pebble casts a screen-space shadow, away from the sun"},
        {false, true,  false, "  and does with GTAO off"},
        {true,  true,  true,  "  and through an orthographic camera"},
        {true,  false, false, "  and casts none with the pass off"},
    };
    for (const Case& c : cases) {
        const auto build = [&c](Scenery& s) {
            buildPebble(s);
            sunAndCamera(s, {0.0f, 0.5f, 0.6f}, {0.0f, 0.0f, 0.0f});
            if (c.orthographic) orthographic(s, 0.2f);
        };

        RenderSettings settings;
        settings.renderMode    = RenderMode::ScreenShadows;
        settings.msaaSamples   = 1;
        settings.gtao          = c.gtao;
        settings.screenShadows = c.screenShadows;
        const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
        if (frame.empty()) return;
        const float dark = shareDarker(frame, 128);
        // The sun stands toward +X, the camera's right, so the shadow falls to the pebble's left.
        int left  = 0;
        int right = 0;
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                if (pixel(frame, x, y).x < 128) ++(x < WIDTH / 2 ? left : right);
            }
        }
        std::printf("      %.2f%% of the frame in shadow: %d px left, %d right\n", 100 * dark, left, right);
        const bool away = left > 4 * right;
        expect(c.what, c.screenShadows ? dark > 0.001f && away : dark == 0.0f, failures);
    }
}

// The pebble with the eye facing a low sun that stands inside the frame: the pass then marches
// toward the sun's point on the screen from every side of it, and the shadow falls toward the eye,
// below the pebble in the frame, never above it toward the sun.
void testAScreenShadowFacingTheSunFallsTowardTheEye(int& failures) {
    std::printf("Screen-space shadows facing the sun:\n");
    const auto build = [](Scenery& s) {
        buildPebble(s);
        // Toward +X, as the sun stands, close and low enough that the sun is in the frame: the
        // pebble fills the rows within 10 of the middle, its shadow those 10 to 21 below.
        sunAndCamera(s, {-0.5f, 0.07f, 0.0f}, {0.0f, 0.03f, 0.0f});
    };

    RenderSettings settings;
    settings.renderMode  = RenderMode::ScreenShadows;
    settings.msaaSamples = 1;
    const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
    if (frame.empty()) return;
    // Rows clear of the pebble itself, which faces the eye from its own shade.
    int towardEye = 0;
    int towardSun = 0;
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            // Half lit or darker: a contact shadow sits about 128, which devices round either way.
            if (pixel(frame, x, y).x >= 160) continue;
            if (y > HEIGHT / 2 + 12) ++towardEye;
            if (y < HEIGHT / 2 - 12) ++towardSun;
        }
    }
    std::printf("      %d px of shadow toward the eye, %d toward the sun\n", towardEye, towardSun);
    // A pebble's shadow covers far less than a hundredth of the frame.
    const bool toward = towardEye > 4 && towardEye > 4 * towardSun && towardEye < WIDTH * HEIGHT / 100;
    expect("facing the sun, the shadow falls toward the eye", toward, failures);
}

// A smoke puff's normal map stores x and y, its z rebuilt where read (TextureUsage::Normal): a flat
// one faces the eye, shading the puff as the middle of the ball it would be without one.
void testAFlatNormalMapFacesThePuffToTheEye(int& failures) {
    std::printf("A puff with a flat normal map:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    std::vector<unsigned char> frames[2];
    for (const bool mapped : {false, true}) {
        const auto build = [mapped](Scenery& s) {
            base(s);
            ParticleEmitter puff;
            puff.blend = ParticleBlend::Alpha;
            holdLook(puff, {0.8f, 0.8f, 0.8f, 1.0f});
            if (mapped) {
                constexpr uint32_t SIZE = 8;
                TextureAsset flat;
                flat.params.width          = SIZE;
                flat.params.height         = SIZE;
                flat.params.internalFormat = TextureInternalFormat::RG8;
                flat.params.format         = inferFormat(2);
                flat.pixelData.assign(SIZE * SIZE * 2, 128);
                puff.normalMap = s.resources.add(std::move(flat), "golden:flat");
            }
            Particle p;
            p.position = {0.0f, 1.2f, 0.0f};
            p.size     = 0.6f;
            addEmitter(s, puff).push_back(p);
            // The sun behind the eye: a puff facing it is lit full, one turned aside half.
            const glm::vec3 toSun = s.scene.environment().sunDirection();
            sunAndCamera(s, p.position + 4.0f * toSun, p.position);
        };
        frames[mapped ? 1 : 0] = freshFrame(build, settings, failures);
        if (frames[mapped ? 1 : 0].empty()) return;
    }
    const glm::ivec3 ball = pixel(frames[0], WIDTH / 2, HEIGHT / 2);
    const glm::ivec3 flat = pixel(frames[1], WIDTH / 2, HEIGHT / 2);
    std::printf(
        "      the ball's middle (%d %d %d), the flat map's (%d %d %d)\n",
        ball.x,
        ball.y,
        ball.z,
        flat.x,
        flat.y,
        flat.z
    );
    const bool alike = glm::all(glm::lessThanEqual(glm::abs(flat - ball), glm::ivec3(3)));
    expect("a flat normal map shades the puff as the ball's middle", alike, failures);
}

// A particle whose sprite is still loading is not drawn, where the missing-texture checker would
// show for a frame; one whose sprite settled with no pixels shows the checker.
void testAParticleWaitsForItsSprite(int& failures) {
    std::printf("A puff whose sprite has no pixels:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    enum class Sprite { None, Loading, Failed };
    std::vector<unsigned char> frames[3];
    for (const Sprite sprite : {Sprite::None, Sprite::Loading, Sprite::Failed}) {
        const auto build = [sprite](Scenery& s) {
            base(s);
            if (sprite != Sprite::None) {
                TextureAsset empty;
                empty.params.width  = 8;
                empty.params.height = 8;
                empty.loading       = sprite == Sprite::Loading;
                ParticleEmitter puff;
                puff.blend   = ParticleBlend::Alpha;
                puff.texture = s.resources.add(std::move(empty), "golden:empty");
                holdLook(puff, glm::vec4(1.0f));
                Particle p;
                p.position = {0.0f, 1.2f, 0.0f};
                p.size     = 0.6f;
                addEmitter(s, puff).push_back(p);
            }
            sunAndCamera(s, {0.0f, 1.2f, 4.0f}, {0.0f, 1.2f, 0.0f});
        };
        frames[static_cast<int>(sprite)] = freshFrame(build, settings, failures);
        if (frames[static_cast<int>(sprite)].empty()) return;
    }
    const float loading = differingShare(frames[1], frames[0].data());
    const float failed  = differingShare(frames[2], frames[0].data());
    std::printf(
        "      %.2f%% of pixels differ while it loads, %.2f%% once it failed\n",
        loading * 100.0f,
        failed * 100.0f
    );
    expect("a puff whose sprite is loading is not drawn", loading <= PIXEL_TOLERANCE, failures);
    expect("  and one whose sprite failed shows the checker", failed > PIXEL_TOLERANCE, failures);
}

// A layered material's preview is drawn as the frame draws it, through the layered program: a ball
// all of a blue layer previews blue, not as the material's own white.
void testALayeredMaterialPreviewsItsLayers(int& failures) {
    std::printf("A layered material's preview:\n");
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    ResourceManager resources;
    MaterialAsset blue;
    blue.albedo = glm::vec4(0.1f, 0.2f, 0.9f, 1.0f);
    TextureAsset weights;
    weights.params.width  = 1;
    weights.params.height = 1;
    weights.pixelData     = {255, 0, 0, 0};
    MaterialAsset layered;
    layered.layers       = {resources.add(std::move(blue), "golden:preview_layer")};
    layered.layerWeights = resources.add(std::move(weights), "golden:preview_weights");
    PreviewRequest request;
    request.key      = 11;
    request.width    = 64;
    request.height   = 64;
    const MeshHandle ball = resources.add(generateSphere(), "golden:preview_ball");
    request.draws    = {{ball, resources.add(std::move(layered), "golden:preview_layered")}};
    const GpuTextureId id = backend.editorHooks()->renderPreview(request, resources);
    std::vector<unsigned char> pixels(64 * 64 * 4, 0);
    if (id != 0) {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }
    const unsigned char* middle = &pixels[(32 * 64 + 32) * 4];
    std::printf("      the ball's middle is %d %d %d\n", middle[0], middle[1], middle[2]);
    expect("the ball previews in its layer's blue", middle[2] > middle[0] + 40, failures);
}

// A layered material blends its layers by its weights' channels at the mesh's uv: a slab weighted
// all to its first layer at one end and its second at the other shows the first's color there,
// from its map, which reaches the shader through an array, and the second's at the other.
void testALayeredMaterialBlendsItsLayers(int& failures) {
    std::printf("A slab of two layered materials:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto build = [](Scenery& s) {
        base(s);
        TextureAsset red;
        red.params.width  = 4;
        red.params.height = 4;
        red.pixelData.resize(4 * 4 * 4);
        for (size_t i = 0; i < red.pixelData.size(); i += 4) {
            red.pixelData[i]     = 220;
            red.pixelData[i + 1] = 30;
            red.pixelData[i + 2] = 30;
            red.pixelData[i + 3] = 255;
        }
        MaterialAsset first;
        first.albedoTexture = s.resources.add(std::move(red), "golden:red");
        first.roughness     = 0.9f;
        const MaterialHandle mapped = s.resources.add(std::move(first), "golden:layer_red");
        const MaterialHandle flat   = material(s, "golden:layer_blue", {0.1f, 0.2f, 0.9f}, 0.9f, 0.0f);

        // All the first layer in the first texel, all the second in the second.
        TextureAsset weights;
        weights.params.width  = 2;
        weights.params.height = 1;
        weights.pixelData     = {255, 0, 0, 0, 0, 255, 0, 0};
        MaterialAsset layered;
        layered.layers       = {mapped, flat};
        layered.layerWeights = s.resources.add(std::move(weights), "golden:weights");
        const MaterialHandle slab = s.resources.add(std::move(layered), "golden:layered");
        place(s, s.cube, slab, {0.0f, 0.0f, 0.0f}, {4.0f, 0.1f, 4.0f});
        sunAndCamera(s, {0.0f, 6.0f, 0.01f}, {0.0f, 0.0f, 0.0f});
    };
    const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
    if (frame.empty()) return;

    // A quarter of the slab in from either end of its uv's u, whichever way the top face lies.
    const int reach = HEIGHT / 8;
    const glm::ivec3 ends[4] = {
        pixel(frame, WIDTH / 2 - reach, HEIGHT / 2),
        pixel(frame, WIDTH / 2 + reach, HEIGHT / 2),
        pixel(frame, WIDTH / 2, HEIGHT / 2 - reach),
        pixel(frame, WIDTH / 2, HEIGHT / 2 + reach),
    };
    const auto red  = [](glm::ivec3 c) { return c.x > c.z + 60 && c.x > c.y + 60; };
    const auto blue = [](glm::ivec3 c) { return c.z > c.x + 60 && c.z > c.y + 60; };
    bool across = false;
    for (int axis = 0; axis < 2; ++axis) {
        const glm::ivec3 a = ends[2 * axis];
        const glm::ivec3 b = ends[2 * axis + 1];
        std::printf("      (%d %d %d) and (%d %d %d)\n", a.x, a.y, a.z, b.x, b.y, b.z);
        across = across || (red(a) && blue(b)) || (blue(a) && red(b));
    }
    expect("one end shows the first layer's map, the other the second layer", across, failures);
}

// A terrain draws its heights with the layer painted at each vertex.
void testATerrainDrawsItsPaintedLayers(int& failures) {
    std::printf("A terrain painted with two layers:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    const auto build = [](Scenery& s) {
        base(s);
        TerrainAsset terrain;
        terrain.resolution = 33;
        terrain.size       = 8.0f;
        terrain.heights.assign(33 * 33, 0.0f);
        terrain.weights.assign(33 * 33 * 4, 0);
        for (int j = 0; j < 33; ++j) {
            for (int i = 0; i < 33; ++i) {
                // A ridge along Z down the middle, the first layer to its -X side, the second to its +X.
                const size_t at   = static_cast<size_t>(j) * 33 + i;
                terrain.heights[at] = std::max(0.0f, 1.0f - std::abs(static_cast<float>(i) - 16.0f) / 8.0f);
                terrain.weights[at * 4 + (i < 16 ? 0 : 1)] = 255;
            }
        }
        terrain.layers = {
            material(s, "golden:sand", {0.9f, 0.15f, 0.1f}, 0.9f, 0.0f),
            material(s, "golden:grass", {0.1f, 0.2f, 0.9f}, 0.9f, 0.0f)
        };
        const EntityId id = s.scene.createEntity();
        s.scene.add(id, Transform{});
        Terrain component;
        component.terrain = s.resources.add(std::move(terrain), "golden:terrain");
        s.scene.add(id, std::move(component));
        // Down the ridge from above its +Z end: -X lies to the left.
        sunAndCamera(s, {0.0f, 7.0f, 6.0f}, {0.0f, 0.0f, 0.0f});
    };
    const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
    if (frame.empty()) return;

    // Either side of the ridge, halfway out to the terrain's edge.
    const glm::ivec3 left  = pixel(frame, WIDTH / 2 - WIDTH / 8, HEIGHT / 2);
    const glm::ivec3 right = pixel(frame, WIDTH / 2 + WIDTH / 8, HEIGHT / 2);
    std::printf("      (%d %d %d) and (%d %d %d)\n", left.x, left.y, left.z, right.x, right.y, right.z);
    expect("the -X side shows the first layer", left.x > left.z + 60 && left.x > left.y + 60, failures);
    expect("the +X side shows the second", right.z > right.x + 60 && right.z > right.y + 60, failures);
}

// Haze moves the wall behind it and adds no light; a ribbon covers the pixels between its points.
// Behaviour, not pixels: any device judges it, not only the one the goldens were made on.
void testHazeBendsAndARibbonCovers(int& failures) {
    std::printf("Haze and a ribbon before a striped wall:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    enum class Shown { Nothing, Haze, Ribbon };
    std::vector<unsigned char> frames[3];
    for (const Shown shown : {Shown::Nothing, Shown::Haze, Shown::Ribbon}) {
        const auto build = [shown](Scenery& s) {
            base(s);
            addStripedWall(s, 16, -2.25f, 1.2f);
            ParticleEmitter emitter;
            if (shown == Shown::Haze) {
                emitter.blend      = ParticleBlend::Distortion;
                emitter.distortion = 0.12f;
                holdLook(emitter, glm::vec4(1.0f));
                Particle p;
                p.position = {0.0f, 1.2f, 0.0f};
                p.size     = 0.6f;
                addEmitter(s, emitter).push_back(p);
            } else if (shown == Shown::Ribbon) {
                emitter.render = ParticleRender::Ribbon;
                holdLook(emitter, {1.0f, 0.5f, 0.1f, 1.0f}, 4.0f);
                std::vector<Particle>& points = addEmitter(s, emitter);
                for (int i = 0; i < 4; ++i) {
                    Particle p;
                    p.position = {-0.6f + 0.4f * static_cast<float>(i), 1.2f, 0.0f};
                    p.size     = 0.05f;
                    points.push_back(p);
                }
            }
            sunAndCamera(s, {0.0f, 1.2f, 4.0f}, {0.0f, 1.2f, 0.0f});
        };
        frames[static_cast<int>(shown)] = freshFrame(build, settings, failures);
        if (frames[static_cast<int>(shown)].empty()) return;
    }

    // Over the haze's middle: how many pixels moved, and the light there before and after.
    const int reach = HEIGHT / 8;
    int   moved = 0;
    int   total = 0;
    float before = 0.0f;
    float after  = 0.0f;
    for (int y = HEIGHT / 2 - reach; y < HEIGHT / 2 + reach; ++y) {
        for (int x = WIDTH / 2 - reach; x < WIDTH / 2 + reach; ++x) {
            const glm::ivec3 bare = pixel(frames[0], x, y);
            const glm::ivec3 bent = pixel(frames[1], x, y);
            moved  += glm::any(glm::greaterThan(glm::abs(bent - bare), glm::ivec3(12))) ? 1 : 0;
            before += static_cast<float>(bare.x + bare.y + bare.z);
            after  += static_cast<float>(bent.x + bent.y + bent.z);
            ++total;
        }
    }
    const float share = static_cast<float>(moved) / static_cast<float>(total);
    const float light = after / before;
    std::printf("      haze moved %.0f%% of its middle; its light is %.3f of it\n", 100.0f * share, light);
    expect("haze bends the wall behind it", share > 0.05f, failures);
    expect("  and adds no light of its own", std::abs(light - 1.0f) < 0.05f, failures);

    // The ribbon runs across the middle, 1.2 m long: on it at the middle, and off it a sixth of the
    // frame away.
    const glm::ivec3 bare   = pixel(frames[0], WIDTH / 2, HEIGHT / 2);
    const glm::ivec3 lit    = pixel(frames[2], WIDTH / 2, HEIGHT / 2);
    const glm::ivec3 beside = pixel(frames[2], WIDTH / 2, HEIGHT / 2 + HEIGHT / 6);
    const glm::ivec3 under  = pixel(frames[0], WIDTH / 2, HEIGHT / 2 + HEIGHT / 6);
    expect("a ribbon covers the pixels between its points", lit.x > bare.x + 40, failures);
    const bool untouched = glm::all(glm::lessThanEqual(glm::abs(beside - under), glm::ivec3(2)));
    expect("  and not those off it", untouched, failures);
}

// A glossy floor under a white ceiling and a lamp, seen across the room: its reflection of the
// ceiling, met edge-on, is traced as one surface, not as rows of hits and misses that would
// differ from one row of pixels to the next far more than from one column to the next.
void testAGlossyFloorReflectsInNoRows(int& failures) {
    std::printf("A glossy floor reflecting the ceiling across a room:\n");
    const auto build = [](Scenery& s) {
        base(s);
        s.scene.environment().sky.sunElevation = -20.0f;
        const MaterialHandle floor = material(s, "golden:polished", {0.05f, 0.05f, 0.06f}, 0.06f, 0.0f);
        const MaterialHandle white = material(s, "golden:plaster", {0.8f, 0.8f, 0.8f}, 0.9f, 0.0f);
        place(s, s.cube, floor, {0.0f, -0.5f, 0.0f}, {12.0f, 1.0f, 16.0f});
        place(s, s.cube, white, {0.0f, 3.5f, 0.0f}, {12.0f, 1.0f, 16.0f});
        place(s, s.cube, white, {-6.5f, 1.5f, 0.0f}, {1.0f, 3.0f, 16.0f});
        place(s, s.cube, white, {6.5f, 1.5f, 0.0f}, {1.0f, 3.0f, 16.0f});
        place(s, s.cube, white, {0.0f, 1.5f, -8.5f}, {12.0f, 3.0f, 1.0f});
        addLamp(s, {0.0f, 2.0f, -2.0f}, 20.0f, 12.0f, false);
        sunAndCamera(s, {0.0f, 1.6f, 6.0f}, {0.0f, 0.9f, -3.0f});
    };
    const std::vector<unsigned char> frame = freshFrame(build, RenderSettings{}, failures);
    if (frame.empty()) return;

    // The floor in front of the camera: the lower three tenths of the frame, its middle three fifths.
    const auto grey = [&](int x, int y) {
        const glm::ivec3 c = pixel(frame, x, y);
        return static_cast<float>(c.x + c.y + c.z) / 3.0f;
    };
    double acrossRows = 0.0;
    double acrossColumns = 0.0;
    for (int y = HEIGHT * 7 / 10; y < HEIGHT - 2; ++y) {
        for (int x = WIDTH / 5; x < WIDTH * 4 / 5; ++x) {
            acrossRows    += std::abs(grey(x, y + 1) - grey(x, y));
            acrossColumns += std::abs(grey(x + 1, y) - grey(x, y));
        }
    }
    const double ratio = acrossRows / std::max(acrossColumns, 1.0);
    std::printf("      row to row %.2f times column to column\n", ratio);
    expect("the floor's reflection runs in no rows", ratio < 3.0, failures);
}

// A puff of smoke at the centre of the frame under an overhead sun, with a roof over it and
// without: an alpha-blended particle scatters the light reaching it, so the roof's shadow darkens
// it; an additive one emits, so the roof leaves it as it was.
void testSmokeIsShadowedAndSparksAreNot(int& failures) {
    std::printf("Particles under a roof:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    float centre[2][2] = {};
    for (const bool additive : {false, true}) {
        for (const bool roof : {false, true}) {
            const auto build = [additive, roof](Scenery& s) {
                base(s);
                s.scene.environment().sky.sunElevation = 89.0f;
                place(
                    s,
                    s.cube,
                    material(s, "golden:floor", {0.3f, 0.3f, 0.3f}, 0.8f, 0.0f),
                    {0.0f, -0.5f, 0.0f},
                    {40.0f, 1.0f, 40.0f}
                );
                if (roof) {
                    place(
                        s,
                        s.cube,
                        material(s, "golden:roof", {0.3f, 0.3f, 0.3f}, 0.8f, 0.0f),
                        {0.0f, 6.0f, 0.0f},
                        {30.0f, 0.5f, 30.0f}
                    );
                }
                ParticleEmitter emitter;
                emitter.blend    = additive ? ParticleBlend::Additive : ParticleBlend::Alpha;
                emitter.softness = 0.2f;
                holdLook(emitter, {0.6f, 0.6f, 0.6f, 1.0f});
                Particle p;
                p.position = {0.0f, 1.0f, 0.0f};
                p.size     = 0.5f;
                addEmitter(s, std::move(emitter)).push_back(p);
                sunAndCamera(s, {0.0f, 1.0f, 3.0f}, {0.0f, 1.0f, 0.0f});
            };
            const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
            if (frame.empty()) return;
            centre[additive ? 1 : 0][roof ? 1 : 0] = centreGrey(frame);
        }
    }
    std::printf(
        "      smoke %.0f in the sun, %.0f under the roof; sparks %.0f and %.0f\n",
        centre[0][0],
        centre[0][1],
        centre[1][0],
        centre[1][1]
    );
    expect("a puff of smoke under a roof is darker", centre[0][1] < 0.6f * centre[0][0], failures);
    expect("  and sparks are not", std::abs(centre[1][1] - centre[1][0]) <= 2.0f, failures);
}

// A grey decal on a floor of the same grey, at night under a lamp: lit as the floor under it is,
// it is all but invisible. Lit by the sun and the environment alone, it would be a dark patch.
void testADecalIsLitAsTheFloorUnderIt(int& failures) {
    std::printf("A decal under a lamp:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    float centre[2] = {};
    for (const bool painted : {false, true}) {
        const auto build = [painted](Scenery& s) {
            base(s);
            s.scene.environment().sky.procedural = false;
            place(
                s,
                s.cube,
                material(s, "golden:floor", {0.5f, 0.5f, 0.5f}, 1.0f, 0.0f),
                {0.0f, -0.5f, 0.0f},
                {20.0f, 1.0f, 20.0f}
            );
            if (painted) {
                MaterialAsset grey;
                grey.type   = MaterialType::Transparent;
                grey.albedo = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
                const EntityId id = s.scene.createEntity();
                Transform pose;
                pose.rotation = glm::quatLookAt(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
                pose.scale    = {1.0f, 1.0f, 1.0f};
                s.scene.add(id, std::move(pose));
                Decal projector;
                projector.material = s.resources.add(std::move(grey), "golden:grey");
                s.scene.add(id, std::move(projector));
            }
            addLamp(s, {0.0f, 1.5f, 0.0f}, 6.0f, 6.0f, true);
            camera(s, {0.0f, 4.0f, 3.0f}, {0.0f, 0.0f, 0.0f});
        };
        const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
        if (frame.empty()) return;
        centre[painted ? 1 : 0] = centreGrey(frame);
    }
    std::printf("      the floor %.0f, the decal on it %.0f\n", centre[0], centre[1]);
    expect(
        "a decal under a lamp is lit as the floor under it",
        centre[1] > 0.85f * centre[0] && centre[1] < 1.15f * centre[0],
        failures
    );
}

// A puff of smoke at night beside a lamp whose reach ends short of the puff's centre but takes in
// a corner of its quad: the lamp lights none of it. Lit at a corner instead, it would take a lamp's
// light at a point it does not fill, a light turning with the view and the puff's spin.
void testSmokeIsLitWhereItIs(int& failures) {
    std::printf("A puff of smoke just past a lamp's reach:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    float centre[2] = {};
    for (const bool lamp : {false, true}) {
        const auto build = [lamp](Scenery& s) {
            base(s);
            s.scene.environment().sky.sunElevation = -30.0f;
            const glm::vec3 at(0.0f, 2.0f, 0.0f);
            if (lamp) {
                // 1.41 m from the centre, 0.42 m from the quad's upper right corner (the camera's
                // right is +X).
                addLamp(s, at + glm::vec3(1.0f, 1.0f, 0.0f), 20.0f, 1.1f, false);
            }
            ParticleEmitter emitter;
            emitter.blend = ParticleBlend::Alpha;
            holdLook(emitter, {0.6f, 0.6f, 0.6f, 1.0f});
            Particle p;
            p.position = at;
            p.size     = 0.7f;
            addEmitter(s, std::move(emitter)).push_back(p);
            sunAndCamera(s, at + glm::vec3(0.0f, 0.0f, 3.0f), at);
        };
        const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
        if (frame.empty()) return;
        centre[lamp ? 1 : 0] = centreGrey(frame);
    }
    std::printf("      %.0f without the lamp, %.0f with it\n", centre[0], centre[1]);
    expect("a lamp that does not reach a puff lights none of it", centre[1] <= centre[0] + 2.0f, failures);
}

// An alpha ribbon at night under a lamp above it, under that lamp and its twin below, and under one
// lamp above twice as bright: a strip is lit as a card, from every way alike, so the twin adds as
// much light as doubling the one.
void testTwoLampsLightARibbonAsOneTwiceAsBright(int& failures) {
    std::printf("An alpha ribbon under one lamp and under two:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    enum class Lit { One, Two, Doubled };
    float centre[3] = {};
    for (const Lit lit : {Lit::One, Lit::Two, Lit::Doubled}) {
        const auto build = [lit](Scenery& s) {
            base(s);
            s.scene.environment().sky.sunElevation = -30.0f;
            const glm::vec3 at(0.0f, 2.0f, 0.0f);
            const glm::vec3 above(0.0f, 1.0f, 0.0f);
            addLamp(s, at + above, lit == Lit::Doubled ? 20.0f : 10.0f, 4.0f, false);
            if (lit == Lit::Two) addLamp(s, at - above, 10.0f, 4.0f, false);
            ParticleEmitter emitter;
            emitter.blend  = ParticleBlend::Alpha;
            emitter.render = ParticleRender::Ribbon;
            holdLook(emitter, {0.5f, 0.5f, 0.5f, 1.0f});
            std::vector<Particle>& points = addEmitter(s, std::move(emitter));
            for (int i = 0; i < 4; ++i) {
                Particle p;
                p.position = at + glm::vec3(-0.6f + 0.4f * static_cast<float>(i), 0.0f, 0.0f);
                p.size     = 0.2f;
                points.push_back(p);
            }
            sunAndCamera(s, at + glm::vec3(0.0f, 0.0f, 3.0f), at);
        };
        const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
        if (frame.empty()) return;
        centre[static_cast<int>(lit)] = centreGrey(frame);
    }
    std::printf(
        "      %.0f under one lamp, %.0f under two, %.0f under one twice as bright\n",
        centre[0],
        centre[1],
        centre[2]
    );
    expect("a second lamp lights it more", centre[1] > centre[0] + 8.0f, failures);
    expect("  as much as doubling the first", std::abs(centre[1] - centre[2]) < 3.0f, failures);
}

// Velvet and brushed metal under a sun 25 degrees up: the velvet's sheen glows at its rim, lit and in the
// sky's light alike, over a base it dims by as much; the metal's highlight and its reflection of
// the sky stretch round the can, its rougher way.
void buildCloth(Scenery& s) {
    base(s);
    s.scene.environment().sky.sunElevation = 25.0f;
    place(
        s,
        s.cube,
        material(s, "golden:floor", {0.3f, 0.3f, 0.3f}, 0.7f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {30.0f, 1.0f, 30.0f}
    );
    const glm::vec3 sun    = s.scene.environment().sunDirection();
    const glm::vec3 eye    = {0.0f, 1.6f, 0.0f};
    const glm::vec3 toward = -glm::normalize(glm::vec3(sun.x, 0.0f, sun.z));
    const glm::vec3 across = glm::cross(toward, glm::vec3(0.0f, 1.0f, 0.0f));

    MaterialAsset velvet;
    velvet.albedo         = glm::vec4(0.25f, 0.02f, 0.05f, 1.0f);
    velvet.roughness      = 0.9f;
    velvet.sheenColor     = {0.9f, 0.45f, 0.55f};
    velvet.sheenRoughness = 0.4f;
    MaterialAsset brushed;
    brushed.albedo     = glm::vec4(0.9f, 0.9f, 0.92f, 1.0f);
    brushed.roughness  = 0.25f;
    brushed.metallic   = 1.0f;
    brushed.anisotropy = 0.8f;
    const MaterialHandle cloth = s.resources.add(std::move(velvet), "golden:velvet");
    const MaterialHandle metal = s.resources.add(std::move(brushed), "golden:brushed");
    // The metal a cylinder turned on a lathe: its tangents run round it, as no sphere's can.
    const MeshHandle can = s.resources.add(generateCylinder(0.5f, 1.0f, 48), "golden:can");
    const glm::vec3 left  = eye + toward * 3.2f - across * 1.05f;
    const glm::vec3 right = eye + toward * 3.2f + across * 1.05f;
    place(s, s.sphere, cloth, {left.x, 0.9f, left.z}, glm::vec3(1.8f));
    place(s, can, metal, {right.x, 0.9f, right.z}, glm::vec3(1.6f, 1.8f, 1.6f));
    sunAndCamera(s, eye, eye + toward * 3.2f + glm::vec3(0.0f, -0.7f, 0.0f));
}

// A white sphere with a white sheen and one without, under the sun and the flat ambient of a scene
// with no sky: the sheen takes from the base what it reflects (KHR_materials_sheen's albedo
// scaling), so it moves the light round the sphere, toward its rim, without adding to it.
void testASheenAddsNoLight(int& failures) {
    std::printf("A white sheen over a white base:\n");
    RenderSettings settings;
    settings.msaaSamples = 1;
    settings.tonemap     = Tonemap::Reinhard;
    settings.exposure    = -2.0f;
    float mean[2] = {};
    for (const bool sheen : {false, true}) {
        const auto build = [sheen](Scenery& s) {
            base(s);
            s.scene.environment().sky.procedural = false;
            MaterialAsset white;
            white.albedo    = glm::vec4(1.0f);
            white.roughness = 1.0f;
            if (sheen) {
                white.sheenColor     = glm::vec3(1.0f);
                white.sheenRoughness = 0.5f;
            }
            const MaterialHandle paint = s.resources.add(std::move(white), "golden:white");
            place(s, s.sphere, paint, {0.0f, 0.0f, 0.0f}, glm::vec3(2.0f));
            sunAndCamera(s, s.scene.environment().sunDirection() * 4.0f, {0.0f, 0.0f, 0.0f});
        };
        const std::vector<unsigned char> frame = freshFrame(build, settings, failures);
        if (frame.empty()) return;
        long sum = 0;
        for (int y = HEIGHT / 4; y < HEIGHT * 3 / 4; ++y) {
            for (int x = WIDTH / 2 - HEIGHT / 4; x < WIDTH / 2 + HEIGHT / 4; ++x) sum += pixel(frame, x, y).x;
        }
        mean[sheen ? 1 : 0] = static_cast<float>(sum) / static_cast<float>((HEIGHT / 2) * (HEIGHT / 2));
    }
    std::printf("      %.1f bare, %.1f with the sheen\n", mean[0], mean[1]);
    // A few percent is the ambient light the base's own Fresnel had kept from it.
    expect("a white sheen adds no light to a white base", mean[1] <= mean[0] * 1.05f, failures);
}

// A wall standing on a yard, for debug drawings to pass behind and before.
void buildDebugWall(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:yard", {0.35f, 0.35f, 0.33f}, 0.9f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );
    place(
        s,
        s.cube,
        material(s, "golden:screen", {0.5f, 0.48f, 0.45f}, 0.8f, 0.0f),
        {0.0f, 1.0f, 0.0f},
        {3.0f, 2.0f, 0.4f}
    );
    sunAndCamera(s, {0.0f, 2.5f, 7.0f}, {0.0f, 1.0f, 0.0f});
}

// A line behind the wall, from x = -4 to 4: the wall hides it within about two metres of x = 0.
struct DebugRow {
    float y;
    float z;

    glm::vec3 at(float x) const { return {x, y, z}; }
};
constexpr DebugRow GHOSTED_ROW{1.0f, -2.0f};
constexpr DebugRow ON_TOP_ROW{1.5f, -2.5f};
constexpr DebugRow SEEN_ONLY_ROW{0.5f, -1.5f};

// What a host draws over the wall: a line behind it in each depth mode, then before it a pulsed
// path lit @p glow for the bloom, a sphere, a turned box and a point.
std::vector<DebugLine> debugDrawings(float glow) {
    DebugDraw draw;
    const DebugStyle red   = DebugStyle(glm::vec3(1.0f, 0.25f, 0.2f)).wide(2.5f);
    const DebugStyle green = DebugStyle(glm::vec3(0.3f, 1.0f, 0.4f)).wide(2.5f).onTop();
    const DebugStyle cyan  = DebugStyle(glm::vec3(0.2f, 0.8f, 1.0f)).wide(2.5f).seenOnly();
    draw.line(GHOSTED_ROW.at(-4.0f), GHOSTED_ROW.at(4.0f), red);
    draw.line(ON_TOP_ROW.at(-4.0f), ON_TOP_ROW.at(4.0f), green);
    draw.line(SEEN_ONLY_ROW.at(-4.0f), SEEN_ONLY_ROW.at(4.0f), cyan);

    DebugPulse pulse;
    pulse.dash    = 0.8f;
    pulse.spacing = 2.0f;
    pulse.speed   = 2.0f;
    pulse.trail   = 0.25f;
    const std::vector<glm::vec3> path = {{-3.0f, 0.3f, 2.0f}, {0.0f, 1.1f, 2.6f}, {3.0f, 0.3f, 2.0f}};
    draw.pulse(path, DebugStyle(glm::vec3(1.0f, 0.85f, 0.2f)).wide(3.0f).glowing(glow), pulse);

    draw.sphere({-2.6f, 0.8f, 1.6f}, 0.6f, glm::vec3(1.0f));
    draw.box(
        {2.6f, 0.5f, 1.6f},
        glm::vec3(0.5f),
        glm::angleAxis(0.6f, glm::vec3(0.0f, 1.0f, 0.0f)),
        DebugStyle(glm::vec3(1.0f, 0.35f, 0.9f)).wide(2.0f)
    );
    draw.point({0.0f, 2.4f, 1.0f}, DebugStyle(glm::vec3(1.0f)).wide(2.0f));
    draw.collect(0.0f, 0.0f, 0);
    return draw.lines();
}

// The wall with its drawings, the pulses three quarters of a second on.
void buildDebugLines(Scenery& s) {
    buildDebugWall(s);
    s.debugLines   = debugDrawings(6.0f);
    s.debugSeconds = 0.75;
}

// A yard of the things the editor shows in the world, drawn as a host draws them - the cull,
// then DebugViewSystem with every category but the bounds on - with a crate in focus.
void buildGizmos(Scenery& s) {
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:yard", {0.35f, 0.35f, 0.33f}, 0.9f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {20.0f, 1.0f, 20.0f}
    );
    const MaterialHandle wood = material(s, "golden:crate", {0.55f, 0.4f, 0.28f}, 0.7f, 0.0f);
    const glm::vec3      up(0.0f, 1.0f, 0.0f);
    const auto entityAt = [&](glm::vec3 at, glm::quat turn = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) {
        const EntityId id = s.scene.createEntity();
        Transform pose;
        pose.position = at;
        pose.rotation = turn;
        s.scene.add(id, std::move(pose));
        return id;
    };
    const auto crate = [&](glm::vec3 at, glm::vec3 size, float turn) {
        const EntityId id = entityAt(at, glm::angleAxis(turn, up));
        s.scene.get<Transform>(id).scale = size;
        Mesh mesh;
        mesh.mesh     = s.cube;
        mesh.material = wood;
        s.scene.add(id, std::move(mesh));
        Collider body;
        body.parts[0].halfExtents = size * 0.5f;
        s.scene.add(id, std::move(body));
        return id;
    };
    crate({-1.8f, 0.5f, 0.2f}, glm::vec3(1.0f), 0.0f);
    const EntityId tower = crate({0.2f, 1.0f, -1.0f}, {1.0f, 2.0f, 1.0f}, 0.5f);
    Collider capsule;
    capsule.parts[0].shape      = ColliderShape::Capsule;
    capsule.parts[0].radius     = 0.35f;
    capsule.parts[0].halfHeight = 0.55f;
    s.scene.add(entityAt({1.8f, 0.9f, 0.4f}), std::move(capsule));

    const auto light = [&](glm::vec3 at, glm::vec3 aim, LightType type, glm::vec3 color, float radius) {
        const EntityId id = entityAt(at, glm::quatLookAt(glm::normalize(aim), up));
        Light lamp;
        lamp.type           = type;
        lamp.color          = color;
        lamp.radius         = radius;
        lamp.intensity      = type == LightType::Directional ? 3.0f : 4.0f;
        lamp.outerConeAngle = 0.45f;
        s.scene.add(id, std::move(lamp));
    };
    const glm::vec3 sunward = -s.scene.environment().sunDirection();
    light({-3.2f, 2.6f, -1.5f}, sunward, LightType::Directional, glm::vec3(1.0f), 10.0f);
    light({-0.4f, 1.9f, 1.4f}, {0.0f, -1.0f, 0.0f}, LightType::Point, {1.0f, 0.75f, 0.45f}, 1.6f);
    light({2.6f, 2.8f, -1.2f}, {-0.8f, -2.8f, 1.6f}, LightType::Spot, {0.6f, 0.8f, 1.0f}, 3.2f);

    Camera other;
    other.fovY   = glm::radians(50.0f);
    other.zNear  = 0.3f;
    other.zFar   = 2.5f;
    other.active = false;
    const glm::quat aimed = glm::quatLookAt(glm::normalize(glm::vec3(-4.4f, -0.8f, -2.6f)), up);
    s.scene.add(entityAt({3.4f, 1.4f, 2.6f}, aimed), other);

    ParticleEmitter spray;
    spray.shape  = EmitShape::Cone;
    spray.radius = 0.25f;
    s.scene.add(entityAt({-2.6f, 0.02f, 2.2f}), std::move(spray));
    AudioSource sound;
    sound.minDistance = 0.4f;
    sound.maxDistance = 1.2f;
    s.scene.add(entityAt({0.9f, 0.4f, 1.9f}), std::move(sound));
    camera(s, {0.6f, 3.4f, 6.8f}, {0.2f, 0.7f, 0.0f});

    Clock          clock;
    EventBus       events;
    WindowManager  window;
    InputMap       input;
    NetSession     net;
    HostChrome     chrome;
    RenderSettings tuning  = s.settings;
    RenderSettings project = s.settings;
    FrameContext   ctx{s.scene, s.resources, clock, events, window, input, net, chrome, tuning, project};
    DebugDraw      draw;
    ctx.debug = &draw;
    for (int category = 0; category < static_cast<int>(DebugCategory::Count); ++category) {
        const DebugCategory kind = static_cast<DebugCategory>(category);
        draw.setShown(kind, kind != DebugCategory::Bounds);
    }
    DebugFocus focus;
    focus.entities = {tower};
    focus.primary  = tower;
    draw.setFocus(focus);
    VisibilitySystem visibility;
    DebugViewSystem  view;
    visibility.update(ctx);
    view.update(ctx);
    draw.collect(0.0f, 0.0f, s.scene.epoch());
    s.debugLines = draw.lines();
}

// Where a world point lands in a frame drawn through @p camera, in pixels from the top left.
glm::vec2 onScreen(const CameraData& camera, glm::vec3 world) {
    const glm::vec4 clip = camera.viewProjection * glm::vec4(world, 1.0f);
    const glm::vec2 ndc  = glm::vec2(clip) / clip.w;
    return glm::vec2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * glm::vec2(WIDTH, HEIGHT);
}

// How far two frames' channels part at most within a pixel of @p at.
int partedNear(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, glm::vec2 at) {
    int most = 0;
    for (int y = static_cast<int>(at.y) - 1; y <= static_cast<int>(at.y) + 1; ++y) {
        for (int x = static_cast<int>(at.x) - 1; x <= static_cast<int>(at.x) + 1; ++x) {
            if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) continue;
            const glm::ivec3 apart = glm::abs(pixel(a, x, y) - pixel(b, x, y));
            most = std::max({most, apart.x, apart.y, apart.z});
        }
    }
    return most;
}

// partedNear along @p row from x @p from to @p to: its least, its most and its mean.
struct Parted {
    int   least = 255;
    int   most  = 0;
    float mean  = 0.0f;
};
Parted partedAlong(
    const std::vector<unsigned char>& a,
    const std::vector<unsigned char>& b,
    const CameraData& camera,
    DebugRow row,
    float from,
    float to
) {
    constexpr int SAMPLES = 25;
    Parted parted;
    for (int i = 0; i < SAMPLES; ++i) {
        const float x     = from + (to - from) * static_cast<float>(i) / static_cast<float>(SAMPLES - 1);
        const int   apart = partedNear(a, b, onScreen(camera, row.at(x)));
        parted.least = std::min(parted.least, apart);
        parted.most  = std::max(parted.most, apart);
        parted.mean += static_cast<float>(apart) / static_cast<float>(SAMPLES);
    }
    return parted;
}

// The wall's frame with @p lines over it, the pulses @p seconds on; @p camera is what it was seen
// through.
std::vector<unsigned char> debugFrame(
    std::vector<DebugLine> lines,
    const RenderSettings& settings,
    CameraData& camera,
    int& failures,
    double seconds = 0.0
) {
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return {};
    }
    Scenery s;
    buildDebugWall(s);
    s.debugLines   = std::move(lines);
    s.debugSeconds = seconds;
    const auto seen = [&](int, const Visibility& visibility) { camera = visibility.camera; };
    return render(s, backend, nullptr, settings, nullptr, FRAMES, seen);
}

// A debug line is the scene's or over it as it asks: full where seen, faint and dotted where the
// wall hides it, full through it on top, gone behind it when seen only.
void testDebugLinesMeetTheSceneAsTheyAsk(int& failures) {
    std::printf("Debug lines behind a wall:\n");
    RenderSettings plain;
    plain.msaaSamples = 1;
    plain.bloom       = false;

    // And one from beside the wall to behind the eye, which only its part in front shows.
    std::vector<DebugLine> lines = debugDrawings(0.0f);
    DebugDraw past;
    past.line({-3.0f, 2.2f, 0.0f}, {-3.0f, 2.2f, 20.0f}, DebugStyle(glm::vec3(1.0f)).wide(2.5f));
    // And a point the wall hides, away from the rows' checks.
    const glm::vec3 hiddenPoint(1.0f, 0.25f, -1.5f);
    past.point(hiddenPoint, DebugStyle(glm::vec3(1.0f)).wide(2.5f));
    past.collect(0.0f, 0.0f, 0);
    lines.insert(lines.end(), past.lines().begin(), past.lines().end());

    CameraData camera;
    const std::vector<unsigned char> bare  = debugFrame({}, plain, camera, failures);
    const std::vector<unsigned char> drawn = debugFrame(lines, plain, camera, failures);
    if (bare.empty() || drawn.empty()) return;

    const Parted ghostSeen = partedAlong(bare, drawn, camera, GHOSTED_ROW, -3.6f, -2.6f);
    const Parted ghostBack = partedAlong(bare, drawn, camera, GHOSTED_ROW, -0.6f, 0.6f);
    const Parted onTopBack = partedAlong(bare, drawn, camera, ON_TOP_ROW, -0.6f, 0.6f);
    const Parted seenOnly  = partedAlong(bare, drawn, camera, SEEN_ONLY_ROW, 2.6f, 3.6f);
    const Parted onlyBack  = partedAlong(bare, drawn, camera, SEEN_ONLY_ROW, -0.6f, 0.6f);
    std::printf(
        "      seen %d..%d, ghosted %d..%d mean %.1f, on top %d.., seen only %d.. and %d behind\n",
        ghostSeen.least,
        ghostSeen.most,
        ghostBack.least,
        ghostBack.most,
        ghostBack.mean,
        onTopBack.least,
        seenOnly.least,
        onlyBack.most
    );
    expect("a line is drawn full where it is seen", ghostSeen.least > 80, failures);
    const bool faint = ghostBack.most > 12 && ghostBack.most < ghostSeen.least / 2;
    expect("  and faintly where the wall hides it", faint, failures);
    expect("  in dots, not a line", ghostBack.least < ghostBack.most / 2, failures);
    expect("a line on top is full behind the wall too", onTopBack.least > 80, failures);
    expect("a line seen only is drawn where seen", seenOnly.least > 80, failures);
    expect("  and not at all where hidden", onlyBack.most <= 3, failures);
    const int pointBack = partedNear(bare, drawn, onScreen(camera, hiddenPoint));
    std::printf("      a hidden point %d\n", pointBack);
    expect("a point the wall hides is a faint dot", pointBack > 12 && pointBack < ghostSeen.least, failures);

    int nearest = 255;
    for (const float z : {0.5f, 1.5f, 2.5f, 3.5f}) {
        nearest = std::min(nearest, partedNear(bare, drawn, onScreen(camera, {-3.0f, 2.2f, z})));
    }
    std::printf("      a line past the eye: %d at least in front of it\n", nearest);
    expect("a line running past the eye is drawn up to it", nearest > 80, failures);
}

// A line that glows lends the frame light where it is seen, which the bloom spreads past its width.
void testADebugLineGlowsIntoTheBloom(int& failures) {
    std::printf("A glowing debug line:\n");
    RenderSettings bloomed;
    bloomed.msaaSamples = 1;

    CameraData camera;
    const std::vector<unsigned char> dull = debugFrame(debugDrawings(0.0f), bloomed, camera, failures, 0.75);
    const std::vector<unsigned char> lit  = debugFrame(debugDrawings(6.0f), bloomed, camera, failures, 0.75);
    if (dull.empty() || lit.empty()) return;

    // Round the path's peak, past the line itself: its halo, and what the bloom spread.
    const glm::vec2 peak = onScreen(camera, {0.0f, 1.1f, 2.6f});

    double halo  = 0.0;
    int    count = 0;
    for (int y = static_cast<int>(peak.y) - 8; y <= static_cast<int>(peak.y) + 8; ++y) {
        for (int x = static_cast<int>(peak.x) - 8; x <= static_cast<int>(peak.x) + 8; ++x) {
            const bool onLine = std::abs(static_cast<float>(y) - peak.y) < 3.0f;
            if (onLine || x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) continue;
            const glm::ivec3 more = pixel(lit, x, y) - pixel(dull, x, y);
            halo += static_cast<double>(more.x + more.y + more.z) / 3.0;
            ++count;
        }
    }
    halo /= std::max(count, 1);
    const glm::ivec3 corner = glm::abs(pixel(lit, 2, 2) - pixel(dull, 2, 2));
    const int        away   = corner.x + corner.y + corner.z;
    std::printf("      %.1f levels brighter round the line, %d in the far corner\n", halo, away);
    expect("its glow spreads round it", halo > 4.0, failures);
    expect("  and not across the frame", away <= 3, failures);
}

void testTheFramesMatchTheirGoldens(GLContext& gl, int& failures, const Shard& shard) {
    std::printf("Whole frames against their goldens:\n");
    if (!gl.attachSurface(WIDTH, HEIGHT)) {
        std::printf("  no pbuffer surface on this device; skipped\n");
        return;
    }

    struct Golden {
        const char* name;
        void (*build)(Scenery&);
    };
    const Golden goldens[] = {
        {"materials",   buildMaterials},
        {"reflections", buildReflections},
        {"glint",       buildGlint},
        {"smoke",       buildSmoke},
        {"sprites",     buildSprites},
        {"viewmodel",   buildViewmodel},
        {"vfx",         buildVfx},
        {"cutout",      buildCutout},
        {"decals",      buildDecals},
        {"fog",         buildFog},
        {"nosky",       buildNoSky},
        {"spot",        buildSpot},
        {"penumbra",    buildPenumbra},
        {"point",       buildPoint},
        {"mirror",      buildMirror},
        {"twosided",    buildTwoSided},
        {"coat",        buildCoat},
        {"cloth",       buildCloth},
        {"sunset",      buildSunset},
        {"room",        buildRoom},
        {"aerial",      buildAerial},
        {"debug",       buildDebugLines},
        {"gizmos",      buildGizmos},
    };
    // The whole frames in the first run; the checks after them dealt out between the runs.
    if (shard.first()) {
        const std::string renderer = gl.renderer();
        for (const Golden& golden : goldens) compare(golden.name, golden.build, renderer, failures);
        for (const Golden& golden : goldens) compare(golden.name, golden.build, renderer, failures, true);
    }

    using Check = void (*)(int&);
    const Check checks[] = {
        testTheUIOverlayLandsOnItsPixels,
        testSoftShadowsOffDrawTheHardPath,
        testAFixedSunsShadowIsBaked,
        testABakeFollowsWhatItsCastersBecome,
        testABakeIsMadeFromTheGivenMeshWhereverTheEyeIs,
        testAReceiverReadsItsBakeOnlyAsItWasBaked,
        testALevelReadsTheBakeOnlyThroughItsGivenMeshsCharts,
        testAReceiverIsBakedAsItsGivenMeshWhateverLevelItDraws,
        testTheLastCascadeDrawsWhatItsLayerHolds,
        testAPondRipplesAndARiverRunsWithNoSea,
        testALightmapBakedOverFramesEndsAsOneBakedWhole,
        testABakedLightIsReadOnlyAsItWasBaked,
        testTheBakedLightsAreTheLowestSlots,
        testAReceiverBelowEveryCasterIsBakedInShadow,
        testAFrameEndsWithTheWindowBound,
        testASoftBakeLeavesOpenGroundLit,
        testASecondFixedSunKeepsItsBakedPenumbra,
        testAHalfScaleSceneFillsTheFrame,
        testTwoWallsACentimetreApartAKilometreOutDoNotFight,
        testHalfResolutionReflectionsAreCloseToFull,
        testAMovedObjectIsDrawnWhereItWent,
        testASpawnAndARemovalAreDrawnAsTheyEnd,
        testAMoverOverFixedScenery,
        testAPlainMaterialDrawsAsTheWholeProgramWould,
        testEveryOptionalFeatureTakesTheWholeProgram,
        testAViewmodelShadesNothingBehindIt,
        testNoDecalLandsOnTheViewmodel,
        testAMirrorShowsWhatIsBehindTheEye,
        testAMirroredCardShowsInAMirror,
        testAMirrorShowsTheShadowTheCascadesHold,
        testAMirrorShowsWhatAFixedSunBaked,
        testAnOrthographicEyesMirrorShowsTheHorizon,
        testAMirrorTurnsAParticleOver,
        testAMirrorHidesTheSkyTheSceneHides,
        testAMirrorKeepsItsPictureAsTheEyeMoves,
        testAWideSpotShadowsItsRim,
        testAPreviewIsNotTheFramesDebugView,
        testABackendDropsWhatTheSceneNoLongerAsksFor,
        testAFrameWithNoCameraKeepsTheProbesBaked,
        testAProbeFollowsItsLight,
        testARefreshedProbeRests,
        testWhatTheCullHidDrawsTheFrameItComesOut,
        testAWeaponHeldIntoAWallIsNotCulled,
        testAFaceFillingTheFrameIsNotCulledByItsOwnDepth,
        testWaterBehindAWallIsCulled,
        testADriftingSunIsFollowedOverFrames,
        testASplashFadesLinearlyOnTheGlass,
        testTheIdentIsLitByItsCometAndTheLoadingScreenFlows,
        testACoveredFramesBakesFindTheShadowUnitsBound,
        testAnOpenFloorIsNotOccluded,
        testAnOpenWallIsOpenToTheFramesEdge,
        testAnOrthographicViewIsOccluded,
        testAnOpenFloorCastsNoScreenShadow,
        testAPebbleCastsAScreenShadow,
        testAFarPostIsGroundedNotTraced,
        testAScreenShadowFacingTheSunFallsTowardTheEye,
        testGTAOReachesAsFarEveryWay,
        testAWallBesideTheEyeIsOpen,
        testAutoExposureBringsDayAndNightTogether,
        testAnOrthographicViewTracesItsReflections,
        testSmokeIsShadowedAndSparksAreNot,
        testHazeBendsAndARibbonCovers,
        testAFlatNormalMapFacesThePuffToTheEye,
        testAParticleWaitsForItsSprite,
        testALayeredMaterialBlendsItsLayers,
        testALayeredMaterialPreviewsItsLayers,
        testATerrainDrawsItsPaintedLayers,
        testAGlossyFloorReflectsInNoRows,
        testADecalIsLitAsTheFloorUnderIt,
        testSmokeIsLitWhereItIs,
        testTwoLampsLightARibbonAsOneTwiceAsBright,
        testAMovedProbeBakesOnceItSettles,
        testASheenAddsNoLight,
        testAProbeBakedUnderADebugViewHoldsTheScene,
        testASealedRoomHoldsNoSkyLight,
        testAnIrradianceVolumeFollowsItsLight,
        testACutShortRelightIsAskedForAgain,
        testAVolumeBakedOverFramesEndsAsOneBakedWhole,
        testReadinessOwesWhatTheFramesBake,
        testAWorldsBakesWaitForItsMeshes,
        testProgramsCompiledInParallelDrawAsWaitedFor,
        testARestPoseSkinsAsTheMeshDraws,
        testAVolumeShowsItsGridUntilItsRelightIsThrough,
        testTheShoreRingsWhatBreaksTheWater,
        testAMirrorShowsTheWaterlineFoam,
        testATransparentPanesShadowIsAsDeepAsItIsOpaque,
        testDebugLinesMeetTheSceneAsTheyAsk,
        testADebugLineGlowsIntoTheBloom
    };
    for (std::size_t i = 0; i < std::size(checks); ++i) {
        if (shard.takes(i)) checks[i](failures);
    }
}

} // namespace

void runGoldenTests(GLContext& gl, int& failures, const Shard& shard) {
    testTheFramesMatchTheirGoldens(gl, failures, shard);
}

} // namespace Vkm::Test

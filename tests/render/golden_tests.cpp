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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "stb_image.h"
#include "stb_image_write.h"

#include "egl_context.h"

#include "gl_backend.h"
#include "frame/gl_occlusion_cull.h"
#include "offline/gl_ibl_baker.h"
#include "offline/gl_irradiance_baker.h"
#include "offline/gl_probe_baker.h"
#include "core/clock.h"
#include "core/event/event_bus.h"
#include "core/host_chrome.h"
#include "core/system.h"
#include "ecs/environment.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/ocean.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/reflection_probe.h"
#include "net/net_session.h"
#include "platform/input/input_map.h"
#include "platform/window/window_manager.h"
#include "resource/resource_manager.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/texture_format.h"
#include "resource/generate/mesh_generators.h"
#include "system/render/render_settings.h"
#include "system/particle/live_particles.h"
#include "system/render/render_view.h"
#include "system/ui/ui_draw_data.h"
#include "system/ocean/ocean_system.h"
#include "system/sky/sky_system.h"
#include "system/visibility/visibility_system.h"

namespace Vkm::Test {

namespace {

using namespace Vkm::Engine;

constexpr int WIDTH  = 320;
constexpr int HEIGHT = 180;

// Enough frames for the bakes a first frame starts to be in the picture.
constexpr int FRAMES = 4;

// The resolution buildProbe's probe asks for, which the arrays raise to GLProbeArray::MIN_RESOLUTION.
constexpr uint32_t PROBE_RESOLUTION = 64;

// A channel further than this from the golden is a differing pixel; more than this
// share of them is a differing frame. Loose enough for a driver update; a lost lobe,
// shadow or reflection is thousands of pixels over.
constexpr int   CHANNEL_TOLERANCE = 12;
constexpr float PIXEL_TOLERANCE   = 0.002f;

const std::filesystem::path GOLDEN_DIR = "tests/render/golden";

struct Scenery {
    Scene           scene;
    ResourceManager resources;
    LiveParticles   particles;  ///< Placed by the scene, as ParticleSystem would simulate them.
    RenderSettings  settings;   ///< What the frame is rendered with.
    MeshHandle      cube;
    MeshHandle      sphere;
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
// aims and colours it each frame, as in a host; under none, this is the authored aim.
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

// A glossy floor under coloured boxes and a glowing sphere: SSR, its blur and fade,
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

    const EntityId smoke = s.scene.createEntity();
    s.scene.add(smoke, Transform{});
    ParticleEmitter emitter;
    emitter.additive   = false;
    emitter.startColor = glm::vec4(0.35f, 0.35f, 0.37f, 0.8f);
    emitter.startSize  = 0.7f;
    emitter.softness   = 0.6f;
    s.scene.add(smoke, std::move(emitter));

    LiveParticles::Pool& pool = s.particles.pools[smoke.slot()];
    pool.emitter = smoke;
    for (int i = 0; i < 7; ++i) {
        Particle p;
        p.position = {-2.4f + 0.8f * static_cast<float>(i), 0.25f, -0.6f};
        pool.particles.push_back(p);
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

    const auto emitter = [&](ParticleEmitter e) {
        const EntityId id = s.scene.createEntity();
        s.scene.add(id, Transform{});
        s.scene.add(id, std::move(e));
        LiveParticles::Pool& pool = s.particles.pools[id.slot()];
        pool.emitter = id;
        return &pool.particles;
    };

    ParticleEmitter cards;
    cards.additive     = false;
    cards.texture      = frames;
    cards.sheetColumns = 2;
    cards.sheetRows    = 2;
    cards.startColor   = glm::vec4(1.0f);
    cards.endColor     = glm::vec4(1.0f);
    cards.startSize    = 0.3f;
    cards.endSize      = 0.3f;
    cards.lifetime     = 4.0f;
    std::vector<Particle>* card = emitter(cards);
    for (int i = 0; i < 5; ++i) {
        Particle p;
        p.position = {-2.6f + 0.75f * static_cast<float>(i), 1.0f, 0.0f};
        p.lifetime = 4.0f;
        p.age      = i < 4 ? 0.5f + static_cast<float>(i) : 0.5f;
        p.angle    = i < 4 ? 0.0f : glm::quarter_pi<float>();
        card->push_back(p);
    }

    ParticleEmitter puff;
    puff.additive   = false;
    puff.startColor = glm::vec4(0.9f, 0.8f, 0.2f, 1.0f);
    puff.endColor   = puff.startColor;
    puff.startSize  = 0.25f;
    puff.endSize    = 0.25f;
    Particle disc;
    disc.position = {-1.5f, 0.45f, 0.3f};
    emitter(puff)->push_back(disc);

    ParticleEmitter sparks;
    sparks.startColor = glm::vec4(3.0f, 1.6f, 0.4f, 1.0f);
    sparks.endColor   = sparks.startColor;
    sparks.startSize  = 0.05f;
    sparks.endSize    = 0.05f;
    sparks.softness   = 0.3f;
    sparks.stretch    = 0.1f;
    std::vector<Particle>* spark = emitter(sparks);
    const glm::vec3 headings[3] = {{8.0f, 0.0f, 0.0f}, {0.0f, 8.0f, 0.0f}, {0.0f, 0.0f, 8.0f}};
    for (int i = 0; i < 3; ++i) {
        Particle p;
        p.position = {1.8f + 0.8f * static_cast<float>(i), 1.0f, 0.0f};
        p.velocity = headings[i];
        spark->push_back(p);
    }
    sunAndCamera(s, {0.0f, 1.3f, 4.0f}, {0.0f, 1.0f, 0.0f});
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
    const EntityId lamp = s.scene.createEntity();
    Transform lampAt;
    lampAt.position = eye + glm::vec3(0.22f, -0.08f, -0.37f);
    s.scene.add(lamp, std::move(lampAt));
    Light bulb;
    bulb.type        = LightType::Point;
    bulb.color       = {1.0f, 0.55f, 0.2f};
    bulb.intensity   = 0.15f;
    bulb.radius      = 0.2f;
    bulb.castShadows = false;
    s.scene.add(lamp, std::move(bulb));

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

    const EntityId muzzle = s.scene.createEntity();
    s.scene.add(muzzle, Transform{});
    ParticleEmitter flash;
    flash.viewmodel  = true;
    flash.startColor = glm::vec4(4.0f, 2.4f, 0.8f, 1.0f);
    flash.endColor   = flash.startColor;
    flash.startSize  = 0.05f;
    flash.endSize    = 0.05f;
    flash.softness   = 0.5f;
    s.scene.add(muzzle, std::move(flash));
    LiveParticles::Pool& pool = s.particles.pools[muzzle.slot()];
    pool.emitter = muzzle;
    Particle spark;
    spark.position = glm::vec3(0.22f, 1.45f, -0.9f);
    pool.particles.push_back(spark);
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

    const EntityId bulb = s.scene.createEntity();
    Transform pose;
    pose.position = {1.4f, 2.0f, 0.9f};
    s.scene.add(bulb, std::move(pose));
    Light light;
    light.type         = LightType::Point;
    light.intensity    = 40.0f;
    light.radius       = 15.0f;
    light.color        = {1.0f, 0.78f, 0.55f};
    light.sourceRadius = 0.12f;
    s.scene.add(bulb, std::move(light));
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
// colour most at a low sun.
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
    sunAndCamera(s, {3.0f, 1.7f, 3.0f}, {-2.5f, 1.2f, -3.0f});
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
    int frames = FRAMES
) {
    Clock          clock;
    EventBus       events;
    WindowManager  window;   // never opened: the pbuffer is the surface
    InputMap       input;
    NetSession     net;
    HostChrome     chrome;
    RenderSettings tuning = settings;
    FrameContext   ctx{s.scene, s.resources, clock, events, window, input, net, chrome, tuning};

    SkySystem        sky;
    OceanSystem      ocean;
    VisibilitySystem visibility;
    RenderView       view;
    view.viewportWidth  = WIDTH;
    view.viewportHeight = HEIGHT;
    view.surfaceWidth   = WIDTH;
    view.surfaceHeight  = HEIGHT;
    view.settings       = tuning;

    for (int frame = 0; frame < frames; ++frame) {
        sky.update(ctx);
        ocean.update(ctx);
        visibility.update(ctx);
        view.build(s.scene, *ctx.visibility, ui, splash, nullptr, &s.particles, ctx.ocean);
        backend.render(view, s.resources);
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

// @p culled draws with the GPU occlusion cull on, against the same golden: it may change no frame.
void compare(
    const char* name,
    void (*build)(Scenery&),
    const std::string& renderer,
    int& failures,
    bool culled = false
) {
    // A backend of its own: each scene's asset handles restart where the last one's
    // did, which an outliving backend would take for the same assets.
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    const std::string label = culled ? std::string(name) + ", culled" : std::string(name);
    if (!backend.init(window)) {
        ++failures;
        std::printf("  %-62s %s\n", label.c_str(), "<-- FAILED: the backend did not start");
        return;
    }

    Scenery scenery;
    build(scenery);
    scenery.settings.occlusionCulling = culled;
    const std::vector<unsigned char> frame = render(scenery, backend, nullptr, scenery.settings);

    const std::filesystem::path png = GOLDEN_DIR / (std::string(name) + ".png");
    const std::filesystem::path gpu = GOLDEN_DIR / (std::string(name) + ".gpu");

    if (std::getenv("VKM_UPDATE_GOLDENS")) {
        if (culled) return;
        std::filesystem::create_directories(GOLDEN_DIR);
        stbi_write_png(png.string().c_str(), WIDTH, HEIGHT, 3, frame.data(), WIDTH * 3);
        std::ofstream(gpu) << renderer << "\n";
        std::printf("  %-62s %s\n", name, "written");
        return;
    }

    const std::string madeOn = readFile(gpu);
    if (madeOn.empty()) {
        ++failures;
        std::printf("  %-62s %s\n", label.c_str(), "<-- FAILED: no golden (VKM_UPDATE_GOLDENS=1 makes one)");
        return;
    }
    if (madeOn != renderer) {
        std::printf("  %-62s skipped: golden made on %s\n", label.c_str(), madeOn.c_str());
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

    restoreContextDefaults();
    GLBackend fresh;
    if (!fresh.init(window)) {
        expect(what, false, failures);
        return;
    }
    Scenery only;
    build(only);
    after(only);
    const std::vector<unsigned char> want = render(only, fresh);

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

    restoreContextDefaults();
    GLBackend fresh;
    if (!fresh.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery set;
    buildSunset(set);
    const std::vector<unsigned char> want = render(set, fresh);

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

    restoreContextDefaults();
    GLBackend fresh;
    if (!fresh.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery set;
    buildProbe(set);
    set.scene.environment().sky.sunElevation = 4.0f;
    const std::vector<unsigned char> want = render(set, fresh);

    const float share = differingShare(seen, want.data());
    std::printf("      %.2f%% of pixels differ\n", share * 100.0f);
    expect("  and once it is, the frame is a fresh backend's", share <= PIXEL_TOLERANCE, failures);
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

// A bare floor at a grazing angle: nothing occludes it. Each GTAO slice integrates an
// oblique open surface to more than one and its neighbours to less, so it is only right
// once averaged - clamped to one first, the average darkens an empty floor.
void testAnOpenFloorIsNotOccluded(int& failures) {
    std::printf("Ambient occlusion over an open floor:\n");

    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }

    Scenery s;
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:open", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f),
        {0.0f, -0.5f, 0.0f},
        {200.0f, 1.0f, 200.0f}
    );
    sunAndCamera(s, {0.0f, 1.5f, 0.0f}, {0.0f, 0.0f, -6.0f});

    RenderSettings settings;
    settings.renderMode    = RenderMode::AmbientOcclusion;
    settings.msaaSamples   = 1;
    settings.gtaoIntensity = 3.0f;  // strong, so a bias shows
    settings.gtaoPower     = 4.0f;
    const std::vector<unsigned char> frame = render(s, backend, nullptr, settings);

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
    std::vector<unsigned char> frames[2];
    for (const bool ssr : {false, true}) {
        restoreContextDefaults();
        WindowManager window;
        GLBackend backend;
        if (!backend.init(window)) {
            expect("the backend starts", false, failures);
            return;
        }
        Scenery s;
        buildMirror(s);
        orthographic(s, 2.5f);
        RenderSettings settings;
        settings.ssr = ssr;
        frames[ssr ? 1 : 0] = render(s, backend, nullptr, settings);
    }
    long sum = 0;
    for (size_t i = 0; i < frames[0].size(); ++i) sum += std::abs(frames[1][i] - frames[0][i]);
    const float mean = static_cast<float>(sum) / static_cast<float>(frames[0].size());
    std::printf("      the traced frame differs by %.2f a channel on average\n", mean);
    expect("an orthographic view traces its reflections", mean > 0.05f, failures);
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

    restoreContextDefaults();
    WindowManager window;
    GLBackend fresh;
    if (!fresh.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery there;
    build(there, to);
    const std::vector<unsigned char> want = render(there, fresh);

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
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    base(s);
    place(
        s,
        s.cube,
        material(s, "golden:grey", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f),
        {0.0f, 1.5f, -0.5f},
        {40.0f, 30.0f, 1.0f}
    );
    sunAndCamera(s, {0.0f, 1.5f, 3.0f}, {0.0f, 1.5f, 0.0f});

    RenderSettings settings;
    settings.renderMode    = RenderMode::AmbientOcclusion;
    settings.msaaSamples   = 1;
    settings.gtaoIntensity = 3.0f;  // strong, so a bias shows
    const std::vector<unsigned char> frame = render(s, backend, nullptr, settings);

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
        restoreContextDefaults();
        WindowManager window;
        GLBackend backend;
        if (!backend.init(window)) {
            expect("the backend starts", false, failures);
            return;
        }
        Scenery s;
        buildRoomShell(s, openings);
        sunAndCamera(s, {3.0f, 1.7f, 3.0f}, {-2.5f, 1.2f, -3.0f});
        means[openings ? 1 : 0] = meanValue(render(s, backend, nullptr, settings));
    }
    std::printf("      mean %.2f sealed, %.2f with a doorway and a window\n", means[0], means[1]);
    expect("a sealed room is dark inside", means[0] < 1.0f, failures);
    expect("  and one with openings keeps the light they let in", means[1] > 8.0f, failures);
}

// A point lamp under buildRoomShell's ceiling, casting no shadow.
void switchOnTheLamp(Scenery& s) {
    const EntityId lamp = s.scene.createEntity();
    Transform at;
    at.position = {-2.0f, 3.2f, -2.0f};
    s.scene.add(lamp, std::move(at));
    Light bulb;
    bulb.type        = LightType::Point;
    bulb.intensity   = 60.0f;
    bulb.radius      = 12.0f;
    bulb.castShadows = false;
    s.scene.add(lamp, std::move(bulb));
}

// A volume's light follows its lights and its sky without a new bake version: its surfels are lit
// again, and the frame then is the one a fresh bake under the new light draws. The room's own
// direct light changes too, but the bounce it sends round the room is what a stale grid would
// keep.
void testAnIrradianceVolumeFollowsItsLight(int& failures) {
    std::printf("An irradiance volume whose lighting changes:\n");

    sameAsAFreshBackend(
        "a room whose sun moves is lit again as a fresh bake lights it",
        [](Scenery& s) {
            buildRoomShell(s, true);
            sunAndCamera(s, {3.0f, 1.7f, 3.0f}, {-2.5f, 1.2f, -3.0f});
        },
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
        [](Scenery& s) {
            buildRoomShell(s, false);
            sunAndCamera(s, {3.0f, 1.7f, 3.0f}, {-2.5f, 1.2f, -3.0f});
        },
        [](Scenery&) {},
        switchOnTheLamp,
        failures
    );
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
    buildRoomShell(room, false);
    sunAndCamera(room, {3.0f, 1.7f, 3.0f}, {-2.5f, 1.2f, -3.0f});
    render(room, lived);
    switchOnTheLamp(room);
    render(room, lived, nullptr, RenderSettings{}, nullptr, 1);
    nudge(room, 0.5f);
    render(room, lived, nullptr, RenderSettings{}, nullptr, 1);
    nudge(room, -0.5f);
    render(room, lived);
    render(room, lived);
    const std::vector<unsigned char> seen = render(room, lived);

    restoreContextDefaults();
    GLBackend fresh;
    if (!fresh.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery lit;
    buildRoomShell(lit, false);
    sunAndCamera(lit, {3.0f, 1.7f, 3.0f}, {-2.5f, 1.2f, -3.0f});
    switchOnTheLamp(lit);
    const std::vector<unsigned char> want = render(lit, fresh);

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
    buildRoomShell(room, false);
    sunAndCamera(room, {3.0f, 1.7f, 3.0f}, {-2.5f, 1.2f, -3.0f});
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
// GTAO reads a viewmodel's squashed depth as sky: as near as it is drawn, it would shade the wall
// it is held against with a halo. Against the same wall with the block hidden in it, the wall
// round the block's outline keeps its shade.
void testAViewmodelShadesNothingBehindIt(int& failures) {
    std::printf("A weapon held into a wall, and the wall round it:\n");

    const auto frameWith = [&](SeenBy seenBy, glm::vec3 glow) {
        restoreContextDefaults();
        WindowManager window;
        GLBackend backend;
        if (!backend.init(window)) return std::vector<unsigned char>{};
        Scenery s;
        buildHeld(s, seenBy, -0.75f, glow);
        // No bloom: the marked block's glow would spread over the wall measured.
        s.settings.bloom = false;
        return render(s, backend, nullptr, s.settings);
    };
    const std::vector<unsigned char> held   = frameWith(SeenBy::Viewmodel, glm::vec3(0.0f));
    const std::vector<unsigned char> hidden = frameWith(SeenBy::EyeOnly, glm::vec3(0.0f));
    // The block glowing green: where it is drawn, however like the wall its own shade.
    const std::vector<unsigned char> marked = frameWith(SeenBy::Viewmodel, glm::vec3(0.0f, 0.5f, 0.0f));
    if (held.empty() || hidden.empty() || marked.empty()) {
        expect("the backend starts", false, failures);
        return;
    }

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

// A transparent pane's shadow is as deep as the pane is opaque: half clear casts about half a
// shadow, and two half-clear panes stacked about three quarters, each pane its own dither. Read off
// the direct light alone, whose display curve (Reinhard, then sRGB) the test undoes.
void testATransparentPanesShadowIsAsDeepAsItIsOpaque(int& failures) {
    std::printf("Shadows of transparent panes:\n");

    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }

    Scenery s;
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
    // Looking down at a slant, so no pane stands between the eye and a shadow it is read at.
    const glm::vec3 eye    = {0.0f, 16.0f, 12.0f};
    const glm::vec3 target = {1.0f, 0.0f, 0.0f};
    sunAndCamera(s, eye, target);

    RenderSettings settings;
    settings.renderMode  = RenderMode::DirectOnly;
    settings.tonemap     = Tonemap::Reinhard;
    settings.msaaSamples = 1;
    settings.bloom       = false;
    settings.ssr         = false;
    settings.gtao        = false;
    const std::vector<unsigned char> frame = render(s, backend, nullptr, settings);

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
// water foams it. Asserted per pixel, foam coloured green over dark water, looking straight down.
void testTheShoreRingsWhatBreaksTheWater(int& failures) {
    std::printf("The ocean's waterline foam:\n");

    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }

    Scenery s;
    base(s);
    MaterialAsset sea;
    sea.albedo    = glm::vec4(0.01f, 0.02f, 0.04f, 1.0f);
    sea.roughness = 0.3f;
    sea.waves     = true;
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

    // Looking down from six metres the frame spans seven and a half either way across: the post
    // stands just past its right edge, its top too low to show, and the block hovers clear of
    // the water on the left.
    const MaterialHandle grey = material(s, "golden:post", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f);
    place(s, s.cube, grey, {8.0f, -0.6f, 0.0f}, {0.5f, 1.8f, 0.5f});
    place(s, s.cube, grey, {8.0f, -1.5f, 0.0f}, {6.0f, 1.0f, 6.0f});
    place(s, s.cube, grey, {-3.5f, 1.0f, 0.0f}, {1.0f, 0.6f, 1.0f});
    sunAndCamera(s, {0.0f, 6.0f, 0.01f}, {0.0f, 0.0f, 0.0f});

    const std::vector<unsigned char> frame = render(s, backend);
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
        "      foam over %.0f%% of the right edge, %.2f%% of the left half\n",
        100.0f * right,
        100.0f * left
    );
    expect("water foams round a post the camera cannot see", right > 0.04f, failures);
    expect("  and nowhere else: not round a block hovering clear of it", left == 0.0f, failures);
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
    for (const uint32_t samples : {4u, 1u}) {
        std::vector<unsigned char> frames[2];
        for (const bool culled : {false, true}) {
            restoreContextDefaults();
            WindowManager window;
            GLBackend backend;
            if (!backend.init(window)) {
                expect("the backend starts", false, failures);
                return;
            }
            Scenery s;
            buildHeld(s, SeenBy::Viewmodel, -0.75f);
            s.settings.msaaSamples      = samples;
            s.settings.occlusionCulling = culled;
            frames[culled ? 1 : 0] = render(s, backend, nullptr, s.settings);
        }
        const float share = differingShare(frames[1], frames[0].data());
        std::printf("      %u sample(s): %.2f%% of pixels differ, culled\n", samples, share * 100.0f);
        const char* what = samples > 1 ? "the weapon shows as without the cull" : "  and at one sample";
        expect(what, share <= PIXEL_TOLERANCE, failures);
    }
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
    float darkness[2] = {};
    for (const bool rolled : {false, true}) {
        restoreContextDefaults();
        WindowManager window;
        GLBackend backend;
        if (!backend.init(window)) {
            expect("the backend starts", false, failures);
            return;
        }
        Scenery s;
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

        RenderSettings settings;
        settings.renderMode    = RenderMode::AmbientOcclusion;
        settings.msaaSamples   = 1;
        settings.gtaoIntensity = 3.0f;
        const std::vector<unsigned char> frame = render(s, backend, nullptr, settings);
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
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    base(s);
    const MaterialHandle grey = material(s, "golden:grey", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f);
    place(s, s.cube, grey, {0.3f, 1.0f, -10.0f}, {0.2f, 6.0f, 22.0f});
    sunAndCamera(s, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, -1.0f});
    s.scene.forEach<Camera>([](EntityId, Camera& lens) { lens.fovY = glm::radians(100.0f); });

    RenderSettings settings;
    settings.renderMode    = RenderMode::AmbientOcclusion;
    settings.msaaSamples   = 1;
    settings.gtaoIntensity = 3.0f;
    const std::vector<unsigned char> frame = render(s, backend, nullptr, settings);
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

// A box on a floor through an orthographic camera, in the AO view: the crease where they meet is
// occluded. An orthographic camera's depth is linear, so read as the viewmodel's band, the nearest
// VIEWMODEL_DEPTH_SHARE of its range would be taken for sky and occlude nothing.
void testAnOrthographicViewIsOccluded(int& failures) {
    std::printf("Ambient occlusion through an orthographic camera:\n");
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    base(s);
    const MaterialHandle grey = material(s, "golden:grey", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f);
    place(s, s.cube, grey, {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f});
    place(s, s.cube, grey, {0.0f, 0.5f, 0.0f}, glm::vec3(1.0f));
    sunAndCamera(s, {0.0f, 3.0f, 5.0f}, {0.0f, 0.0f, 0.0f});
    orthographic(s, 2.0f);

    RenderSettings settings;
    settings.renderMode    = RenderMode::AmbientOcclusion;
    settings.msaaSamples   = 1;
    settings.gtaoIntensity = 3.0f;
    const std::vector<unsigned char> frame = render(s, backend, nullptr, settings);
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

    restoreContextDefaults();
    GLBackend fresh;
    if (!fresh.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery plain;
    buildProbe(plain);
    const std::vector<unsigned char> want = render(plain, fresh);

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
    for (const Case c : {Case{50.0f, false}, Case{12.0f, false}, Case{12.0f, true}}) {
        restoreContextDefaults();
        WindowManager window;
        GLBackend backend;
        if (!backend.init(window)) {
            expect("the backend starts", false, failures);
            return;
        }
        Scenery s;
        base(s);
        s.scene.environment().sky.procedural   = false;
        s.scene.environment().sky.sunElevation = c.elevation;
        s.scene.environment().sky.sunAzimuth   = 30.0f;
        place(
            s,
            s.cube,
            material(s, "golden:open", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f),
            {0.0f, -0.5f, 0.0f},
            {200.0f, 1.0f, 200.0f}
        );
        sunAndCamera(s, {0.0f, 1.5f, 0.0f}, {0.0f, 0.0f, -6.0f});
        if (c.orthographic) orthographic(s, 3.0f);

        RenderSettings settings;
        settings.renderMode  = RenderMode::ScreenShadows;
        settings.msaaSamples = 1;
        const std::vector<unsigned char> frame = render(s, backend, nullptr, settings);
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
        restoreContextDefaults();
        WindowManager window;
        GLBackend backend;
        if (!backend.init(window)) {
            expect("the backend starts", false, failures);
            return;
        }
        Scenery s;
        base(s);
        s.scene.environment().sky.procedural   = false;
        s.scene.environment().sky.sunElevation = 20.0f;
        s.scene.environment().sky.sunAzimuth   = 90.0f;
        place(
            s,
            s.cube,
            material(s, "golden:open", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f),
            {0.0f, -0.5f, 0.0f},
            {20.0f, 1.0f, 20.0f}
        );
        place(
            s,
            s.cube,
            material(s, "golden:pebble", {0.4f, 0.4f, 0.4f}, 0.8f, 0.0f),
            {0.0f, 0.03f, 0.0f},
            glm::vec3(0.06f)
        );
        sunAndCamera(s, {0.0f, 0.5f, 0.6f}, {0.0f, 0.0f, 0.0f});
        if (c.orthographic) orthographic(s, 0.2f);

        RenderSettings settings;
        settings.renderMode    = RenderMode::ScreenShadows;
        settings.msaaSamples   = 1;
        settings.gtao          = c.gtao;
        settings.screenShadows = c.screenShadows;
        const std::vector<unsigned char> frame = render(s, backend, nullptr, settings);
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
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        expect("the backend starts", false, failures);
        return;
    }
    Scenery s;
    base(s);
    s.scene.environment().sky.procedural   = false;
    s.scene.environment().sky.sunElevation = 20.0f;
    s.scene.environment().sky.sunAzimuth   = 90.0f;
    const MaterialHandle open   = material(s, "golden:open", {0.5f, 0.5f, 0.5f}, 0.8f, 0.0f);
    const MaterialHandle pebble = material(s, "golden:pebble", {0.4f, 0.4f, 0.4f}, 0.8f, 0.0f);
    place(s, s.cube, open, {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f});
    place(s, s.cube, pebble, {0.0f, 0.03f, 0.0f}, glm::vec3(0.06f));
    // Toward +X, as the sun stands, close and low enough that the sun is in the frame: the pebble
    // fills the rows within 10 of the middle, its shadow those 10 to 21 below.
    sunAndCamera(s, {-0.5f, 0.07f, 0.0f}, {0.0f, 0.03f, 0.0f});

    RenderSettings settings;
    settings.renderMode  = RenderMode::ScreenShadows;
    settings.msaaSamples = 1;
    const std::vector<unsigned char> frame = render(s, backend, nullptr, settings);
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

// A puff of smoke at the centre of the frame under an overhead sun, with a roof over it and
// without: an alpha-blended particle scatters the light reaching it, so the roof's shadow darkens
// it; an additive one emits, so the roof leaves it as it was.
void testSmokeIsShadowedAndSparksAreNot(int& failures) {
    std::printf("Particles under a roof:\n");
    float centre[2][2] = {};
    for (const bool additive : {false, true}) {
        for (const bool roof : {false, true}) {
            restoreContextDefaults();
            WindowManager window;
            GLBackend backend;
            if (!backend.init(window)) {
                expect("the backend starts", false, failures);
                return;
            }
            Scenery s;
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
            const EntityId puff = s.scene.createEntity();
            s.scene.add(puff, Transform{});
            ParticleEmitter emitter;
            emitter.additive   = additive;
            emitter.startColor = glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
            emitter.startSize  = 0.5f;
            emitter.softness   = 0.2f;
            s.scene.add(puff, std::move(emitter));
            LiveParticles::Pool& pool = s.particles.pools[puff.slot()];
            pool.emitter = puff;
            Particle p;
            p.position = {0.0f, 1.0f, 0.0f};
            pool.particles.push_back(p);
            sunAndCamera(s, {0.0f, 1.0f, 3.0f}, {0.0f, 1.0f, 0.0f});

            RenderSettings settings;
            settings.msaaSamples = 1;
            const glm::ivec3 c = pixel(render(s, backend, nullptr, settings), WIDTH / 2, HEIGHT / 2);
            centre[additive ? 1 : 0][roof ? 1 : 0] = static_cast<float>(c.x + c.y + c.z) / 3.0f;
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
    float centre[2] = {};
    for (const bool painted : {false, true}) {
        restoreContextDefaults();
        WindowManager window;
        GLBackend backend;
        if (!backend.init(window)) {
            expect("the backend starts", false, failures);
            return;
        }
        Scenery s;
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
        const EntityId lamp = s.scene.createEntity();
        Transform lampAt;
        lampAt.position = {0.0f, 1.5f, 0.0f};
        s.scene.add(lamp, std::move(lampAt));
        Light bulb;
        bulb.type      = LightType::Point;
        bulb.intensity = 6.0f;
        bulb.radius    = 6.0f;
        s.scene.add(lamp, std::move(bulb));
        camera(s, {0.0f, 4.0f, 3.0f}, {0.0f, 0.0f, 0.0f});

        RenderSettings settings;
        settings.msaaSamples = 1;
        const glm::ivec3 c = pixel(render(s, backend, nullptr, settings), WIDTH / 2, HEIGHT / 2);
        centre[painted ? 1 : 0] = static_cast<float>(c.x + c.y + c.z) / 3.0f;
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
    float centre[2] = {};
    for (const bool lamp : {false, true}) {
        restoreContextDefaults();
        WindowManager window;
        GLBackend backend;
        if (!backend.init(window)) {
            expect("the backend starts", false, failures);
            return;
        }
        Scenery s;
        base(s);
        s.scene.environment().sky.sunElevation = -30.0f;
        const glm::vec3 at(0.0f, 2.0f, 0.0f);
        if (lamp) {
            // 1.41 m from the centre, 0.42 m from the quad's upper right corner (the camera's right
            // is +X).
            const EntityId bulb = s.scene.createEntity();
            Transform pose;
            pose.position = at + glm::vec3(1.0f, 1.0f, 0.0f);
            s.scene.add(bulb, std::move(pose));
            Light light;
            light.type        = LightType::Point;
            light.intensity   = 20.0f;
            light.radius      = 1.1f;
            light.castShadows = false;
            s.scene.add(bulb, std::move(light));
        }
        const EntityId puff = s.scene.createEntity();
        s.scene.add(puff, Transform{});
        ParticleEmitter emitter;
        emitter.additive   = false;
        emitter.startColor = glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
        emitter.startSize  = 0.7f;
        s.scene.add(puff, std::move(emitter));
        LiveParticles::Pool& pool = s.particles.pools[puff.slot()];
        pool.emitter = puff;
        Particle p;
        p.position = at;
        pool.particles.push_back(p);
        sunAndCamera(s, at + glm::vec3(0.0f, 0.0f, 3.0f), at);

        RenderSettings settings;
        settings.msaaSamples = 1;
        const glm::ivec3 c = pixel(render(s, backend, nullptr, settings), WIDTH / 2, HEIGHT / 2);
        centre[lamp ? 1 : 0] = static_cast<float>(c.x + c.y + c.z) / 3.0f;
    }
    std::printf("      %.0f without the lamp, %.0f with it\n", centre[0], centre[1]);
    expect("a lamp that does not reach a puff lights none of it", centre[1] <= centre[0] + 2.0f, failures);
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
    float mean[2] = {};
    for (const bool sheen : {false, true}) {
        restoreContextDefaults();
        WindowManager window;
        GLBackend backend;
        if (!backend.init(window)) {
            expect("the backend starts", false, failures);
            return;
        }
        Scenery s;
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

        RenderSettings settings;
        settings.msaaSamples = 1;
        settings.tonemap     = Tonemap::Reinhard;
        settings.exposure    = -2.0f;
        const std::vector<unsigned char> frame = render(s, backend, nullptr, settings);
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

void testTheFramesMatchTheirGoldens(GLContext& gl, int& failures) {
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
    };
    for (const Golden& golden : goldens) compare(golden.name, golden.build, gl.renderer(), failures);
    for (const Golden& golden : goldens) compare(golden.name, golden.build, gl.renderer(), failures, true);

    testTheUIOverlayLandsOnItsPixels(failures);
    testAViewmodelShadesNothingBehindIt(failures);
    testABackendDropsWhatTheSceneNoLongerAsksFor(failures);
    testAFrameWithNoCameraKeepsTheProbesBaked(failures);
    testAProbeFollowsItsLight(failures);
    testWhatTheCullHidDrawsTheFrameItComesOut(failures);
    testAWeaponHeldIntoAWallIsNotCulled(failures);
    testAFaceFillingTheFrameIsNotCulledByItsOwnDepth(failures);
    testADriftingSunIsFollowedOverFrames(failures);
    testASplashFadesLinearlyOnTheGlass(failures);
    testAnOpenFloorIsNotOccluded(failures);
    testAnOpenWallIsOpenToTheFramesEdge(failures);
    testAnOrthographicViewIsOccluded(failures);
    testAnOpenFloorCastsNoScreenShadow(failures);
    testAPebbleCastsAScreenShadow(failures);
    testAScreenShadowFacingTheSunFallsTowardTheEye(failures);
    testGTAOReachesAsFarEveryWay(failures);
    testAWallBesideTheEyeIsOpen(failures);
    testAnOrthographicViewTracesItsReflections(failures);
    testSmokeIsShadowedAndSparksAreNot(failures);
    testADecalIsLitAsTheFloorUnderIt(failures);
    testSmokeIsLitWhereItIs(failures);
    testAMovedProbeBakesOnceItSettles(failures);
    testASheenAddsNoLight(failures);
    testAProbeBakedUnderADebugViewHoldsTheScene(failures);
    testASealedRoomHoldsNoSkyLight(failures);
    testAnIrradianceVolumeFollowsItsLight(failures);
    testACutShortRelightIsAskedForAgain(failures);
    testAVolumeShowsItsGridUntilItsRelightIsThrough(failures);
    testTheShoreRingsWhatBreaksTheWater(failures);
    testATransparentPanesShadowIsAsDeepAsItIsOpaque(failures);
}

} // namespace

void runGoldenTests(GLContext& gl, int& failures) {
    testTheFramesMatchTheirGoldens(gl, failures);
}

} // namespace Vkm::Test

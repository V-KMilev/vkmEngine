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
#include "system/sky/sky_system.h"
#include "system/visibility/visibility_system.h"

namespace Vkm::Test {

namespace {

using namespace Vkm::Engine;

constexpr int WIDTH  = 320;
constexpr int HEIGHT = 180;

// Enough frames for the bakes a first frame starts to be in the picture.
constexpr int FRAMES = 4;

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

// The frame, rendered and read back as RenderSystem does for a screenshot: top row
// first, as a PNG stores. @p ui is the overlay UISystem would have published, if any,
// and @p splash the logo SplashSystem would have.
std::vector<unsigned char> render(
    Scenery& s,
    GLBackend& backend,
    const UIDrawData* ui = nullptr,
    const RenderSettings& settings = RenderSettings{},
    const SplashFrame* splash = nullptr
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
    VisibilitySystem visibility;
    RenderView       view;
    view.viewportWidth  = WIDTH;
    view.viewportHeight = HEIGHT;
    view.surfaceWidth   = WIDTH;
    view.surfaceHeight  = HEIGHT;
    view.settings       = tuning;

    for (int frame = 0; frame < FRAMES; ++frame) {
        sky.update(ctx);
        visibility.update(ctx);
        view.build(s.scene, *ctx.visibility, ui, splash, nullptr, &s.particles);
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

// The share of pixels where two frames differ by more than a driver update would.
float differingShare(const std::vector<unsigned char>& frame, const unsigned char* other) {
    size_t differing = 0;
    for (size_t px = 0; px < static_cast<size_t>(WIDTH) * HEIGHT; ++px) {
        for (int c = 0; c < 3; ++c) {
            if (std::abs(int(frame[px * 3 + c]) - int(other[px * 3 + c])) > CHANNEL_TOLERANCE) {
                ++differing;
                break;
            }
        }
    }
    return static_cast<float>(differing) / static_cast<float>(WIDTH * HEIGHT);
}

void compare(const char* name, void (*build)(Scenery&), const std::string& renderer, int& failures) {
    // A backend of its own: each scene's asset handles restart where the last one's
    // did, which an outliving backend would take for the same assets.
    restoreContextDefaults();
    WindowManager window;
    GLBackend backend;
    if (!backend.init(window)) {
        ++failures;
        std::printf("  %-62s %s\n", name, "<-- FAILED: the backend did not start");
        return;
    }

    Scenery scenery;
    build(scenery);
    const std::vector<unsigned char> frame = render(scenery, backend);

    const std::filesystem::path png = GOLDEN_DIR / (std::string(name) + ".png");
    const std::filesystem::path gpu = GOLDEN_DIR / (std::string(name) + ".gpu");

    if (std::getenv("VKM_UPDATE_GOLDENS")) {
        std::filesystem::create_directories(GOLDEN_DIR);
        stbi_write_png(png.string().c_str(), WIDTH, HEIGHT, 3, frame.data(), WIDTH * 3);
        std::ofstream(gpu) << renderer << "\n";
        std::printf("  %-62s %s\n", name, "written");
        return;
    }

    const std::string madeOn = readFile(gpu);
    if (madeOn.empty()) {
        ++failures;
        std::printf("  %-62s %s\n", name, "<-- FAILED: no golden (VKM_UPDATE_GOLDENS=1 makes one)");
        return;
    }
    if (madeOn != renderer) {
        std::printf("  %-62s skipped: golden made on %s\n", name, madeOn.c_str());
        return;
    }

    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* golden = stbi_load(png.string().c_str(), &w, &h, &channels, 3);
    if (!golden || w != WIDTH || h != HEIGHT) {
        if (golden) stbi_image_free(golden);
        ++failures;
        std::printf("  %-62s %s\n", name, "<-- FAILED: golden unreadable or the wrong size");
        return;
    }

    const float share = differingShare(frame, golden);
    stbi_image_free(golden);

    const bool same = share <= PIXEL_TOLERANCE;
    if (!same) {
        ++failures;
        // Beside the log, so a failure can be looked at.
        const std::string seen = "/tmp/vkm_golden_" + std::string(name) + ".png";
        stbi_write_png(seen.c_str(), WIDTH, HEIGHT, 3, frame.data(), WIDTH * 3);
        std::printf("      %.2f%% of pixels differ; this frame is at %s\n", share * 100.0f, seen.c_str());
    }
    std::printf("  %-62s %s\n", name, same ? "ok" : "<-- FAILED");
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
    capture.resolution = 64;
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

    // A probe's capture is lit by the key light's map fitted to its box, so a resized
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
}

// A frame with no camera gathers no probes; the scene has not lost them. A noon capture
// still shows at sunset after cameraless frames, as with a backend that drew every
// frame through one; dropped and retaken, it would show the sunset.
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

// The overlay drawn by the real pass over a real frame, asserted per pixel, not against
// a golden: where its edges land is arithmetic that holds on any GPU.
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

void testTheFramesMatchTheirGoldens(GLContext& gl, int& failures) {
    std::printf("Whole frames against their goldens:\n");
    if (!gl.attachSurface(WIDTH, HEIGHT)) {
        std::printf("  no pbuffer surface on this device; skipped\n");
        return;
    }

    compare("materials",   buildMaterials,   gl.renderer(), failures);
    compare("reflections", buildReflections, gl.renderer(), failures);
    compare("glint",       buildGlint,       gl.renderer(), failures);
    compare("smoke",       buildSmoke,       gl.renderer(), failures);
    compare("cutout",      buildCutout,      gl.renderer(), failures);
    compare("decals",      buildDecals,      gl.renderer(), failures);
    compare("fog",         buildFog,         gl.renderer(), failures);
    compare("nosky",       buildNoSky,       gl.renderer(), failures);
    compare("spot",        buildSpot,        gl.renderer(), failures);
    compare("penumbra",    buildPenumbra,    gl.renderer(), failures);

    testTheUIOverlayLandsOnItsPixels(failures);
    testABackendDropsWhatTheSceneNoLongerAsksFor(failures);
    testAFrameWithNoCameraKeepsTheProbesBaked(failures);
    testASplashFadesLinearlyOnTheGlass(failures);
    testAnOpenFloorIsNotOccluded(failures);
    testASealedRoomHoldsNoSkyLight(failures);
}

} // namespace

void runGoldenTests(GLContext& gl, int& failures) {
    testTheFramesMatchTheirGoldens(gl, failures);
}

} // namespace Vkm::Test

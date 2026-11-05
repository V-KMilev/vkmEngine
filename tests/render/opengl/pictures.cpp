// The reference pictures: a few scenes, each framing a set of the renderer's features large enough
// to see, drawn as a game draws them and compared with what this GPU drew when they were made
// (gpu.h, matchesReference). A feature that stops drawing, or draws otherwise, changes its picture
// by far more than a driver's rounding.

#include <cmath>

#include "core/frame_context.h"
#include "core/system.h"
#include "debug/draw/debug_draw.h"
#include "ecs/component/effect/decal.h"
#include "ecs/component/effect/particle_emitter.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/component/landscape/water.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/outline.h"
#include "ecs/component/ui/ui_build.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_shape.h"
#include "font_baker.h"
#include "resource/asset/terrain_asset.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

namespace {

/// A checkerboard of @p squares a side over 64 texels, @p a and @p b, opaque.
TextureHandle checker(
    ResourceManager& resources,
    const std::string& name,
    int squares,
    glm::vec3 a,
    glm::vec3 b,
    float alphaOfB = 1.0f
) {
    return texture(resources, name, 64, TextureUsage::Color, [=](uint32_t x, uint32_t y) {
        const bool first = ((x * squares / 64) + (y * squares / 64)) % 2 == 0;
        return first ? glm::vec4(a, 1.0f) : glm::vec4(b, alphaOfB);
    });
}

/// Round bumps, @p across a side: a normal map's X and Y.
TextureHandle bumps(ResourceManager& resources, const std::string& name, int across) {
    return texture(resources, name, 64, TextureUsage::Normal, [=](uint32_t x, uint32_t y) {
        const float cell = 64.0f / static_cast<float>(across);
        const float u    = std::fmod(static_cast<float>(x) + 0.5f, cell) / cell * 2.0f - 1.0f;
        const float v    = std::fmod(static_cast<float>(y) + 0.5f, cell) / cell * 2.0f - 1.0f;
        const glm::vec2 slope = glm::vec2(u, v) * 0.6f;
        return glm::vec4(slope * 0.5f + 0.5f, 0.0f, 1.0f);
    });
}

MaterialHandle add(ResourceManager& resources, const std::string& name, MaterialAsset made) {
    return resources.add(std::move(made), name);
}

MaterialAsset plain(const glm::vec3& albedo, float roughness, float metallic = 0.0f) {
    MaterialAsset made;
    made.albedo    = glm::vec4(albedo, 1.0f);
    made.roughness = roughness;
    made.metallic  = metallic;
    return made;
}

/**
 * @brief Draws the same lines every frame into a DebugDraw of its own, handed to the frame as a host
 *        with a window hands its own: a headless one's frame has none.
 */
class Liner : public System {
    public:
        Liner() = default;
        ~Liner() override = default;

        Liner(const Liner& other) = delete;
        Liner& operator=(const Liner& other) = delete;

        Liner(Liner && other) = delete;
        Liner& operator=(Liner && other) = delete;

    public:
        void update(FrameContext& ctx) override {
            ctx.debug = &m_draw;
            // Round the post, ghosted where the post hides it; through it, on top; and beside it.
            m_draw.box({0.0f, 1.0f, 0.0f}, glm::vec3(0.8f, 1.2f, 0.8f), glm::vec3(0.2f, 1.0f, 0.4f));
            const DebugStyle through = DebugStyle(glm::vec3(1.0f, 0.3f, 0.2f)).wide(4.0f).onTop();
            m_draw.line({-3.0f, 1.0f, 0.0f}, {3.0f, 1.0f, 0.0f}, through);
            m_draw.sphere({-2.5f, 1.0f, 1.5f}, 0.8f, glm::vec3(0.3f, 0.6f, 1.0f));
            m_draw.arrow({2.0f, 0.2f, 1.5f}, {2.0f, 2.2f, 1.5f}, glm::vec3(1.0f, 0.9f, 0.2f));
            m_draw.point({2.5f, 1.5f, -1.0f}, DebugStyle(glm::vec3(1.0f, 1.0f, 1.0f)).wide(3.0f));
        }

    private:
        DebugDraw m_draw;
};

} // namespace

TEST("the grid and debug lines draw as they did: the floor's plane and axes, lines ghosted, on top, wide and a point") {
    Screen screen;
    Scene& scene = screen.scene();
    ResourceManager& r = screen.resources();
    const Shapes shapes(r);
    // The floor a little under the grid's plane, which would otherwise lie in it.
    const MaterialHandle floor = add(r, "test:floor", plain({0.04f, 0.04f, 0.04f}, 0.9f));
    const MaterialHandle post  = add(r, "test:post", plain({0.6f, 0.5f, 0.4f}, 0.7f));
    place(scene, shapes.cube, floor, {0.0f, -0.6f, 0.0f}, {40.0f, 1.0f, 40.0f});
    place(scene, shapes.cube, post, {0.0f, 1.0f, 0.0f}, {1.0f, 2.0f, 1.0f});
    addSun(scene);
    addEye(scene, {1.5f, 3.5f, 7.0f}, {0.0f, 0.8f, 0.0f});
    screen.chrome().display.gridAxisX = true;
    screen.chrome().display.gridAxisZ = true;
    screen.game.engine.addSystem<Liner>(SystemStage::Simulation);
    matchesReference(screen.shot(), "lines", screen.device);
}

TEST("materials draw as they did: every lobe, map, type and tiling, emission's bloom and a far floor's filtering") {
    Screen screen;
    Scene& scene = screen.scene();
    ResourceManager& r = screen.resources();
    const Shapes shapes(r);

    // A checkered floor, tiled, running far off: its texture filtered at a slant.
    MaterialAsset floor = plain(glm::vec3(1.0f), 0.7f);
    floor.albedoTexture = checker(r, "test:checks", 2, {0.8f, 0.8f, 0.75f}, {0.25f, 0.25f, 0.3f});
    floor.uvScale       = {20.0f, 20.0f};
    addFloor(scene, shapes, add(r, "test:floor", floor));

    // The back row: rough and smooth metal, a clear coat over bumps, cloth, skin, an emitter.
    MaterialAsset smooth = plain({0.95f, 0.95f, 0.95f}, 0.05f, 1.0f);
    MaterialAsset coat   = plain({0.7f, 0.05f, 0.05f}, 0.6f);
    MaterialAsset cloth  = plain({0.05f, 0.08f, 0.3f}, 0.9f);
    MaterialAsset skin   = plain({0.8f, 0.55f, 0.45f}, 0.5f);
    MaterialAsset glow   = plain({0.1f, 0.1f, 0.1f}, 0.5f);
    smooth.normalTexture    = bumps(r, "test:fine", 32);  // finer than a pixel: specular antialiasing
    coat.normalTexture      = bumps(r, "test:coarse", 6);
    coat.clearcoat          = 1.0f;
    coat.clearcoatRoughness = 0.05f;
    cloth.sheenColor        = {0.9f, 0.9f, 1.0f};
    cloth.sheenRoughness    = 0.4f;
    skin.subsurface         = 1.0f;
    skin.subsurfaceColor    = {1.0f, 0.3f, 0.2f};
    glow.emission           = {8.0f, 4.0f, 1.5f};
    const MaterialHandle back[] = {
        add(r, "test:gold", plain({1.0f, 0.78f, 0.34f}, 0.45f, 1.0f)), add(r, "test:chrome", smooth),
        add(r, "test:coat", coat), add(
            r,
            "test:cloth",
            cloth
        ), add(r, "test:skin", skin), add(r, "test:glow", glow),
    };
    for (int i = 0; i < 6; ++i) place(
        scene,
        shapes.sphere,
        back[i],
        {-5.0f + 2.0f * i, 0.9f, -1.0f},
        glm::vec3(1.8f)
    );

    // The front row: two glass panes, one before the other, over a block; a cut-out card; an unlit
    // block; a stretched block box-mapped, its checks square.
    MaterialAsset glass = plain({0.9f, 0.3f, 0.3f}, 0.05f);
    glass.type          = MaterialType::Transparent;
    glass.albedo.a      = 0.35f;

    MaterialAsset glass2 = glass;
    glass2.albedo = {0.3f, 0.4f, 0.9f, 0.35f};
    place(
        scene,
        shapes.cube,
        add(r, "test:block", plain({0.9f, 0.9f, 0.2f}, 0.5f)),
        {-3.6f, 0.4f, 1.2f},
        glm::vec3(0.8f)
    );
    place(scene, shapes.cube, add(r, "test:glass", glass), {-3.6f, 0.6f, 2.0f}, {1.2f, 1.2f, 0.05f});
    place(scene, shapes.cube, add(r, "test:glass2", glass2), {-3.3f, 0.6f, 1.7f}, {1.2f, 1.2f, 0.05f});
    MaterialAsset cut = plain(glm::vec3(1.0f), 0.6f);
    cut.type            = MaterialType::AlphaMask;
    cut.doubleSided     = true;
    cut.albedoTexture   = checker(r, "test:holes", 4, {0.2f, 0.7f, 0.2f}, {0.0f, 0.0f, 0.0f}, 0.0f);
    place(scene, shapes.cube, add(r, "test:cut", cut), {-1.4f, 0.8f, 1.6f}, {1.4f, 1.4f, 0.02f});
    MaterialAsset flat = plain({0.9f, 0.7f, 0.1f}, 0.5f);
    flat.type = MaterialType::Unlit;
    place(scene, shapes.cube, add(r, "test:unlit", flat), {0.6f, 0.4f, 1.6f}, glm::vec3(0.8f));
    MaterialAsset boxed = plain(glm::vec3(1.0f), 0.6f);
    boxed.albedoTexture = checker(r, "test:boxchecks", 2, {0.9f, 0.4f, 0.1f}, {0.1f, 0.2f, 0.6f});
    boxed.boxMapping    = 0.5f;
    place(scene, shapes.cube, add(r, "test:boxed", boxed), {3.0f, 0.4f, 1.6f}, {2.4f, 0.8f, 0.8f});
    // Many of one mesh, drawn together.
    const MaterialHandle pebble = add(r, "test:pebble", plain({0.5f, 0.45f, 0.4f}, 0.8f));
    for (int i = 0; i < 24; ++i) {
        place(scene, shapes.cube, pebble, {-5.0f + 0.45f * i, 0.1f, 3.2f}, glm::vec3(0.2f));
    }

    addSun(scene);
    addEye(scene, {0.0f, 2.0f, 6.5f}, {0.0f, 0.6f, 0.0f});
    matchesReference(screen.shot(), "materials", screen.device);
}

TEST("shadows draw as they did: the sun's cascades, spots and lamps by size and budget, cut-outs, ambient occlusion, decals and outlines") {
    Screen screen;
    Scene& scene = screen.scene();
    ResourceManager& r = screen.resources();
    const Shapes shapes(r);
    // A low evening sun, its shadows long; a large source softens them away from their casters.
    scene.environment().sky.sunElevation = 18.0f;
    addFloor(scene, shapes, add(r, "test:floor", plain({0.6f, 0.6f, 0.6f}, 0.9f)));
    const MaterialHandle stone = add(r, "test:stone", plain({0.7f, 0.68f, 0.65f}, 0.8f));
    // Posts in a row, far and near, across the cascades.
    for (int i = 0; i < 6; ++i) place(
        scene,
        shapes.cube,
        stone,
        {-6.0f, 1.5f, 4.0f - 8.0f * i},
        {0.4f, 3.0f, 0.4f}
    );
    const EntityId sun = addSun(scene);
    scene.get<Light>(sun).sourceRadius = glm::radians(2.0f);

    // A cut-out card: its shadow the pattern of its holes.
    MaterialAsset cut = plain(glm::vec3(1.0f), 0.6f);
    cut.type           = MaterialType::AlphaMask;
    cut.doubleSided    = true;
    cut.albedoTexture  = checker(r, "test:holes", 4, {0.8f, 0.3f, 0.1f}, glm::vec3(0.0f), 0.0f);
    place(scene, shapes.cube, add(r, "test:cut", cut), {-3.0f, 1.2f, 0.0f}, {1.6f, 1.6f, 0.02f});

    // A spot over a block on a post, its shadow on the floor; a lamp among pillars.
    const MaterialHandle block = add(r, "test:block", plain({0.2f, 0.4f, 0.8f}, 0.6f));
    place(scene, shapes.cube, block, {1.0f, 0.9f, 0.0f}, glm::vec3(0.6f));
    place(scene, shapes.cube, stone, {1.0f, 0.3f, 0.0f}, {0.15f, 0.6f, 0.15f});
    const EntityId spot = scene.createEntity();
    Transform spotPose;
    spotPose.position = {1.0f, 3.5f, 1.0f};
    spotPose.rotation = glm::quatLookAt(
        glm::normalize(glm::vec3(0.0f, -1.0f, -0.3f)),
        glm::vec3(0.0f, 0.0f, -1.0f)
    );
    scene.add(spot, spotPose);
    Light spotLight = generateLight(LightType::Spot);
    spotLight.color     = {1.0f, 0.8f, 0.5f};
    spotLight.intensity = 60.0f;
    spotLight.radius    = 8.0f;
    scene.add(spot, spotLight);
    for (int i = 0; i < 3; ++i) place(
        scene,
        shapes.cube,
        stone,
        {3.5f + 0.9f * i, 1.0f, -1.0f},
        {0.25f, 2.0f, 0.25f}
    );
    Light& lamp = scene.get<Light>(addLamp(scene, {4.4f, 1.2f, -0.2f}, {0.6f, 0.8f, 1.0f}, 30.0f, 6.0f));
    lamp.castShadows  = true;
    lamp.sourceRadius = 0.05f;

    // More lamps than the shadow budget holds, the highest priority keeping theirs, and 64 small
    // lamps with none, in a grid on the floor: the clusters.
    for (int i = 0; i < 64; ++i) {
        addLamp(
            scene,
            {-5.0f + 0.4f * (i % 8), 0.1f, -1.0f + 0.4f * (i / 8)},
            {0.5f + 0.5f * float(i % 2), 0.3f, 1.0f - 0.5f * float(i % 3)},
            0.15f,
            0.3f
        );
    }

    // A decal projected down onto the floor, and an outlined block.
    const EntityId stain = scene.createEntity();
    Transform stainPose;
    stainPose.position = {-0.5f, 0.0f, 3.5f};
    stainPose.rotation = glm::quatLookAt(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    stainPose.scale    = {2.0f, 2.0f, 0.4f};
    scene.add(stain, stainPose);
    Decal decal;
    decal.material = add(r, "test:stain", plain({0.6f, 0.05f, 0.5f}, 0.5f));
    scene.add(stain, decal);
    const EntityId marked = place(scene, shapes.cube, block, {2.0f, 0.5f, 3.0f}, glm::vec3(1.0f));
    Outline outline;
    outline.color = {1.0f, 0.9f, 0.1f};
    scene.add(marked, outline);

    addEye(scene, {0.0f, 3.0f, 8.0f}, {0.0f, 0.5f, 0.0f});
    matchesReference(screen.shot(), "shadows", screen.device);
}

TEST("reflections draw as they did: a polished floor mirrors what stands on it, and metal the sky") {
    Screen screen;
    Scene& scene = screen.scene();
    ResourceManager& r = screen.resources();
    const Shapes shapes(r);
    addFloor(scene, shapes, add(r, "test:floor", plain({0.3f, 0.3f, 0.32f}, 0.08f)));
    place(
        scene,
        shapes.cube,
        add(r, "test:red", plain({0.8f, 0.05f, 0.05f}, 0.5f)),
        {-2.0f, 0.75f, -1.0f},
        {1.0f, 1.5f, 1.0f}
    );
    place(scene, shapes.cube, add(
        r,
        "test:green",
        plain({0.1f, 0.7f, 0.1f}, 0.5f)
    ), {2.0f, 0.5f, -1.5f}, glm::vec3(1.0f));
    place(scene, shapes.sphere, add(
        r,
        "test:chrome",
        plain({0.95f, 0.95f, 0.95f}, 0.02f, 1.0f)
    ), {0.0f, 0.8f, 0.0f},
          glm::vec3(1.6f));
    place(
        scene,
        shapes.sphere,
        add(r, "test:copper", plain({0.95f, 0.64f, 0.54f}, 0.35f, 1.0f)),
        {-0.4f, 0.4f, 2.0f},
        glm::vec3(0.8f)
    );
    addSun(scene);
    addEye(scene, {0.0f, 1.6f, 6.0f}, {0.0f, 0.5f, 0.0f});
    matchesReference(screen.shot(), "reflections", screen.device);
}

TEST("a fixed room's baked light draws as it did: its lightmap shadows, sky occlusion, volume and probe, and a lamp left out of captures") {
    Screen screen;
    bakedRoom(screen.scene(), screen.resources());
    matchesReference(screen.shot(), "baked", screen.device);
}

TEST("the day draws as it did: the sky, the sun's tint low over the ground, the air's haze far off and fog in shafts") {
    Screen screen;
    Scene& scene = screen.scene();
    ResourceManager& r = screen.resources();
    const Shapes shapes(r);
    scene.environment().sky.sunElevation = 12.0f;
    scene.environment().sky.sunAzimuth   = 200.0f;
    place(
        scene,
        shapes.cube,
        add(r, "test:ground", plain({0.35f, 0.4f, 0.3f}, 0.9f)),
        {0.0f, -0.5f, -2000.0f},
        {8000.0f, 1.0f, 8000.0f}
    );
    const MaterialHandle block = add(r, "test:block", plain({0.7f, 0.7f, 0.7f}, 0.8f));
    // Blocks 100 m, 600 m and 2 km off, each fading further into the air.
    place(scene, shapes.cube, block, {-40.0f, 25.0f, -100.0f}, {30.0f, 50.0f, 30.0f});
    place(scene, shapes.cube, block, {150.0f, 150.0f, -600.0f}, {200.0f, 300.0f, 200.0f});
    place(scene, shapes.cube, block, {-900.0f, 600.0f, -2000.0f}, {800.0f, 1200.0f, 600.0f});
    // A wall with gaps near the eye, the fog bright where the sun comes through them.
    for (int i = 0; i < 5; ++i) place(
        scene,
        shapes.cube,
        block,
        {-8.0f + 4.0f * i, 2.0f, -14.0f},
        {3.0f, 4.0f, 0.5f}
    );
    FogSettings& fog = scene.environment().fog;
    fog.enabled = true;
    fog.density = 0.004f;
    addSun(scene);
    const EntityId eye = addEye(scene, {0.0f, 2.0f, 10.0f}, {0.0f, 12.0f, -100.0f});
    scene.get<Camera>(eye).zFar = 10000.0f;
    matchesReference(screen.shot(), "day", screen.device);
}

TEST("the night draws as it did: the moon, its glow and the stars") {
    Screen screen;
    Scene& scene = screen.scene();
    ResourceManager& r = screen.resources();
    const Shapes shapes(r);
    scene.environment().sky.sunElevation = -30.0f;
    addFloor(scene, shapes, add(r, "test:floor", plain({0.5f, 0.5f, 0.5f}, 0.8f)));
    place(
        scene,
        shapes.cube,
        add(r, "test:block", plain({0.7f, 0.7f, 0.7f}, 0.8f)),
        {0.0f, 1.0f, -6.0f},
        glm::vec3(2.0f)
    );
    addSun(scene);
    // The moon in a narrow view.
    const glm::vec3 moon = scene.environment().moonDirection();
    const EntityId eye   = addEye(
        scene,
        {0.0f, 1.5f, 6.0f},
        glm::vec3(0.0f, 1.5f, 6.0f) + moon - glm::vec3(0.0f, 0.12f, 0.0f)
    );
    scene.get<Camera>(eye).fovY = glm::radians(30.0f);
    matchesReference(screen.shot(), "night", screen.device);
}

namespace {

/// An emitter at @p at, @p made as a scene author sets one.
EntityId emitter(Scene& scene, const glm::vec3& at, ParticleEmitter made) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.position = at;
    scene.add(id, pose);
    scene.add(id, std::move(made));
    return id;
}

/// One color over a whole life, at the size each particle is born with.
void hold(ParticleEmitter& made, const glm::vec4& color) {
    made.color.keys = {{0.0f, color}};
    made.sizeOverLife.keys.clear();
}

} // namespace

TEST("particles draw as they did: added, lit and shadowed, bending, trailing, stretched, soft, flipped through a sheet, and lighting what is near") {
    Screen screen;
    Scene& scene = screen.scene();
    ResourceManager& r = screen.resources();
    const Shapes shapes(r);
    scene.environment().sky.sunElevation = 25.0f;
    scene.environment().sky.sunAzimuth   = 100.0f;
    addFloor(scene, shapes, add(r, "test:floor", plain({0.4f, 0.4f, 0.4f}, 0.8f)));
    // A striped wall behind it all, which the heat haze bends.
    const MaterialHandle light = add(r, "test:light", plain({0.8f, 0.8f, 0.8f}, 0.8f));
    const MaterialHandle dark  = add(r, "test:dark", plain({0.1f, 0.1f, 0.1f}, 0.8f));
    for (int i = 0; i < 24; ++i) {
        place(scene, shapes.cube, i % 2 ? dark : light, {-6.0f + 0.5f * i, 2.0f, -3.0f}, {0.5f, 4.0f, 0.2f});
    }
    addSun(scene);

    // Fire: added light, past 1 so it blooms, lighting the floor round it.
    ParticleEmitter fire;
    fire.rate           = 80.0f;
    fire.lifetime       = {0.8f, 1.2f};
    fire.speed          = {1.0f, 1.6f};
    fire.acceleration   = {0.0f, 0.5f, 0.0f};
    fire.size           = {0.15f, 0.25f};
    fire.intensity      = 3.0f;
    fire.lightIntensity = 6.0f;
    fire.lightRadius    = 4.0f;
    emitter(scene, {-4.0f, 0.1f, 0.0f}, fire);

    // Smoke lit by the sun and shaded by its normal map, a wall's shadow across its lower half.
    ParticleEmitter smoke;
    smoke.blend        = ParticleBlend::Alpha;
    smoke.rate         = 12.0f;
    smoke.lifetime     = {4.0f, 4.0f};
    smoke.speed        = {0.4f, 0.6f};
    smoke.acceleration = {0.0f, 0.2f, 0.0f};
    smoke.size         = {0.4f, 0.6f};
    smoke.normalMap    = bumps(r, "test:puffs", 2);
    hold(smoke, {0.7f, 0.7f, 0.7f, 0.8f});
    emitter(scene, {-1.6f, 0.2f, 0.0f}, smoke);
    place(scene, shapes.cube, light, {-0.9f, 1.0f, 0.0f}, {0.1f, 2.0f, 1.2f});

    // Heat haze before the stripes.
    ParticleEmitter haze;
    haze.blend      = ParticleBlend::Distortion;
    haze.rate       = 10.0f;
    haze.lifetime   = {2.0f, 2.0f};
    haze.speed      = {0.3f, 0.3f};
    haze.size       = {0.5f, 0.5f};
    haze.distortion = 0.05f;
    emitter(scene, {0.6f, 1.0f, -1.5f}, haze);

    // Sparks: trails curving down, and cards stretched along their flight.
    ParticleEmitter trails;
    trails.render       = ParticleRender::Trail;
    trails.rate         = 6.0f;
    trails.lifetime     = {1.5f, 1.5f};
    trails.speed        = {3.0f, 3.0f};
    trails.angle        = glm::radians(10.0f);
    trails.acceleration = {0.0f, -3.0f, 0.0f};
    trails.size         = {0.04f, 0.04f};
    trails.trailLength  = 0.5f;
    trails.intensity    = 2.0f;
    hold(trails, {0.4f, 0.8f, 1.0f, 1.0f});
    emitter(scene, {2.4f, 0.2f, 0.0f}, trails);
    ParticleEmitter streaks = trails;
    streaks.render  = ParticleRender::Stretched;
    streaks.rate    = 20.0f;
    streaks.stretch = 0.08f;
    hold(streaks, {1.0f, 0.9f, 0.3f, 1.0f});
    emitter(scene, {3.4f, 0.2f, 0.5f}, streaks);

    // A glow half sunk in the floor: soft where it meets it, not cut along a line.
    ParticleEmitter glow;
    glow.emitting     = false;
    glow.burst        = 1;
    glow.lifetime     = {1000.0f, 1000.0f};
    glow.speed        = {0.0f, 0.0f};
    glow.acceleration = glm::vec3(0.0f);
    glow.size         = {0.7f, 0.7f};
    hold(glow, {0.3f, 1.0f, 0.4f, 1.0f});
    emitter(scene, {4.6f, 0.0f, 1.5f}, glow);

    // A sheet of four colored frames, played once over a life: a row of cards at each age.
    ParticleEmitter sheet;
    sheet.blend        = ParticleBlend::Alpha;
    sheet.emission     = 1.0f;
    sheet.rate         = 2.0f;
    sheet.lifetime     = {2.0f, 2.0f};
    sheet.speed        = {0.8f, 0.8f};
    sheet.angle        = 0.0f;
    sheet.acceleration = glm::vec3(0.0f);
    sheet.size         = {0.2f, 0.2f};
    sheet.sheetColumns = 2;
    sheet.sheetRows    = 2;
    sheet.texture      = texture(r, "test:sheet", 64, TextureUsage::Color, [](uint32_t x, uint32_t y) {
        const glm::vec4 frames[] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 1, 1}};
        return frames[(x / 32) + 2 * (y / 32)];
    });
    hold(sheet, glm::vec4(1.0f));
    emitter(scene, {-3.0f, 0.5f, 1.5f}, sheet);

    addEye(scene, {0.0f, 2.0f, 7.0f}, {0.0f, 1.2f, 0.0f});
    matchesReference(screen.shot(), "particles", screen.device);
}

TEST("a heat haze that bends far draws as it did: it reads the scene as far round it as it bends") {
    Screen screen;
    Scene& scene = screen.scene();
    ResourceManager& r = screen.resources();
    const Shapes shapes(r);
    addFloor(scene, shapes, add(r, "test:floor", plain({0.4f, 0.4f, 0.4f}, 0.8f)));
    const MaterialHandle light = add(r, "test:light", plain({0.9f, 0.9f, 0.9f}, 0.8f));
    const MaterialHandle dark  = add(r, "test:dark", plain({0.05f, 0.05f, 0.05f}, 0.8f));
    for (int i = 0; i < 24; ++i) {
        place(scene, shapes.cube, i % 2 ? dark : light, {-6.0f + 0.5f * i, 2.0f, -3.0f}, {0.5f, 4.0f, 0.2f});
    }
    addSun(scene);

    // One card held still, its every texel bending to the right a quarter of the view's height, further
    // than the card is wide: what it shows is only there if the scene was copied that far round it.
    ParticleEmitter haze;
    haze.blend        = ParticleBlend::Distortion;
    haze.emitting     = false;
    haze.burst        = 1;
    haze.lifetime     = {1000.0f, 1000.0f};
    haze.speed        = {0.0f, 0.0f};
    haze.acceleration = glm::vec3(0.0f);
    haze.size         = {1.2f, 1.2f};
    haze.distortion   = 0.25f;
    haze.texture      = texture(r, "test:lean", 4, TextureUsage::Normal, [](uint32_t, uint32_t) {
        return glm::vec4(1.0f, 0.5f, 0.5f, 1.0f);
    });
    emitter(scene, {0.0f, 1.5f, -1.0f}, haze);

    // Each frame half a meter aside from the one before, a stripe's width: what this frame copied for the
    // haze to read is this frame's stripes, and what it did not copy still holds the last frame's.
    const EntityId eye = addEye(scene, {0.0f, 2.0f, 7.0f}, {0.0f, 1.2f, 0.0f});
    const auto aside   = [&](int frame) { scene.get<Transform>(eye).position.x = frame % 2 ? 0.0f : 0.5f; };
    matchesReference(screen.shot(aside), "haze", screen.device);
}

TEST("smoke over most of the view draws as it did, at half size, its edges kept against what stands in it") {
    Screen screen;
    Scene& scene = screen.scene();
    ResourceManager& r = screen.resources();
    const Shapes shapes(r);
    addFloor(scene, shapes, add(r, "test:floor", plain({0.3f, 0.3f, 0.3f}, 0.1f)));
    const MaterialHandle light = add(r, "test:light", plain({0.8f, 0.8f, 0.8f}, 0.8f));
    const MaterialHandle dark  = add(r, "test:dark", plain({0.1f, 0.1f, 0.1f}, 0.8f));
    for (int i = 0; i < 24; ++i) {
        place(scene, shapes.cube, i % 2 ? dark : light, {-6.0f + 0.5f * i, 2.0f, -3.0f}, {0.5f, 4.0f, 0.2f});
    }
    // In front of all the smoke: its two long edges stand against it, where a half-size texel spans both.
    place(
        scene,
        shapes.cube,
        add(r, "test:post", plain({0.8f, 0.2f, 0.1f}, 0.5f)),
        {0.0f, 1.0f, 2.5f},
        {0.3f, 2.0f, 0.3f}
    );
    addSun(scene);
    ParticleEmitter thick;
    thick.blend        = ParticleBlend::Alpha;
    thick.shape        = EmitShape::Box;
    thick.halfExtents  = {4.0f, 1.0f, 1.5f};
    thick.rate         = 200.0f;
    thick.maxParticles = 400;
    thick.lifetime     = {3.0f, 3.0f};
    thick.speed        = {0.1f, 0.2f};
    thick.acceleration = glm::vec3(0.0f);
    thick.size         = {0.8f, 1.2f};
    hold(thick, {0.6f, 0.6f, 0.65f, 0.25f});
    emitter(scene, {0.0f, 1.5f, -0.5f}, thick);
    addEye(scene, {0.0f, 1.5f, 5.0f}, {0.0f, 1.2f, 0.0f});
    matchesReference(screen.shot(), "smoke", screen.device);
}

TEST("a shore draws as it did: a painted terrain and its plants, the sea's waves and foam, and a running pond") {
    Screen screen;
    Scene& scene = screen.scene();
    ResourceManager& r = screen.resources();
    const Shapes shapes(r);

    // An island 256 m across rising from under the sea: sand by the water, grass above, rock on top,
    // plants in the grass. Far enough to draw its far chunks coarse.
    TerrainAsset land;
    land.resolution = 129;
    land.size       = 256.0f;
    const uint32_t n = land.resolution;
    land.heights.resize(static_cast<size_t>(n) * n);
    land.weights.resize(static_cast<size_t>(n) * n * TerrainAsset::WEIGHTS_PER_VERTEX);
    TerrainFoliage grass;
    grass.parts          = {{shapes.cube, add(r, "test:leaf", plain({0.2f, 0.5f, 0.15f}, 0.8f)),
                             glm::scale(glm::mat4(1.0f), {0.2f, 0.8f, 0.2f}), {}, {}}};
    grass.density.resize(static_cast<size_t>(n) * n);
    grass.perSquareMeter = 0.6f;
    grass.seed           = 3;
    for (uint32_t j = 0; j < n; ++j) {
        for (uint32_t i = 0; i < n; ++i) {
            const glm::vec2 at = (glm::vec2(i, j) / float(n - 1) - 0.5f) * land.size;
            const float height = 12.0f - 0.18f * glm::length(at - glm::vec2(0.0f, -60.0f));
            const size_t v     = static_cast<size_t>(j) * n + i;
            const int layer    = height < 1.5f ? 0 : height < 7.0f ? 1 : 2;
            land.heights[v]                                            = height;
            land.weights[v * TerrainAsset::WEIGHTS_PER_VERTEX + layer] = 255;
            grass.density[v]                                           = layer == 1 ? 255 : 0;
        }
    }
    land.layers[0] = add(r, "test:sand", plain({0.76f, 0.68f, 0.5f}, 0.9f));
    land.layers[1] = add(r, "test:grass", plain({0.25f, 0.45f, 0.15f}, 0.9f));
    land.layers[2] = add(r, "test:rock", plain({0.45f, 0.43f, 0.4f}, 0.8f));
    land.foliage   = {grass};
    const EntityId island = scene.createEntity();
    scene.add(island, Transform{});
    Terrain ground;
    ground.terrain = r.add(std::move(land), "test:island");
    scene.add(island, ground);

    // The sea, foaming at its crests and round a rock standing in it.
    MaterialAsset sea;
    sea.albedo           = {0.05f, 0.2f, 0.25f, 1.0f};
    sea.roughness        = 0.05f;
    sea.ior              = 1.33f;
    sea.subsurface       = 0.5f;
    sea.subsurfaceColor  = {0.1f, 0.5f, 0.45f};
    const EntityId water = place(
        scene,
        r.add(generatePlane({400.0f, 400.0f, 200, 200}), "test:sea"),
        add(r, "test:seawater", sea),
        {0.0f, 0.0f, 0.0f}
    );
    Water open;
    open.waves     = 1.0f;
    open.ripples   = 1.0f;
    open.depth     = 0.0f;
    open.foam      = 0.8f;
    open.waterline = 1.0f;
    scene.add(water, open);
    scene.environment().waves.shape.windSpeed = 10.0f;
    place(
        scene,
        shapes.cube,
        add(r, "test:stone", plain({0.4f, 0.4f, 0.38f}, 0.9f)),
        {6.0f, 0.0f, 20.0f},
        {2.0f, 3.0f, 2.0f}
    );

    // A pond in a stone basin up on the grass, running and rippling.
    const MaterialHandle stone = add(r, "test:basin", plain({0.5f, 0.5f, 0.48f}, 0.8f));
    const glm::vec3 pond = {-10.0f, 7.0f, 0.0f};
    place(scene, shapes.cube, stone, pond + glm::vec3(0.0f, -0.6f, 0.0f), {6.0f, 0.4f, 6.0f});
    for (const glm::vec3& side : {glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(-3.0f, 0.0f, 0.0f)}) {
        place(scene, shapes.cube, stone, pond + side, {0.4f, 1.6f, 6.4f});
    }
    for (const glm::vec3& side : {glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, -3.0f)}) {
        place(scene, shapes.cube, stone, pond + side, {6.4f, 1.6f, 0.4f});
    }
    const EntityId still = place(
        scene,
        r.add(generatePlane({5.8f, 5.8f, 16, 16}), "test:pond"),
        add(r, "test:pondwater", sea),
        pond + glm::vec3(0.0f, 0.3f, 0.0f)
    );
    Water running;
    running.ripples = 1.0f;
    running.flow    = 0.8f;
    running.depth   = 1.0f;
    scene.add(still, running);

    addSun(scene);
    const EntityId eye = addEye(scene, {4.0f, 24.0f, 34.0f}, {-8.0f, 4.0f, -10.0f});
    scene.get<Camera>(eye).zFar = 2000.0f;
    matchesReference(screen.shot(), "shore", screen.device);
}

TEST("a screen draws as it did: its UI's images, shapes, text, clipping and stacks over a held weapon and a level handing over") {
    Screen screen;
    Scene& scene = screen.scene();
    ResourceManager& r = screen.resources();
    const Shapes shapes(r);
    ensureDefaultUIFont(r, static_cast<int>(HEIGHT));
    addFloor(scene, shapes, add(r, "test:floor", plain({0.5f, 0.5f, 0.5f}, 0.8f)));
    addSun(scene);
    const EntityId eye = addEye(scene, {0.0f, 1.6f, 6.0f}, {0.0f, 1.0f, 0.0f});
    scene.get<Camera>(eye).viewmodelFovY = glm::radians(50.0f);

    // A level handing over, half way through its band: both levels show, each in its share of pixels.
    const EntityId handing = place(
        scene,
        shapes.cube,
        add(r, "test:lod", plain({0.8f, 0.6f, 0.1f}, 0.5f)),
        {2.0f, 1.0f, 0.0f},
        glm::vec3(2.0f)
    );
    LOD lod;
    const float distance = glm::length(glm::vec3(2.0f, 1.0f, 0.0f) - glm::vec3(0.0f, 1.6f, 6.0f));
    lod.levels = {{shapes.cube, 0.0f}, {shapes.sphere, 1000.0f}};
    // Its range, scaled as the cull scales a distance by the lens, ends just short of it.
    lod.levels[0].maxDistance = distance * (
        LOD::REFERENCE_P11 / (1.0f / std::tan(Camera{}.fovY / 2.0f))
    ) / 1.0375f;
    scene.add(handing, lod);

    // A weapon held low and right, through its own field of view.
    const EntityId held = place(
        scene,
        shapes.cube,
        add(r, "test:gun", plain({0.15f, 0.15f, 0.17f}, 0.3f, 1.0f)),
        {0.25f, 1.35f, 5.4f},
        {0.08f, 0.1f, 0.5f}
    );
    scene.get<Mesh>(held).seenBy = SeenBy::Viewmodel;

    // The UI: a picture, a rounded and bordered panel, text, a stack of three, and a panel clipping
    // its child.
    const EntityId hud = UI::canvas(scene, "test:hud");
    UIElement at;
    at.anchor   = at.pivot = {0.0f, 0.0f};
    at.position = {60.0f, 60.0f};
    at.size     = {300.0f, 300.0f};
    const EntityId picture = UI::panel(scene, hud, at, glm::vec4(1.0f));
    scene.get<UIImage>(picture).texture = texture(r, "test:icon", 64, TextureUsage::Color, [](
        uint32_t x,
        uint32_t y
    ) {
        return glm::vec4(x / 64.0f, y / 64.0f, 0.5f, 1.0f);
    });
    at.position = {420.0f, 60.0f};
    at.size     = {500.0f, 160.0f};
    const EntityId rounded = UI::panel(scene, hud, at, glm::vec4(0.1f, 0.2f, 0.5f, 0.9f));
    UIShape shape;
    shape.cornerRadius = 40.0f;
    shape.borderWidth  = 10.0f;
    shape.borderColor  = {1.0f, 0.8f, 0.2f, 1.0f};
    scene.add(rounded, shape);
    UIElement words;
    words.size = {480.0f, 140.0f};
    UI::Style big;
    big.textSize = 90.0f;
    UI::text(scene, rounded, words, "Score 1250", big);
    at.position = {1500.0f, 60.0f};
    at.size     = {300.0f, 500.0f};
    const EntityId stack = UI::column(scene, hud, at, 20.0f);
    UIElement entry;
    entry.size = {300.0f, 140.0f};
    const glm::vec4 colors[] = {
        glm::vec4(0.8f, 0.2f, 0.2f, 1.0f),
        glm::vec4(0.2f, 0.8f, 0.2f, 1.0f),
        glm::vec4(0.2f, 0.2f, 0.8f, 1.0f),
    };
    for (const glm::vec4& color : colors) {
        UI::panel(scene, stack, entry, color);
    }
    at.position = {60.0f, 700.0f};
    at.size     = {300.0f, 200.0f};
    const EntityId clip = UI::panel(scene, hud, at, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
    scene.get<UIElement>(clip).clipChildren = true;
    UIElement over;
    over.anchor   = over.pivot = {0.0f, 0.0f};
    over.position = {150.0f, 100.0f};
    over.size     = {300.0f, 300.0f};
    UI::panel(scene, clip, over, glm::vec4(0.9f, 0.1f, 0.6f, 1.0f));

    matchesReference(screen.shot(), "screen", screen.device);
}

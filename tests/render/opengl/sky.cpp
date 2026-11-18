// The sky: one from an image, what lights the scene and what stands behind it; the procedural one's
// air, as the sunlight crosses it.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <vector>

#include "vkm_opengl/core/context.h"

#include "convention/gl_bindings.h"
#include "offline/gl_air_baker.h"
#include "storage/light/gl_atmosphere.h"
#include "ecs/settings/environment.h"
#include "system/loading/loading_system.h"
#include "system/sky/atmosphere.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

namespace {

/// A Radiance HDR file of one color, 64 by 32, its rows written flat.
void writeSky(const std::filesystem::path& path, const glm::vec3& color) {
    constexpr int W = 64, H = 32;
    std::ofstream out(path, std::ios::binary);
    out << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " << H << " +X " << W << "\n";
    int exponent = 0;
    const float most  = std::max({color.r, color.g, color.b});
    const float scale = std::frexp(most, &exponent) * 256.0f / most;
    const uint8_t texel[4] = {static_cast<uint8_t>(color.r * scale), static_cast<uint8_t>(color.g * scale),
                              static_cast<uint8_t>(color.b * scale), static_cast<uint8_t>(exponent + 128)};
    for (int i = 0; i < W * H; ++i) out.write(reinterpret_cast<const char*>(texel), 4);
}

} // namespace

TEST("an HDR sky lights the scene in its colors and stands behind it, and another file changes both") {
    const std::filesystem::path root = openProject();
    writeSky(root / "warm.hdr", {0.5f, 0.2f, 0.05f});
    writeSky(root / "cool.hdr", {0.05f, 0.2f, 0.5f});

    Screen screen;
    Scene& scene = screen.scene();
    const Shapes shapes(screen.resources());
    scene.environment().sky.procedural = false;
    scene.environment().sky.hdrPath    = "warm.hdr";
    place(
        scene,
        shapes.sphere,
        material(screen.resources(), "test:white", glm::vec3(0.9f), 0.5f),
        glm::vec3(0.0f),
        glm::vec3(2.0f)
    );
    const EntityId eye = addEye(scene, {0.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 0.0f});

    const std::vector<Picture> frames = screen.shots({
        [&] { scene.environment().sky.hdrPath = "cool.hdr"; },
    }, 8);

    REQUIRE(frames.size() == 2u);
    const Rect ball = Rect::around(pixelOf(scene, eye, glm::vec3(0.0f)), 10);
    const Rect sky{0, 0, 40, 40};
    std::printf(
        "  warm: ball (%.0f %.0f %.0f), sky (%.0f %.0f %.0f); cool: ball (%.0f %.0f %.0f)\n",
        frames[0].mean(ball).r,
        frames[0].mean(ball).g,
        frames[0].mean(ball).b,
        frames[0].mean(sky).r,
        frames[0].mean(sky).g,
        frames[0].mean(sky).b,
        frames[1].mean(ball).r,
        frames[1].mean(ball).g,
        frames[1].mean(ball).b
    );
    for (const Rect& where : {ball, sky}) {
        CHECK(frames[0].mean(where).r > 1.5f * frames[0].mean(where).b + 10.0f);
        CHECK(frames[1].mean(where).b > 1.5f * frames[1].mean(where).r + 10.0f);
    }
}

TEST("a sky image newly named is decoded off the frame: the sky keeps its light until the image lands, then takes it") {
    const std::filesystem::path root = openProject();
    writeSky(root / "warm.hdr", {0.5f, 0.2f, 0.05f});
    writeSky(root / "cool.hdr", {0.05f, 0.2f, 0.5f});

    Screen screen;
    Scene& scene = screen.scene();
    const Shapes shapes(screen.resources());
    scene.environment().sky.procedural = false;
    scene.environment().sky.hdrPath    = "warm.hdr";
    place(
        scene,
        shapes.sphere,
        material(screen.resources(), "test:white", glm::vec3(0.9f), 0.5f),
        glm::vec3(0.0f),
        glm::vec3(2.0f)
    );
    const EntityId eye = addEye(scene, {0.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 0.0f});

    // The cool image named with every worker held: decoded on one, its frame draws the warm light; let
    // go, the image lands by the next frame, which draws the cool.
    std::optional<HeldWorkers> held;
    Picture named, landed;
    bool pending = false;
    screen.play(PAST_THE_IDENT + 2, [&](int frame) {
        const int after = frame - PAST_THE_IDENT;
        if (after == 0) {
            held.emplace();
            scene.environment().sky.hdrPath = "cool.hdr";
        }
        if (after == 1) {
            named   = screen.picture();
            pending = screen.readiness().skyPending;
            held->release();
        }
        if (after == 2) landed = screen.picture();
    }, 16);

    const Rect ball = Rect::around(pixelOf(scene, eye, glm::vec3(0.0f)), 10);
    const Rect sky{0, 0, 40, 40};
    CHECK(pending);
    for (const Rect& where : {ball, sky}) {
        CHECK(named.mean(where).r > 1.5f * named.mean(where).b + 10.0f);
        CHECK(landed.mean(where).b > 1.5f * landed.mean(where).r + 10.0f);
    }
}

TEST("a world that arrives naming a sky image is covered until its image is in, and shows under that sky") {
    const std::filesystem::path root = openProject();
    writeSky(root / "warm.hdr", {0.5f, 0.2f, 0.05f});
    writeSky(root / "cool.hdr", {0.05f, 0.2f, 0.5f});

    Screen screen;
    Scene& scene = screen.scene();
    const LoadingSystem& loading = screen.game.engine.addSystem<LoadingSystem>(SystemStage::Simulation);
    const Shapes shapes(screen.resources());
    EntityId eye;
    const auto build = [&](const char* sky) {
        scene.environment().sky.procedural = false;
        scene.environment().sky.hdrPath    = sky;
        place(
            scene,
            shapes.sphere,
            material(screen.resources(), "test:white", glm::vec3(0.9f), 0.5f),
            glm::vec3(0.0f),
            glm::vec3(2.0f)
        );
        eye = addEye(scene, {0.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 0.0f});
    };
    build("warm.hdr");

    // Another world arrives under the cool sky, every asset of it in, with the workers held across the
    // frames that follow, more than a world takes to be drawn whole behind the screen: it stays
    // covered while its sky's image cannot decode, and once let go, shows lit by that image.
    constexpr int HELD = 20;
    std::optional<HeldWorkers> held;
    int coveredHeld = 0;
    int shownAt     = 0;
    Picture shown;
    screen.play(PAST_THE_IDENT + HELD + 16, [&](int frame) {
        const int after = frame - PAST_THE_IDENT;
        if (after == 0) {
            held.emplace();
            scene.clear();
            build("cool.hdr");
        }
        if (after >= 1 && after <= HELD) coveredHeld += loading.loading() ? 1 : 0;
        if (after == HELD) held->release();
        if (after > HELD && shownAt == 0 && !loading.loading()) shownAt = after;
        if (after == HELD + 16) shown = screen.picture();
    }, 16);

    const Rect ball = Rect::around(pixelOf(scene, eye, glm::vec3(0.0f)), 10);
    const Rect sky{0, 0, 40, 40};
    CHECK(coveredHeld == HELD);
    CHECK(shownAt > 0);
    for (const Rect& where : {ball, sky}) {
        CHECK(shown.mean(where).b > 1.5f * shown.mean(where).r + 10.0f);
    }
}

TEST("the sky's transmittance table holds the sunlight's transmittance, the sun low and high") {
    // The table the sky is drawn from and the CPU's sunlight integrate one atmosphere: a term in one and
    // not the other shows as a sky and a sun disagreeing about the time of day.
    const Probes probes;
    Vkm::OpenGL::Context gl;
    GLAirBaker   air;
    GLAtmosphere atmosphere;
    Environment  env;
    air.bake(gl, atmosphere, Atmosphere::coefficients(env.sky));
    atmosphere.bindTransmittance(GLBindings::BakeTextureSlots::TRANSMITTANCE);

    // The elevations first, then the sun overhead, which the CPU's transmittance is relative to.
    const float elevations[] = {4.0f, 10.0f, 30.0f, 60.0f};
    std::vector<glm::vec4> rows(64, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    for (size_t i = 0; i < std::size(elevations); ++i) rows[i].x = std::sin(glm::radians(elevations[i]));
    const std::vector<glm::vec4> read = probes.run("sky_transmittance", rows);
    const glm::vec3 overhead(read.back());

    for (size_t i = 0; i < std::size(elevations); ++i) {
        env.sky.sunElevation = elevations[i];
        const glm::vec3 baked = glm::vec3(read[i]) / overhead;
        const glm::vec3 cpu   = Atmosphere::sunTransmittance(env.sky);
        std::printf(
            "  %4.0f degrees: baked %.3f %.3f %.3f, the CPU's %.3f %.3f %.3f\n",
            elevations[i],
            baked.r,
            baked.g,
            baked.b,
            cpu.r,
            cpu.g,
            cpu.b
        );
        for (int c = 0; c < 3; ++c) CHECK(std::abs(baked[c] - cpu[c]) <= 0.02f * cpu[c] + 0.002f);
    }
}

// Water as the frame draws it: a moving surface, whole, where gameplay finds it, rippled as its
// spectrum says.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "vkm_opengl/shader/compute_shader.h"

#include "frame/water/gl_waves.h"
#include "storage/water/gl_wave_fields.h"
#include "ecs/component/landscape/water.h"
#include "ecs/settings/wave_settings.h"
#include "ecs/water/wave_frame.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

TEST("a pond moved by the waves is drawn whole: no pixel of it lost between its depth and its color") {
    Screen screen;
    Scene& scene = screen.scene();
    MaterialAsset optics;
    optics.albedo          = {0.1f, 0.3f, 0.35f, 1.0f};
    optics.roughness       = 0.05f;
    optics.ior             = 1.33f;
    const EntityId pond = place(
        scene,
        screen.resources().add(generatePlane({30.0f, 30.0f, 160, 160}), "test:pond"),
        screen.resources().add(std::move(optics), "test:water"),
        glm::vec3(0.0f)
    );
    Water water;
    water.waves   = 1.0f;
    water.ripples = 1.0f;
    water.depth   = 0.0f;
    scene.add(pond, water);
    scene.environment().waves.shape.windSpeed = 12.0f;
    addSun(scene);
    addEye(scene, {0.0f, 2.5f, 12.0f}, {0.0f, 0.0f, 0.0f});

    const Picture frame = screen.shot();

    // The pond fills the frame's lower half; a pixel its color pass dropped stays black.
    int lost = 0;
    for (uint32_t y = HEIGHT / 2 + 20; y < HEIGHT; ++y) {
        for (uint32_t x = 0; x < WIDTH; ++x) lost += luminance(frame.at(x, y)) < 6.0f;
    }
    CHECK(lit(frame) > 0.95f);
    CHECK(lost == 0);
}

TEST("the GPU moves and turns the water where WaveSurface says, within a centimeter, at all and half of the waves") {
    // Gameplay floats things on WaveSurface's water and the GPU draws waves.glsl's: one sum stated twice.
    const Probes probes;
    WaveSettings storm;
    storm.shape.windSpeed  = 14.0f;
    storm.shape.choppiness = 1.0f;
    storm.calmCenter       = {5.0f, -5.0f};
    storm.calmRadius       = 30.0f;
    storm.stillRadius      = 90.0f;
    // Two surges over the points, one whole and one with its end among them.
    storm.surges[0] = {{-20.0f, 10.0f}, glm::normalize(glm::vec2(0.6f, 0.8f)), 1.4f, 1.5f, 12.0f};
    storm.surges[1] = {{40.0f, -30.0f}, {1.0f, 0.0f}, 0.8f, 1.0f, 20.0f};
    WaveSettings lake = storm;
    lake.shape.windSpeed = 0.0f;

    constexpr int POINTS = 256;
    std::vector<glm::vec4> rows(2 * POINTS, glm::vec4(0.0f));
    for (int i = 0; i < POINTS; ++i) {
        const float x = static_cast<float>(i) * 0.43f - 55.0f;
        const float z = std::fmod(static_cast<float>(i) * 7.9f, 120.0f) - 60.0f;
        rows[2 * i] = glm::vec4(x, z, 0.0f, 0.0f);
    }

    // How far the GPU's water is from the CPU's at worst, and its normal from the CPU's where it went.
    GLWaves      block;
    GLWaveFields fields;
    const auto apart = [&](const WaveFrame& waves, float share) {
        block.update(waves);
        fields.drawSwell(waves);
        const auto shared = [&](const Vkm::OpenGL::ComputeShader& probe) {
            probe.setUniform1f("u_share", share);
        };
        const std::vector<glm::vec4> read = probes.run("wave_surface", rows, shared);
        // A NaN the GPU gave is worst of all, where std::max would drop it.
        const auto worse = [](float most, float off) {
            return std::isnan(off) || off > most ? off : most;
        };
        glm::vec2 worst(0.0f);
        for (int i = 0; i < POINTS; ++i) {
            const glm::vec2 rest(rows[2 * i]);
            const glm::vec3 moved(read[2 * i]);
            const glm::vec3 normal(read[2 * i + 1]);
            const glm::vec2 over = rest + glm::vec2(moved.x, moved.z);
            worst.x = worse(worst.x, glm::length(moved - waves.surface.displacement(rest, share)));
            worst.y = worse(worst.y, glm::length(normal - waves.surface.normalAt(over, share)));
        }
        return worst;
    };

    WaveFrame sea;
    sea.surface = WaveSurface::at(storm, 1234.5);
    WaveFrame still;
    still.surface = WaveSurface::at(lake, 1234.5);
    const glm::vec2 whole = apart(sea, 1.0f);
    const glm::vec2 half  = apart(sea, 0.5f);
    const glm::vec2 calm  = apart(still, 1.0f);
    std::printf(
        "  waves %.2f m high: the GPU's water within %.4f m and %.4f of the CPU's, at half %.4f m and %.4f; "
        "windless, its surges within %.4f m\n",
        sea.surface.significantHeight(),
        whole.x,
        whole.y,
        half.x,
        half.y,
        calm.x
    );
    CHECK(sea.surface.significantHeight() > 1.0f);
    CHECK(whole.x < 0.01f);
    CHECK(whole.y < 0.01f);
    CHECK(half.x < 0.01f);
    CHECK(half.y < 0.01f);
    CHECK(!still.surface.swell);
    CHECK(calm.x < 0.01f);
}

TEST("the GPU draws the ripples from WaveSpectrum's density, in deep water and shallow") {
    // WaveSpectrum::density is stated again in ripples.glsl (rippleDensity), and the fields are drawn
    // from that statement: one density, wavenumber for wavenumber, with the wind and against it.
    const Probes probes;
    for (const float depth : {0.0f, 1.5f}) {
        WaveSettings settings;
        settings.shape.windSpeed     = 9.0f;
        settings.shape.windDirection = 40.0f;
        settings.shape.spread        = 0.4f;
        settings.shape.depth         = depth;
        settings.shape.heightScale   = 1.3f;
        WaveFrame waves;
        waves.surface = WaveSurface::at(settings, 3.0);
        waves.ripples = Ripples::at(settings, 3.0);
        GLWaves block;
        block.update(waves, true);

        // Sixteen wavenumbers across the band, in each of eight directions.
        constexpr int POINTS = 128;
        std::vector<glm::vec4> rows(POINTS);
        for (int i = 0; i < POINTS; ++i) {
            const float k     = waves.ripples.longest * std::pow(40.0f, static_cast<float>(i % 16) / 15.0f);
            const float angle = glm::two_pi<float>() * static_cast<float>(i / 16) / 8.0f + 0.3f;
            rows[i] = glm::vec4(k * std::cos(angle), k * std::sin(angle), 0.0f, 0.0f);
        }
        const std::vector<glm::vec4> read = probes.run("ripple_density", rows);

        float worst = 0.0f;
        for (int i = 0; i < POINTS; ++i) {
            const float cpu = waves.ripples.spectrum.density(glm::vec2(rows[i]));
            const float gpu = read[i].x;
            if (std::isnan(gpu)) worst = gpu;  // fails the check below, as std::max would not
            else if (cpu > 1e-12f) worst = std::max(worst, std::abs(gpu - cpu) / cpu);
            else if (gpu > 1e-12f) worst = 1.0f;
        }
        std::printf(
            "  %s water: the GPU's density within %.4f%% of the CPU's\n",
            depth > 0.0f ? "shallow" : "deep",
            100.0f * worst
        );
        CHECK(waves.ripples.any());
        CHECK(worst < 0.001f);
    }
}

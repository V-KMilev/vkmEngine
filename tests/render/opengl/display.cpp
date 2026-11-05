// How the frame is shown: its exposure, tone, grade, debug views, scale and the tiers a player picks.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <vector>

#include "core/graphics.h"
#include "core/render_mode.h"
#include "ecs/component/ui/ui_build.h"
#include "font_baker.h"
#include "system/render/exposure.h"
#include "system/render/screenshot.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

namespace {

/// The frame's settings with @p change made: what a player's menu writes.
template<typename Change>
std::function<void()> setting(Screen& screen, Change change) {
    return [&screen, change] {
        RenderSettings settings = screen.graphics().settings();
        change(settings);
        screen.graphics().set(settings);
    };
}

/**
 * @brief A grading strip of 16 squares, 256 by 16, written as a PNG: red to the right and blue from
 *        square to square, green up a square.
 *
 * @param path Where.
 * @param swap Red and blue swapped; else the identity.
 */
void writeStrip(const std::filesystem::path& path, bool swap) {
    constexpr uint32_t N = 16;
    std::vector<uint8_t> rgb(N * N * N * 3);
    const auto level = [](uint32_t i) { return static_cast<uint8_t>(i * 255 / (N - 1)); };
    for (uint32_t y = 0; y < N; ++y) {
        for (uint32_t x = 0; x < N * N; ++x) {
            const uint32_t r = x % N, g = N - 1 - y, b = x / N;
            uint8_t* texel   = rgb.data() + (static_cast<size_t>(y) * N * N + x) * 3;
            texel[0] = level(swap ? b : r);
            texel[1] = level(g);
            texel[2] = level(swap ? r : b);
        }
    }
    REQUIRE(writeScreenshotFile(path.string(), N * N, N, rgb));
}

} // namespace

TEST("a stop more exposure brightens the frame, and Hable draws unlike ACES") {
    Screen screen;
    yard(screen.scene(), screen.resources());

    const std::vector<Picture> frames = screen.shots({
        setting(screen, [](RenderSettings& s) { s.exposure += 1.0f; }),
        setting(screen, [](RenderSettings& s) { s.exposure -= 1.0f; s.tonemap = Tonemap::Hable; }),
    });

    REQUIRE(frames.size() == 3u);
    CHECK(luminance(frames[1].mean()) > 1.15f * luminance(frames[0].mean()));
    CHECK(differing(frames[0], frames[2]) > 0.2f);
}

TEST("a grading strip that swaps red and blue swaps them in the frame, and at strength 0 or as the identity changes nothing") {
    const std::filesystem::path root = openProject();
    writeStrip(root / "identity.png", false);
    writeStrip(root / "swap.png", true);
    writeStrip(root / "wide.png", false);
    // Not a strip of squares: refused, said, and the frame goes ungraded.
    REQUIRE(
        writeScreenshotFile((root / "wide.png").string(), 100, 16, std::vector<uint8_t>(100 * 16 * 3, 128))
    );

    Screen screen;
    const Yard seen = yard(screen.scene(), screen.resources());
    const std::vector<Picture> frames = screen.shots({
        setting(screen, [](RenderSettings& s) { s.gradingLut = "identity.png"; }),
        setting(screen, [](RenderSettings& s) { s.gradingLut = "swap.png"; }),
        setting(screen, [](RenderSettings& s) { s.gradingStrength = 0.0f; }),
        setting(screen, [](RenderSettings& s) { s.gradingLut = "wide.png"; s.gradingStrength = 1.0f; }),
    });

    REQUIRE(frames.size() == 5u);
    const Rect red    = Rect::around(pixelOf(screen.scene(), seen.eye, {-1.6f, 0.5f, 0.5f}), 4);
    const glm::vec3 a = frames[0].mean(red);
    const glm::vec3 b = frames[2].mean(red);
    CHECK(a.r > 2.0f * a.b);
    CHECK_NEAR(b.r, a.b, 12.0f);
    CHECK_NEAR(b.b, a.r, 12.0f);
    CHECK(differing(frames[0], frames[1]) < 0.005f);
    CHECK(differing(frames[0], frames[3]) < 0.001f);
    CHECK(differing(frames[0], frames[4]) < 0.001f);
    CHECK(countInLog("not a strip of N squares") == 1);
}

TEST("auto-exposure is off by default, so a dark scene draws dark, and on it draws a dark and a bright scene closer") {
    Screen screen;
    yard(screen.scene(), screen.resources());
    Environment& sky = screen.scene().environment();
    const float noon = sky.sky.sunElevation;
    CHECK(!screen.graphics().settings().autoExposure);

    // Long enough for the eye to adapt either way, a stop a second at the slowest.
    const std::vector<Picture> frames = screen.shots({
        [&] { sky.sky.sunElevation = -20.0f; },
        setting(screen, [](RenderSettings& s) { s.autoExposure = true; }),
        [&] { sky.sky.sunElevation = noon; },
    }, 40);

    REQUIRE(frames.size() == 4u);
    const float bright      = luminance(frames[0].mean()), dark = luminance(frames[1].mean());
    const float darkAdapted = luminance(frames[2].mean()), brightAdapted = luminance(frames[3].mean());
    std::printf(
        "  bright %.1f, dark %.1f; adapted: dark %.1f, bright %.1f\n",
        bright,
        dark,
        darkAdapted,
        brightAdapted
    );
    CHECK(dark < 0.25f * bright);
    CHECK(darkAdapted > 2.0f * dark);
    CHECK(brightAdapted / darkAdapted < 0.5f * bright / dark);
}

TEST("the GPU's histogram bins a luminance where Exposure reads the bin: each bin's center, both ends and no number") {
    // A bin the GPU counts in and the CPU reads otherwise moves the frame's exposure by a bin's stops.
    const Probes probes;
    std::vector<glm::vec4> rows;
    for (int bin = 1; bin < Exposure::BINS; ++bin) {
        rows.emplace_back(std::exp2(Exposure::binCenter(bin)), 0.0f, 0.0f, 0.0f);
    }
    const float ends[] = {
        0.0f,
        -1.0f,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        std::exp2(Exposure::MIN_LOG - 1.0f),
        std::exp2(Exposure::MIN_LOG + 0.01f),
        std::exp2(Exposure::MAX_LOG - 0.01f),
        std::exp2(Exposure::MAX_LOG + 1.0f),
    };
    for (const float brightness : ends) rows.emplace_back(brightness, 0.0f, 0.0f, 0.0f);
    rows.resize((rows.size() + 63) / 64 * 64, glm::vec4(0.0f));
    const std::vector<glm::vec4> read = probes.run("exposure_bins", rows);

    int apart = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const float brightness = rows[i].x;
        const int cpu = brightness > 0.0f ? Exposure::binOf(std::log2(brightness)) : 0;
        apart += static_cast<int>(read[i].y) != cpu ? 1 : 0;
    }
    std::printf("  %d of %zu luminances binned apart\n", apart, rows.size());
    CHECK(apart == 0);
}

TEST("each debug view draws unlike the picture and unlike every other") {
    Screen screen;
    Scene& scene = screen.scene();
    yard(scene, screen.resources());
    // Something for each view to show: a metal, an emitter, a lamp and fog.
    const Shapes shapes(screen.resources());
    place(
        scene,
        shapes.sphere,
        material(screen.resources(), "test:metal", {0.9f, 0.8f, 0.6f}, 0.2f, 1.0f),
        {-0.8f, 1.4f, 0.5f},
        glm::vec3(0.6f)
    );
    MaterialAsset glow;
    glow.albedo   = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    glow.emission = glm::vec3(8.0f, 6.0f, 2.0f);
    place(
        scene,
        shapes.sphere,
        screen.resources().add(std::move(glow), "test:glow"),
        {0.8f, 1.4f, 0.5f},
        glm::vec3(0.4f)
    );
    addLamp(scene, {0.0f, 1.0f, 1.5f}, {0.4f, 1.0f, 0.4f}, 20.0f, 4.0f);
    scene.environment().fog.enabled = true;

    std::vector<std::function<void()>> views;
    for (int mode = 1; mode < static_cast<int>(RenderMode::Count); ++mode) {
        views.push_back([&screen, mode] { screen.chrome().display.mode = static_cast<RenderMode>(mode); });
    }
    const std::vector<Picture> frames = screen.shots(views, 2);

    REQUIRE(frames.size() == static_cast<size_t>(RenderMode::Count));
    for (size_t i = 0; i < frames.size(); ++i) {
        for (size_t j = i + 1; j < frames.size(); ++j) {
            const float apart = differing(frames[i], frames[j]);
            if (apart < 0.01f) std::printf("  views %zu and %zu draw alike\n", i, j);
            CHECK(apart >= 0.01f);
        }
    }
}

TEST("half the render scale draws close to the full frame, and the UI at full size") {
    Screen screen;
    Scene& scene = screen.scene();
    yard(scene, screen.resources());
    ensureDefaultUIFont(screen.resources(), static_cast<int>(HEIGHT));
    const EntityId canvas = UI::canvas(scene, "test:hud");
    UIElement corner;
    corner.anchor   = {0.0f, 0.0f};
    corner.pivot    = {0.0f, 0.0f};
    corner.position = {40.0f, 40.0f};
    corner.size     = {400.0f, 120.0f};
    const EntityId panel = UI::panel(scene, canvas, corner, glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
    UIElement words;
    words.size = {380.0f, 100.0f};
    UI::text(scene, panel, words, "Scale");

    const std::vector<Picture> frames = screen.shots({
        setting(screen, [](RenderSettings& s) { s.renderScale = 0.5f; }),
    });

    REQUIRE(frames.size() == 2u);
    // The canvas is laid out at 1080 rows: the panel is the frame's top left, about 80 by 24 here.
    const Rect hud{0, 0, 96, 36};
    const Rect world{0, 60, WIDTH, HEIGHT};
    std::printf(
        "  UI apart %.4f; world apart %.4f, by %.2f levels\n",
        differing(frames[0], frames[1], 8, hud),
        differing(frames[0], frames[1], 8, world),
        meanApart(frames[0], frames[1], world)
    );
    CHECK(differing(frames[0], frames[1], 8, hud) < 0.002f);
    CHECK(differing(frames[0], frames[1], 8, world) > 0.01f);
    CHECK(meanApart(frames[0], frames[1], world) < 4.0f);
}

TEST("reflections traced at half resolution draw close to full") {
    Screen screen;
    const Yard seen = yard(screen.scene(), screen.resources(), 0.05f, glm::vec3(0.05f));

    const std::vector<Picture> frames = screen.shots({
        setting(screen, [](RenderSettings& s) { s.ssrHalfResolution = true; }),
        setting(screen, [](RenderSettings& s) { s.ssr = false; }),
    });

    REQUIRE(frames.size() == 3u);
    // The polished floor in front of each box, where it is mirrored.
    float half = 0.0f, none = 0.0f;
    for (const float x : {-1.6f, 1.6f}) {
        const Rect mirrored = Rect::around(pixelOf(screen.scene(), seen.eye, {x, 0.0f, 0.9f}), 6);
        half += meanApart(frames[0], frames[1], mirrored) / 2.0f;
        none += meanApart(frames[0], frames[2], mirrored) / 2.0f;
    }
    std::printf("  half resolution %.2f levels from full, none %.2f\n", half, none);
    CHECK(none > 10.0f);
    CHECK(half < 0.25f * none);
}

TEST("a quality tier draws what it sets: Performance without bloom, ambient occlusion and reflections, Max with them") {
    Screen screen;
    Scene& scene    = screen.scene();
    const Yard seen = yard(scene, screen.resources(), 0.05f);
    const Shapes shapes(screen.resources());
    MaterialAsset glow;
    glow.albedo   = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    glow.emission = glm::vec3(20.0f, 16.0f, 6.0f);
    place(
        scene,
        shapes.sphere,
        screen.resources().add(std::move(glow), "test:glow"),
        {0.0f, 1.6f, 0.0f},
        glm::vec3(0.3f)
    );

    // The ambient occlusion each tier draws, as its debug view shows it.
    const auto occlusion = [&] { screen.chrome().display.mode = RenderMode::AmbientOcclusion; };
    const auto picture   = [&] { screen.chrome().display.mode = RenderMode::Lit; };
    const std::vector<Picture> frames = screen.shots({
        [&] { screen.graphics().setQuality(GraphicsPreset::Max); },
        occlusion,
        [&] { picture(); screen.graphics().setQuality(GraphicsPreset::Performance); },
        occlusion,
    });

    REQUIRE(frames.size() == 5u);
    const Picture& max = frames[1];
    const Picture& low = frames[3];
    // Beside the emitter, where only its bloom reaches.
    const glm::vec2 glowAt = pixelOf(scene, seen.eye, {0.0f, 1.6f, 0.0f});
    const Rect halo = Rect::around(glowAt + glm::vec2(0.0f, -18.0f), 3);
    // The floor in front of the red box, which mirrors it.
    const Rect mirrored = Rect::around(pixelOf(scene, seen.eye, {-1.6f, 0.0f, 0.9f}), 4);
    // Where the floor meets the sphere, which ambient occlusion darkens.
    const Rect contact = Rect::around(pixelOf(scene, seen.eye, {0.0f, 0.0f, 0.45f}), 3);
    std::printf(
        "  halo %.1f / %.1f, mirrored red %.1f / %.1f, occlusion %.1f / %.1f\n",
        luminance(max.mean(halo)),
        luminance(low.mean(halo)),
        max.mean(mirrored).r - max.mean(mirrored).g,
        low.mean(mirrored).r - low.mean(mirrored).g,
        luminance(frames[2].mean(contact)),
        luminance(frames[4].mean(contact))
    );
    CHECK(luminance(max.mean(halo)) > luminance(low.mean(halo)) + 8.0f);
    CHECK(max.mean(mirrored).r - max.mean(mirrored).g > low.mean(mirrored).r - low.mean(mirrored).g + 8.0f);
    CHECK(luminance(frames[2].mean(contact)) < luminance(frames[4].mean(contact)) - 20.0f);
}

TEST("4x MSAA smooths a shape's edges and leaves its faces as they were") {
    Screen screen;
    Scene& scene = screen.scene();
    const Shapes shapes(screen.resources());
    // A white slab, turned so its edges run across the pixels, against the dark night sky.
    scene.environment().sky.sunElevation = -30.0f;
    MaterialAsset white;
    white.type   = MaterialType::Unlit;
    white.albedo = glm::vec4(1.0f);
    const glm::quat turned = glm::angleAxis(glm::radians(20.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    place(
        scene,
        shapes.cube,
        screen.resources().add(std::move(white), "test:white"),
        {0.0f, 2.0f, 0.0f},
        {2.0f, 2.0f, 0.2f},
        turned
    );
    const EntityId eye = addEye(scene, {0.0f, 2.0f, 5.0f}, {0.0f, 2.0f, 0.0f});
    RenderSettings plain;
    plain.bloom       = false;
    plain.msaaSamples = 1;
    screen.graphics().setProject(plain);

    const std::vector<Picture> frames = screen.shots({
        setting(screen, [](RenderSettings& s) { s.msaaSamples = 4; }),
    });

    REQUIRE(frames.size() == 2u);
    // Pixels part slab, part sky: few without, where each pixel is one or the other, and along every
    // edge with.
    const Rect around = Rect::around(pixelOf(scene, eye, {0.0f, 2.0f, 0.0f}), 90);
    const auto partial = [&](const Picture& p) {
        int found = 0;
        for (uint32_t y = around.y0; y < around.y1; ++y) {
            for (uint32_t x = around.x0; x < around.x1; ++x) {
                const float here = luminance(p.at(x, y));
                found += here > 40.0f && here < 200.0f;
            }
        }
        return found;
    };
    std::printf("  partial pixels %d without, %d with\n", partial(frames[0]), partial(frames[1]));
    CHECK(partial(frames[1]) > 3 * partial(frames[0]));
    CHECK(partial(frames[1]) > 100);
    CHECK(differing(
        frames[0],
        frames[1],
        4,
        Rect::around(pixelOf(scene, eye, {0.0f, 2.0f, 0.1f}), 10)
    ) < 0.01f);
}

TEST("an orthographic camera draws a near and a far box the same size, and a perspective one does not") {
    Screen screen;
    Scene& scene = screen.scene();
    const Shapes shapes(screen.resources());
    addFloor(scene, shapes, material(screen.resources(), "test:floor", {0.5f, 0.5f, 0.5f}));
    place(scene, shapes.cube, material(screen.resources(), "test:red", {0.9f, 0.05f, 0.05f}), {-1.5f, 1.0f, 2.0f});
    place(scene, shapes.cube, material(screen.resources(), "test:green", {0.05f, 0.8f, 0.05f}), {1.5f, 1.0f, -6.0f});
    addSun(scene);
    const EntityId eye = addEye(scene, {0.0f, 1.0f, 8.0f}, {0.0f, 1.0f, 0.0f});

    const std::vector<Picture> frames = screen.shots({
        [&] {
            Camera& lens = scene.get<Camera>(eye);
            lens.projection  = ProjectionType::Orthographic;
            lens.orthoHeight = 3.0f;
        },
    });

    REQUIRE(frames.size() == 2u);
    // Each box's width along its row, in pixels of its own color.
    const auto width = [](const Picture& p, bool red) {
        int most = 0;
        for (uint32_t y = 0; y < p.height; ++y) {
            int run = 0;
            for (uint32_t x = 0; x < p.width; ++x) {
                const glm::vec3 c = p.at(x, y);
                run += red ? (c.r > 2.0f * c.b && c.r > 2.0f * c.g) : (c.g > 1.4f * c.r && c.g > 1.4f * c.b);
            }
            most = std::max(most, run);
        }
        return most;
    };
    std::printf(
        "  perspective: %d and %d; orthographic: %d and %d\n",
        width(frames[0], true),
        width(frames[0], false),
        width(frames[1], true),
        width(frames[1], false)
    );
    CHECK(width(frames[0], true) > width(frames[0], false) + 10);
    CHECK(width(frames[1], true) > 10);
    CHECK(std::abs(width(frames[1], true) - width(frames[1], false)) <= 2);
}

TEST("two walls a centimeter apart a kilometer away draw the nearer whole") {
    Screen screen;
    Scene& scene = screen.scene();
    const Shapes shapes(screen.resources());
    // Their faces toward the eye 1 km out, the green one's 1 cm behind the red one's.
    place(
        scene,
        shapes.cube,
        material(screen.resources(), "test:red", {0.9f, 0.05f, 0.05f}, 1.0f),
        {0.0f, 0.0f, -1000.5f},
        {400.0f, 400.0f, 1.0f}
    );
    place(
        scene,
        shapes.cube,
        material(screen.resources(), "test:green", {0.05f, 0.9f, 0.05f}, 1.0f),
        {0.0f, 0.0f, -1000.51f},
        {400.0f, 400.0f, 1.0f}
    );
    addSun(scene);
    const EntityId eye = addEye(scene, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
    scene.get<Camera>(eye).zFar = 5000.0f;

    const Picture frame = screen.shot();

    // The walls' middle, well inside their edges.
    const Rect wall = Rect::around({WIDTH / 2.0f, HEIGHT / 2.0f}, 24);
    int green = 0, red = 0;
    for (uint32_t y = wall.y0; y < wall.y1; ++y) {
        for (uint32_t x = wall.x0; x < wall.x1; ++x) {
            const glm::vec3 c = frame.at(x, y);
            green += c.g > c.r;
            red   += c.r > 2.0f * c.g;
        }
    }
    std::printf("  red %d, green %d\n", red, green);
    CHECK(red == 49 * 49);
    CHECK(green == 0);
}

TEST("ambient occlusion switched back on is worked out that frame, not read from before it was off") {
    // A thousand frames a second, the eye still: the pass is worked out on one frame in five.
    Screen screen(1000);
    Scene& scene    = screen.scene();
    const Yard seen = yard(scene, screen.resources());
    screen.chrome().display.mode = RenderMode::AmbientOcclusion;
    const glm::vec3 places[] = {{0.0f, 0.5f, 0.0f}, {0.0f, 0.5f, 1.5f}};
    const auto contact = [&](int place) {
        return Rect::around(pixelOf(scene, seen.eye, places[place] + glm::vec3(0.0f, -0.5f, 0.45f)), 2);
    };
    const auto gtao = [&](bool on) {
        RenderSettings settings = screen.graphics().settings();
        settings.gtao = on;
        screen.graphics().set(settings);
    };

    // Past the ident a quarter second a frame; then, five times, each at another frame of the
    // pass's five: off, the sphere moved, on, and that frame looked at.
    int place = 0, checks = 0, stale = 0;
    screen.play(PAST_THE_IDENT + 5 * 8, [&](int frame) {
        const int after = frame - PAST_THE_IDENT;
        if (after < 0) {
            screen.game.clock().requestStep(249);
            return;
        }
        const int round = after / 8, step = after % 8;
        if (step == 0) gtao(false);
        if (step == 1) {
            place = 1 - place;
            scene.get<Transform>(seen.sphere).position = places[place];
        }
        if (step == 2 + round) gtao(true);
        if (step == 3 + round) {
            const Picture p = screen.picture();
            ++checks;
            stale += luminance(p.mean(contact(place))) >= luminance(p.mean(contact(1 - place)));
        }
    });

    CHECK(checks == 5);
    CHECK(stale == 0);
}

TEST("a world replaced in place has its own ambient occlusion from its first frame, not the last world's") {
    // A thousand frames a second, the eye still: the pass is worked out on one frame in five.
    Screen screen(1000);
    Scene& scene = screen.scene();
    Yard seen    = yard(scene, screen.resources());
    screen.chrome().display.mode = RenderMode::AmbientOcclusion;
    const glm::vec3 places[] = {{0.0f, 0.5f, 0.0f}, {0.0f, 0.5f, 1.5f}};
    const auto contact = [&](int place) {
        return Rect::around(pixelOf(scene, seen.eye, places[place] + glm::vec3(0.0f, -0.5f, 0.45f)), 2);
    };

    // Past the ident a quarter second a frame; then, five times, each at another frame of the pass's
    // five: the world replaced by one with the sphere moved, and its first frame looked at.
    int place = 0, checks = 0, stale = 0;
    screen.play(PAST_THE_IDENT + 5 * 8, [&](int frame) {
        const int after = frame - PAST_THE_IDENT;
        if (after < 0) {
            screen.game.clock().requestStep(249);
            return;
        }
        const int round = after / 8, step = after % 8;
        if (step == round) {
            place = 1 - place;
            scene.clear();
            seen = yard(scene, screen.resources());
            scene.get<Transform>(seen.sphere).position = places[place];
        }
        if (step == round + 1) {
            const Picture p = screen.picture();
            ++checks;
            stale += luminance(p.mean(contact(place))) >= luminance(p.mean(contact(1 - place)));
        }
    });

    CHECK(checks == 5);
    CHECK(stale == 0);
}

// What a game shows first: the engine's ident, over its notice, then the project's own logos, each
// made off the frame and timed from when it is first drawn.

#include <map>

#include "io/project/roots.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

namespace {

// Ticks a frame that make it a quarter second: the most a splash steps a frame, so a frame stepped
// while the world is held for its programs steps it no further than one that is not.
constexpr int QUARTER = 15;

/**
 * @brief Run @p frames frames of a quarter second, from the first; @p each sees each with the splash's
 *        seconds since its images were drawn, or below 0 before.
 */
void runSplash(Screen& screen, int frames, const std::function<void(int frame, float seconds)>& each) {
    int landed = 0;
    bool owed  = false;
    screen.game.run(frames, [&](int frame) {
        const bool pending = screen.readiness().splashPending;
        owed = owed || pending;
        if (owed && !pending && landed == 0) landed = frame;
        each(frame, landed ? 0.25f * static_cast<float>(frame - landed) : -1.0f);
    }, QUARTER);
}

/// The middle of the frame, where a logo is drawn.
const Rect MIDDLE{WIDTH / 2 - 60, HEIGHT / 2 - 40, WIDTH / 2 + 60, HEIGHT / 2 + 40};

} // namespace

TEST("the engine's ident is drawn the same at each moment: its comet, its strike at six seconds, its caption after") {
    Screen screen;
    // No camera, so no world: the ident alone.
    const std::map<float, std::string> moments = {
        {0.5f, "ident_0.5"}, {3.0f, "ident_3"}, {5.75f, "ident_5.75"}, {6.0f, "ident_6"},
        {6.25f, "ident_6.25"}, {7.0f, "ident_7"}, {9.75f, "ident_9.75"},
    };
    std::map<float, Picture> taken;
    runSplash(screen, 4000, [&](int, float seconds) {
        if (moments.count(seconds)) taken[seconds] = screen.picture();
        if (seconds >= 10.0f) screen.game.engine.getWindow().requestClose();
    });

    REQUIRE(taken.size() == moments.size());
    // Its caption, the notice along the bottom: dark until the strike, whole a second after it.
    const Rect caption{WIDTH / 2 - 80, HEIGHT - 16, WIDTH / 2 + 80, HEIGHT - 6};
    std::printf(
        "  caption at 5.75 s %.1f, at 7 s %.1f\n",
        luminance(taken[5.75f].mean(caption)),
        luminance(taken[7.0f].mean(caption))
    );
    CHECK(luminance(taken[5.75f].mean(caption)) < 1.0f);
    CHECK(luminance(taken[7.0f].mean(caption)) > 4.0f);
    CHECK(lit(taken[7.0f]) > 0.01f);
    for (const auto& [seconds, name] : moments) matchesReference(taken[seconds], name, screen.device);
}

TEST("the ident covers the world while it shows, and the world shows after it") {
    Screen screen;
    yard(screen.scene(), screen.resources());
    Picture during, after;
    bool drawable = false;
    runSplash(screen, 4000, [&](int, float seconds) {
        if (seconds == 7.0f) {
            during   = screen.picture();
            drawable = !screen.readiness().compiling();
        }
        if (seconds == 11.0f) {
            after = screen.picture();
            screen.game.engine.getWindow().requestClose();
        }
    });

    REQUIRE(after.width == WIDTH);
    if (!drawable) std::printf("  the world's programs were still compiling under the ident\n");
    const Rect sky{0, 0, WIDTH, 12};
    CHECK(luminance(during.mean(MIDDLE)) > 5.0f);
    CHECK(luminance(during.mean(sky)) < 2.0f);
    CHECK(after.mean(sky).b > 60.0f);
}

TEST("a splash's images are made off the frame: its first frame is black and owes them, then its logo shows") {
    Screen screen;
    bool firstOwed = false, firstBlack = false;
    int landedAt   = 0;
    Picture shown;
    runSplash(screen, 4000, [&](int frame, float seconds) {
        if (frame == 1) {
            firstOwed  = screen.readiness().splashPending;
            firstBlack = lit(screen.picture()) == 0.0f;
        }
        if (seconds == 0.0f) landedAt = frame;
        if (seconds == 7.0f) {
            shown = screen.picture();
            screen.game.engine.getWindow().requestClose();
        }
    });

    std::printf("  the images landed on frame %d\n", landedAt);
    CHECK(firstOwed);
    CHECK(firstBlack);
    CHECK(landedAt > 1);
    CHECK(lit(shown) > 0.01f);
}

TEST("a project's splash that cannot be decoded is said once, owes nothing and draws black") {
    const std::filesystem::path root = openProject();
    writeText(root / "logo.png", "not a picture");
    Screen screen;
    screen.game.systems.splash.add((root / "logo.png").string(), 2.0f);

    bool owedAfter  = true;
    float litDuring = 1.0f;
    // The ident's ten seconds, then a second into the logo's span.
    runSplash(screen, 4000, [&](int, float seconds) {
        if (seconds == 11.5f) {
            owedAfter = screen.readiness().splashPending;
            litDuring = lit(screen.picture());
            screen.game.engine.getWindow().requestClose();
        }
    });

    CHECK(!owedAfter);
    CHECK(litDuring == 0.0f);
    CHECK(countInLog("logo.png': could not be decoded") == 1);
}

TEST("a project's plain splash shows its logo and caption whole while it holds") {
    Screen screen;
    const std::filesystem::path logos = Roots::engineAssets() / "logo";
    screen.game.systems.splash.add(
        (logos / "vkm_engine_logo_mono.png").string(),
        2.0f,
        (logos / "vkm_engine_notice.png").string()
    );
    Picture held;
    // A second's fade in after the ident's ten, then the middle of its hold.
    runSplash(screen, 4000, [&](int, float seconds) {
        if (seconds == 12.0f) {
            held = screen.picture();
            screen.game.engine.getWindow().requestClose();
        }
    });

    REQUIRE(held.width == WIDTH);
    CHECK(lit(held) > 0.01f);
    matchesReference(held, "splash", screen.device);
}

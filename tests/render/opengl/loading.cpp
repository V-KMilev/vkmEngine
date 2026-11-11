// The loading screen a windowed host puts over a world that is not ready to show.

#include <cstdint>
#include <cstdio>
#include <optional>

#include "gl_backend.h"
#include "resource/asset/texture_asset.h"
#include "system/loading/loading_system.h"

#include "harness/check.h"
#include "render/opengl/gpu.h"
#include "render/opengl/scenery.h"

using namespace Vkm::Test;

TEST("a world that arrives is covered by the loading screen's water and bar until it is ready, then shown") {
    Screen screen;
    Scene& scene = screen.scene();
    // As a host with a window stands it up; with none there is nothing to cover.
    screen.game.engine.addSystem<LoadingSystem>(SystemStage::Simulation);
    const Yard first   = yard(scene, screen.resources());
    const Rect red     = Rect::around(pixelOf(scene, first.eye, {-1.6f, 0.5f, 0.5f}), 4);
    const auto reddish = [&](const Picture& p) { return p.mean(red).r > 2.0f * p.mean(red).b; };

    // The first world shown; then another arrives in its place, the same yard.
    Picture before, arrived, shown;
    screen.play(PAST_THE_IDENT + 12, [&](int frame) {
        const int after = frame - PAST_THE_IDENT;
        if (after == 0) {
            before = screen.picture();
            scene.clear();
            yard(scene, screen.resources());
        }
        if (after == 1) arrived = screen.picture();
        if (after == 12) shown = screen.picture();
    }, 16);

    std::printf("  covered: %.3f of the frame unlike the world\n", differing(arrived, before));
    CHECK(reddish(before));
    CHECK(!reddish(arrived));
    CHECK(differing(arrived, before) > 0.5f);
    CHECK(lit(arrived) > 0.5f);
    CHECK(reddish(shown));
    CHECK(differing(shown, before) < 0.002f);
}

TEST("a frame the loading screen covers ends its shadow cull with it, before the next rewrites what the cull reads") {
    Screen screen;
    Scene& scene = screen.scene();
    screen.game.engine.addSystem<LoadingSystem>(SystemStage::Simulation);
    yard(scene, screen.resources());

    // Another world arrives, with the workers held across the frame it arrives in, and a texture of it
    // still decoding: covered and not yet whole, that frame neither warms the world nor runs a shadow
    // pass to join its caster cull. Once it is drawn the harness rewrites what it drew from, and the
    // workers go: a cull still queued would read the rewrite.
    Picture before, arrived, shown;
    std::optional<HeldWorkers> held;
    TextureHandle decoding;
    screen.play(PAST_THE_IDENT + 12, [&](int frame) {
        const int after = frame - PAST_THE_IDENT;
        if (after == 0) {
            before = screen.picture();
            held.emplace();
            scene.clear();
            yard(scene, screen.resources());
            TextureAsset texture;
            texture.loading = true;
            decoding        = screen.resources().add(std::move(texture), "test:decoding");
        }
        if (after == 1) {
            arrived = screen.picture();
            held->release();
            screen.resources().edit(decoding).loading = false;
        }
        if (after == 12) shown = screen.picture();
    }, 16);

    CHECK(differing(arrived, before) > 0.5f);
    CHECK(differing(shown, before) < 0.002f);
}

TEST("a world that arrives is drawn whole behind the loading screen before it shows") {
    Screen screen;
    Scene& scene = screen.scene();
    screen.game.engine.addSystem<LoadingSystem>(SystemStage::Simulation);
    yard(scene, screen.resources());

    // Another world arrives, the same yard: the frame it first shows as the first did, and whether
    // the backend had drawn all of it behind the screen by then, so that frame builds no program.
    Picture before;
    int  shownAt = 0;
    bool warmed  = false;
    screen.play(PAST_THE_IDENT + 40, [&](int frame) {
        const int after = frame - PAST_THE_IDENT;
        if (after == 0) {
            before = screen.picture();
            scene.clear();
            yard(scene, screen.resources());
        }
        if (after < 1 || shownAt > 0 || differing(screen.picture(), before) >= 0.002f) return;
        shownAt = after;
        warmed  = screen.gl().warmedWhole();
    }, 16);

    REQUIRE(shownAt > 0);
    CHECK(warmed);
}

TEST("a world drawn whole behind the loading screen keeps its sun's held shadows through the frames the screen still covers") {
    Screen screen;
    Scene& scene = screen.scene();
    const LoadingSystem& loading = screen.game.engine.addSystem<LoadingSystem>(SystemStage::Simulation);
    bakedRoom(scene, screen.resources());

    // Another world arrives, the same room: drawn whole behind the screen, which goes on covering its
    // bakes, then shown. Nothing moves, so the first frame it shows in draws nothing of the held layers
    // again. The screen covers the frame it says it is done in, and lifts the next.
    int coveredWhole = 0;
    bool done  = false;
    bool shown = false;
    uint64_t redrawn = 0;
    screen.play(PAST_THE_IDENT + 200, [&](int frame) {
        const int after = frame - PAST_THE_IDENT;
        if (after == 0) {
            scene.clear();
            bakedRoom(scene, screen.resources());
        }
        if (after < 1 || shown) return;
        if (loading.loading()) {
            coveredWhole += screen.gl().warmedWhole() ? 1 : 0;
            return;
        }
        if (!done) {
            done = true;
            return;
        }
        for (const ShadowHeldJob& job : screen.gl().shadowPlan().jobsHeld()) {
            redrawn += static_cast<uint64_t>(job.rect.width) * job.rect.height;
        }
        shown = true;
    }, 16);

    REQUIRE(shown);
    std::printf(
        "  %d frames covered once drawn whole; the first shown draws %llu texels of its held layers again\n",
        coveredWhole,
        static_cast<unsigned long long>(redrawn)
    );
    CHECK(coveredWhole > 0);
    CHECK(redrawn == 0u);
}

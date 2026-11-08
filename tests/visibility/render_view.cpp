// What a frame hands the render backend (RenderView): what it gathers through the camera, emptied
// with none, and the other systems' products it borrows rather than copies.

#include <memory>
#include <vector>

#include "core/frame/loading_frame.h"
#include "core/frame/render_readiness.h"
#include "core/frame/splash_frame.h"
#include "ecs/component/effect/particle_emitter.h"
#include "ecs/component/light/irradiance_volume.h"
#include "ecs/component/light/light.h"
#include "ecs/component/render/camera.h"
#include "system/render/render_backend.h"
#include "system/render/render_view.h"

#include "animation/rig.h"
#include "harness/check.h"
#include "harness/watch.h"
#include "harness/world.h"

using namespace Vkm::Test;
using namespace Vkm::Platform;

namespace {

/**
 * @brief A backend that draws nothing and keeps what each frame handed it.
 */
class Recorder : public RenderBackend {
    public:
        struct Seen {
            bool                  hasCamera   = false;
            glm::vec3             eye{0.0f};
            size_t                lights      = 0, particles = 0, decals = 0, probes = 0;
            bool                  volume      = false, skinned = false, debug = false;
            const RenderSettings* settings    = nullptr;
            const Environment*    environment = nullptr;
            const UIDrawData*     ui          = nullptr;
            const UIDrawData*     cover       = nullptr;
            const SplashFrame*    splash      = nullptr;
            const LoadingFrame*   loading     = nullptr;
            const RenderObjects*  objects     = nullptr;
            uint64_t              worldEpoch  = 0;
        };

        explicit Recorder(std::vector<Seen>& frames) : m_frames(frames) {}

        bool init(WindowManager&) override { return true; }
        void render(const RenderView& view, const ResourceManager&) override {
            const RenderView::SceneLists& l = view.lists;
            m_frames.push_back({view.hasCamera, view.camera.position, l.lights.size(

            ), l.particlesAdditive.size(),
                                l.decals.size(

                                ), l.probes.size(), l.hasIrradianceVolume, l.skinMatrices != nullptr,
                                l.debug.lines != nullptr, view.settings, view.environment, view.ui, view.cover,
                                view.splash, view.loading, view.objects, view.worldEpoch});
        }
        bool readFrame(const RenderView&) override { return false; }
        bool takeFrame(FramePixels&, bool) override { return false; }
        uint32_t maxAnisotropy() const override { return 1; }
        bool holdsPixels(const Handle<TextureAsset>&, uint64_t) const override { return false; }
        EditorRenderHooks* editorHooks() override { return nullptr; }
        RenderReadiness readiness() const override { return {}; }

    private:
        std::vector<Seen>& m_frames;
};

} // namespace

TEST("a frame without a camera hands the backend empty lists, still passing on what it borrows") {
    Game game;
    Scene& scene = game.scene();
    std::vector<Recorder::Seen> frames;
    game.systems.render.setBackend(std::make_unique<Recorder>(frames), game.engine.getWindow());

    const EntityId eye = scene.createEntity();
    Transform pose;
    pose.position = {0.0f, 1.0f, 10.0f};
    scene.add(eye, pose);
    scene.add(eye, Camera{});
    // A lamp, a fire, a posed rig and a baked-light volume: something for every list.
    const EntityId lamp = scene.createEntity();
    scene.add(lamp, Transform{});
    scene.add(lamp, generateLight(LightType::Point));
    const EntityId fire = scene.createEntity();
    scene.add(fire, Transform{});
    ParticleEmitter flames;
    flames.rate = 120.0f;
    scene.add(fire, flames);
    addCharacter(scene, addRig(game.resources()), addTurn(game.resources(), "test:turn", 0.0f, 90.0f));
    const EntityId volume = scene.createEntity();
    scene.add(volume, Transform{});
    scene.add(volume, IrradianceVolume{});

    // The camera goes after frame 3; frames 4 and 5 have none.
    Watch& seen = watch(game);
    std::vector<bool> borrowed;
    game.run(5, [&](int frame) {
        const FrameContext& ctx    = *seen.frame;
        const Recorder::Seen& last = frames.back();
        borrowed.push_back(last.ui == ctx.ui && last.cover == ctx.cover && last.splash == ctx.splash
                           && last.loading == ctx.loading && last.settings == &ctx.graphics.settings()
                           && last.environment == &scene.environment() && last.worldEpoch == scene.epoch()
                           && last.objects == &ctx.visibility->objects);
        if (frame == 3) scene.get<Camera>(eye).active = false;
    });

    REQUIRE(frames.size() == 5);
    const Recorder::Seen& lit = frames[2];
    CHECK(lit.hasCamera);
    CHECK(lit.lights == 1);
    CHECK(lit.particles > 0);
    CHECK(lit.skinned);
    CHECK(lit.volume);
    const Recorder::Seen& dark = frames[4];
    CHECK(!dark.hasCamera);
    CHECK(dark.lights == 0);
    CHECK(dark.particles == 0);
    CHECK(!dark.skinned);
    CHECK(!dark.volume);
    CHECK(dark.ui != nullptr);
    // The camera stands as it last was: what reads it is never handed a zeroed one.
    CHECK(dark.eye == lit.eye);
    CHECK(borrowed == std::vector<bool>(5, true));
}

TEST("the loading screen's backdrop shows only while it does and no splash does, and covers the world only when whole") {
    RenderView view;
    LoadingFrame loading;
    SplashFrame splash;
    CHECK(!view.loadingBackdropShows());
    CHECK(!view.loadingCovers());
    CHECK(!view.splashShows());

    view.loading    = &loading;
    view.splash     = &splash;
    loading.opacity = 0.0f;
    CHECK(!view.loadingBackdropShows());
    CHECK(!view.loadingCovers());
    loading.opacity = 0.5f;
    CHECK(view.loadingBackdropShows());
    CHECK(!view.loadingCovers());
    loading.opacity = 1.0f;
    CHECK(view.loadingBackdropShows());
    CHECK(view.loadingCovers());

    // Over a splash: the screen still covers the world, but its backdrop waits.
    splash.key = "logo.png";
    CHECK(view.splashShows());
    CHECK(!view.loadingBackdropShows());
    CHECK(view.loadingCovers());
    splash.key.clear();
    CHECK(!view.splashShows());
    CHECK(view.loadingBackdropShows());
}

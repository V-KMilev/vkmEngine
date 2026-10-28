#define VKM_LOG_CATEGORY "LOADING"

#include "system/loading/loading_system.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "logger.h"

#include "core/clock.h"
#include "core/host_chrome.h"
#include "core/math/random.h"
#include "debug/engine_error_log.h"
#include "debug/profiler.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/scene.h"
#include "io/asset/asset_serializer.h"
#include "io/scene/prefab.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"
#include "system/async/async_loader_system.h"
#include "system/loading/loading_events.h"
#include "system/render/render_backend.h"
#include "system/splash/splash_frame.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief A status line naming a step and how far through it the load is.
 *
 * @param step  What is under way.
 * @param done  Its units done.
 * @param total Its units in all.
 * @return E.g. "Compiling shaders 34 / 80".
 */
std::string stepStatus(const char* step, uint32_t done, uint32_t total) {
    char line[64];
    std::snprintf(line, sizeof(line), "%s %u / %u", step, done, total);
    return line;
}

} // namespace

LoadingSystem::LoadingSystem() = default;

LoadingSystem::~LoadingSystem() = default;

void LoadingSystem::setScreen(const LoadingScreen& screen) {
    // An empty tip says nothing, so none is turned to.
    m_screen = screen;
    std::vector<std::string>& tips = m_screen.tips;
    tips.erase(std::remove(tips.begin(), tips.end(), std::string{}), tips.end());
    forgetBuiltScreen();
}

bool LoadingSystem::screenStale(const ResourceManager& resources) const {
    if (m_screenAssets != resources.epoch()) return true;
    bool gone = false;
    m_screenScene->forEach<UIImage>([&](EntityId, const UIImage& image) {
        gone = gone || (image.texture && !resources.isAlive(image.texture));
    });
    return gone;
}

void LoadingSystem::forgetBuiltScreen() {
    // Built again as it next shows, from the file as it is then.
    m_screenScene.reset();
    m_screenWaiting.clear();
    m_screenAssets       = 0;
    m_frame.screen       = nullptr;
    m_frame.screenAssets = 0;
}

void LoadingSystem::init(FrameContext& ctx) {
    m_events   = &ctx.events;
    m_expected = m_events->subscribe<LoadExpected>([this](const LoadExpected&) {
        m_expecting = true;
        m_asked     = true;
    });
}

void LoadingSystem::shutdown() {
    if (m_events) m_events->unsubscribe<LoadExpected>(m_expected);
    m_events = nullptr;
}

void LoadingSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("LoadingSystem");

    m_authoring = ctx.chrome.authoring();

    // A screen built against a graph swapped out, or with a picture removed under it - an editor's
    // Stop drops what its session loaded - is built again as it next shows, its pictures by name.
    if (m_screenScene && screenStale(ctx.resources)) forgetBuiltScreen();
    if (m_screenNamesGraph != ctx.resources.epoch()) m_screenNames.clear();

    // A world arrives: cut in at once, as the frame it arrived in already shows it half made. A
    // cover asked for is the one world's that follows: an open that brought none lets it go, or the
    // next world to arrive - a Stop's - would be covered for it.
    const uint64_t epoch = ctx.scene.epoch();
    const bool     asked = ctx.chrome.takeCoverNextWorld();
    if (!m_seen || epoch != m_epoch) {
        m_coverAsked     = asked;
        m_expecting      = false;
        m_covered        = 0;
        m_seen           = true;
        m_epoch          = epoch;
        m_loading        = true;
        m_frames         = 0;
        m_programsPeak   = 0;
        m_assetsPeak     = 0;
        m_bakesPeak      = 0;
        m_stalled        = 0.0f;
        m_opacity        = 1.0f;
        m_frame.progress = 0.0f;
    }
    // A load not asked for again since the last frame failed or was dropped. With no world loading,
    // nothing is under way: its bar was the load's, and would hold a host's notice up for good.
    if (m_expecting && !m_asked) {
        m_expecting = false;
        m_covered   = 0;
        if (!m_loading) m_frame.progress = 1.0f;
    }
    m_asked = false;

    if (m_expecting) {
        // Covered before the load, which then holds a frame of the cover rather than of the world.
        ++m_covered;
        m_opacity        = 1.0f;
        m_frame.progress = 0.0f;
        m_frame.status   = "Loading the world";
    } else if (m_loading) {
        ++m_frames;
        const uint32_t        assets    = static_cast<uint32_t>(assetsLoading(ctx.resources));
        const RenderReadiness render    = ctx.readiness ? *ctx.readiness : RenderReadiness{};
        const uint32_t        compiling = render.programs - std::min(render.compiled, render.programs);
        const uint32_t        owed      = render.total - std::min(render.done, render.total);
        // Until the renderer can draw a world there is none to show, authored or not.
        if (compiling > 0) m_coverAsked = true;
        m_programsPeak = std::max(m_programsPeak, compiling);
        m_assetsPeak   = std::max(m_assetsPeak, assets);
        m_bakesPeak    = std::max(m_bakesPeak, owed);

        // Steps done of the most seen owed, so the bar never goes back as a bake begins; empty
        // until a step is seen, as the renderer's first word on a world comes a frame late.
        const uint32_t peak = m_programsPeak + m_assetsPeak + m_bakesPeak;
        const uint32_t left = compiling + assets + owed;
        const float    done = peak > 0 ? 1.0f - static_cast<float>(left) / static_cast<float>(peak) : 0.0f;
        m_frame.progress = std::max(m_frame.progress, done);

        // The step under way, and how far through it.
        if (compiling > 0) {
            m_frame.status = stepStatus("Compiling shaders", render.compiled, render.programs);
        } else if (assets > 0) {
            m_frame.status = stepStatus("Loading assets", m_assetsPeak - assets, m_assetsPeak);
        } else if (owed > 0) {
            m_frame.status = stepStatus("Lighting the world", m_bakesPeak - owed, m_bakesPeak);
        } else {
            m_frame.status = "Ready";
        }

        // Bounded by progress, not time: a world whose steps stop coming - a fixed light that
        // never holds still for its lightmap, say - is shown rather than covered for good.
        // A compile always ends, and until it does the backend has no world to show.
        m_stalled = left == m_left ? m_stalled + ctx.clock.getDeltaTime() : 0.0f;
        m_left    = left;
        const bool stalled = compiling == 0 && left > 0 && m_stalled >= m_patience;
        if (stalled) {
            LOG_WARNING(
                "The loading screen lifts on a world not ready: %u assets decoding and %u bake "
                "steps owed, none done for %g seconds",
                assets,
                owed,
                static_cast<double>(m_patience)
            );
        }

        if (m_frames >= MIN_FRAMES && (left == 0 || stalled)) {
            m_loading        = false;
            m_frame.progress = 1.0f;
        }
    } else {
        m_opacity = std::max(m_opacity - ctx.clock.getDeltaTime() / FADE_SECONDS, 0.0f);
    }

    // A splash is its own screen: the loading screen's content comes in once it has gone
    // (LoadingFrame::content).
    const bool splashing = ctx.splash && ctx.splash->isShowing();
    m_content = splashing ? 0.0f : std::min(m_content + ctx.clock.getDeltaTime() / CONTENT_SECONDS, 1.0f);
    m_frame.content  = m_content;
    m_frame.opacity  = m_authoring && !m_coverAsked ? 0.0f : m_opacity;
    m_frame.covering = m_authoring || m_covered >= COVERED_FRAMES;
    const bool showing = m_frame.isShowing();
    if (showing) showScreen(ctx, !m_shown);
    m_shown     = showing;
    ctx.loading = &m_frame;
}

void LoadingSystem::showScreen(FrameContext& ctx, bool fresh) {
    // A tip in turn, fading between them; one alone holds.
    const std::vector<std::string>& tips = m_screen.tips;
    if (tips.empty()) {
        m_frame.tip.clear();
    } else {
        const bool turns = tips.size() > 1;
        m_tipSeconds = fresh ? 0.0f : m_tipSeconds + ctx.clock.getDeltaTime();
        if (fresh) m_tip = static_cast<size_t>(Math::Random::range(0, static_cast<int>(tips.size()) - 1));
        if (turns && m_tipSeconds >= TIP_SECONDS) {
            m_tipSeconds = 0.0f;
            ++m_tip;
        }
        m_tip %= tips.size();
        const float in   = m_tipSeconds / TIP_FADE;
        const float out  = turns ? (TIP_SECONDS - m_tipSeconds) / TIP_FADE : 1.0f;
        m_frame.tip      = tips[m_tip];
        m_frame.tipShown = std::clamp(std::min(in, out), 0.0f, 1.0f);
    }

    // Built against the assets of the moment: a world's that swaps them in leaves its handles stale.
    if (!m_screen.screen.empty() && m_screenAssets != ctx.resources.epoch()) {
        m_screenAssets = ctx.resources.epoch();
        m_screenScene  = std::make_unique<Scene>();
        m_screenWaiting.clear();
        if (!Prefab::instantiate(*m_screenScene, ctx.resources, m_screen.screen)) {
            const std::string source = "loading screen '" + m_screen.screen + "'";
            reportError("Loading", source, "did not build; shown without it");
            m_screenScene.reset();
        } else {
            const nlohmann::json named = AssetSerializer::saveAssetsForScene(*m_screenScene, ctx.resources);
            m_screenNames              = AssetSerializer::namesIn(named);
            m_screenNamesGraph         = ctx.resources.epoch();
        }
    }
    // A picture of it shows once it has decoded: one still loading would draw as the missing-texture
    // checker. Its tint is clear meanwhile, a gradient's bottom edge too, so what it holds and the
    // rest of the screen still show.
    if (m_screenScene) {
        m_screenScene->forEach<UIImage>([&](EntityId id, UIImage& image) {
            const bool alive   = image.texture && ctx.resources.isAlive(image.texture);
            const bool loading = alive && ctx.resources.get(image.texture).loading;
            const auto waiting = std::find_if(
                m_screenWaiting.begin(),
                m_screenWaiting.end(),
                [id](const Waiting& held) { return held.picture == id; }
            );
            if (loading && waiting == m_screenWaiting.end()) {
                m_screenWaiting.push_back({id, image.color.a, image.shape.bottomColor.a});
                image.color.a             = 0.0f;
                image.shape.bottomColor.a = 0.0f;
            } else if (!loading && waiting != m_screenWaiting.end()) {
                image.color.a             = waiting->alpha;
                image.shape.bottomColor.a = waiting->bottomAlpha;
                m_screenWaiting.erase(waiting);
            }
        });
    }
    m_frame.screen       = m_screenScene.get();
    m_frame.screenAssets = m_screenAssets;
    m_frame.water        = m_screen.water;
}

} // namespace Vkm::Engine

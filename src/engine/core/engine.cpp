#define VKM_LOG_CATEGORY "CORE"

#include "core/engine.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "logger.h"

#include "core/engine_config.h"
#include "debug/profiler.h"
#include "platform/threading/render_thread.h"
#include "system/render/render_system.h"

namespace Engine {

Engine::Engine()  = default;
Engine::~Engine() = default;

namespace {

constexpr const char* STAGE_NAMES[] = {
    "Input", "Simulation", "Transform", "Visibility", "Render", "UI"
};

} // namespace

void Engine::run() {
    float accumulator = 0.0f;

    LOG_TRACE("Entering main loop");

    while (m_window.beginFrame()) {
        const float deltaTime = m_frameTracker.getFrameRateInfo().frameTime / 1000.0f;

        if (!m_window.updateInput()) break;

        // The editor (if attached) writes the scene viewport rect to
        // WindowManager each frame. Without an editor, the 3D pipeline
        // sees the full window. A zero size in either dimension means
        // "no editor reported a rect this frame" -> use the window.
        const uint32_t winW = static_cast<uint32_t>(m_window.getWidth());
        const uint32_t winH = static_cast<uint32_t>(m_window.getHeight());
        const uint32_t vpW  = m_window.sceneViewportWidth();
        const uint32_t vpH  = m_window.sceneViewportHeight();
        const bool     vpSet = (vpW > 0 && vpH > 0);

        FrameContext ctx{
            m_scene, m_resources, m_window, m_frameTracker,
            deltaTime,
            Config::FIXED_TIME_STEP,
            vpSet ? m_window.sceneViewportX() : 0u,
            vpSet ? m_window.sceneViewportY() : 0u,
            vpSet ? vpW : winW,
            vpSet ? vpH : winH,
            nullptr
        };

        if (!m_initialized) {
            initSystems(ctx);
        }

        const float beforeClamp = accumulator + deltaTime;
        accumulator = std::min(beforeClamp, Config::MAX_FRAME_ACCUMULATOR);
        if (beforeClamp > Config::MAX_FRAME_ACCUMULATOR) {
            // Sustained slow frames would spam this every tick. Rate-limit
            // to one warning per second of wall clock; on the throttled
            // frames just bump a counter and emit a summary on the next
            // un-throttled warning.
            const auto now = std::chrono::steady_clock::now();
            if (now - m_lastAccumClampWarn >= std::chrono::seconds(1)) {
                if (m_accumClampSuppressed > 0) {
                    LOG_WARNING("Frame accumulator clamped (%.3fs > %.3fs cap, +%d similar in the last second) - spiral-of-death guard fired; some fixed ticks dropped",
                        beforeClamp, Config::MAX_FRAME_ACCUMULATOR, m_accumClampSuppressed);
                } else {
                    LOG_WARNING("Frame accumulator clamped (%.3fs > %.3fs cap) - spiral-of-death guard fired; some fixed ticks dropped",
                        beforeClamp, Config::MAX_FRAME_ACCUMULATOR);
                }
                m_accumClampSuppressed = 0;
                m_lastAccumClampWarn = now;
            } else {
                ++m_accumClampSuppressed;
            }
        }
        while (accumulator >= Config::FIXED_TIME_STEP) {
            PROFILE_SCOPE("FixedUpdate");
            for (System* system : m_fixedUpdaters) {
                if (system->isEnabled()) {
                    system->fixedUpdate(ctx);
                }
            }
            accumulator -= Config::FIXED_TIME_STEP;
        }

        // First frame after init: migrate the backend context to a
        // dedicated render thread + locate the RenderSystem so the
        // overlap loop can split its buildView / executeFrame phases.
        // Done here rather than before the loop so initSystems (and any
        // backend work it triggers, like the IBL bake's first execute)
        // still sees the context on the main thread.
        if (!m_renderThread) {
            const size_t renderIdx = static_cast<size_t>(SystemStage::Render);
            for (auto& system : m_systemsByStage[renderIdx]) {
                if (auto* rs = dynamic_cast<RenderSystem*>(system.get())) {
                    m_renderSystem = rs;
                    break;
                }
            }
            if (!m_renderSystem) {
                LOG_FATAL("No RenderSystem registered; engine requires one.");
                throw std::runtime_error("Engine::run: no RenderSystem registered");
            }
            // Cache UI-stage systems with backend-side companion work
            // (currently just EditorSystem). Their executeBackend() runs
            // on the render thread after the scene render finishes, so
            // the UI overlays the rendered scene.
            const size_t uiIdx = static_cast<size_t>(SystemStage::UI);
            for (auto& system : m_systemsByStage[uiIdx]) {
                if (system->hasBackendWork()) m_backendWorkSystems.push_back(system.get());
            }
            m_renderThread = std::make_unique<RenderThread>(m_window.getWindowContext());
        }

        // Frame K layout (overlapping with render K-1):
        //   pre-wait phase  (non-mutator systems, all stages)   <- overlaps
        //   buildView K     (RenderSystem; writes m_views[K&1]) <- overlaps
        //   waitForFrame    (block until render K-1 is done)    <-- sync point
        //   post-wait phase (mutator systems, all stages)
        //   postFrame K     (render thread reads m_views[K&1])
        // The render thread runs RenderSystem::executeFrame against the
        // buffer index it was posted with - main has already moved on to
        // writing the OTHER buffer for frame K+1, so no race.
        constexpr size_t RENDER_IDX = static_cast<size_t>(SystemStage::Render);

        // UI stage IS included in runPhase. Systems with hasBackendWork()
        // do their build on main here; their executeBackend() is appended
        // to the render-thread lambda below.
        auto runPhase = [&](bool mutators) {
            for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
                if (s == RENDER_IDX) continue;  // RenderSystem is routed manually
                PROFILE_SCOPE_NAMED(STAGE_NAMES[s]);
                for (auto& sys : m_systemsByStage[s]) {
                    if (!sys->isEnabled()) continue;
                    if (sys->mutatesResources() != mutators) continue;
                    sys->update(ctx);
                }
            }
        };

        runPhase(/*mutators=*/false);
        m_renderSystem->buildView(ctx, m_renderFrameIndex);
        m_renderThread->waitForFrame();
        runPhase(/*mutators=*/true);

        // ctx is copied by value into the lambda so the next loop
        // iteration can mutate the local ctx without affecting the
        // render thread's snapshot. UI-stage backend-work systems run
        // AFTER executeFrame so the UI overlays the rendered scene.
        const uint32_t frameIdx = m_renderFrameIndex;
        m_renderThread->postFrame([this, ctx, frameIdx]() mutable {
            m_renderSystem->executeFrame(ctx, frameIdx);
            for (System* sys : m_backendWorkSystems) sys->executeBackend(ctx);
            m_window.swapBuffers();
        });
        ++m_renderFrameIndex;

        m_frameTracker.update();
        PROFILE_FRAME_MARK();
    }

    LOG_TRACE("Main loop exited, running shutdown");
    // Tear the render thread down before shutdownSystems so any backend
    // teardown in destructors finds the context current on main again.
    m_renderThread.reset();
    shutdownSystems();
}

void Engine::initSystems(FrameContext& ctx) {
    PROFILE_SCOPE("Engine::initSystems");

    // Init systems in registration order, top to bottom of every stage.
    // While we're at it, collect the systems that opt into fixedUpdate so
    // the per-tick loop only visits them instead of every registered system.
    m_fixedUpdaters.clear();
    size_t totalSystems = 0;
    for (auto& stage : m_systemsByStage) {
        for (auto& system : stage) {
            system->init(ctx);
            if (system->hasFixedUpdate()) m_fixedUpdaters.push_back(system.get());
            ++totalSystems;
        }
    }
    m_initialized = true;
    LOG_INFO("Initialized %zu system(s) across %zu stage(s); %zu opted into fixedUpdate",
        totalSystems, m_systemsByStage.size(), m_fixedUpdaters.size());
}

void Engine::shutdownSystems() {
    LOG_TRACE("Shutting down systems (reverse registration order)");
    // Reverse stage order, then reverse within each stage.
    for (auto stageIt = m_systemsByStage.rbegin(); stageIt != m_systemsByStage.rend(); ++stageIt) {
        for (auto it = stageIt->rbegin(); it != stageIt->rend(); ++it) {
            (*it)->shutdown();
        }
    }
}

} // namespace Engine

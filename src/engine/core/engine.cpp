#define VKM_LOG_CATEGORY "CORE"

#include "core/engine.h"

#include <algorithm>
#include <chrono>

#include "logger.h"

#include "core/engine_config.h"
#include "debug/profiler.h"

namespace Engine {

Engine::Engine()  = default;
Engine::~Engine() = default;

namespace {

constexpr const char* STAGE_NAMES[] = {
    "Input", "Simulation", "Transform", "Visibility", "Render", "UI"
};
static_assert(sizeof(STAGE_NAMES) / sizeof(STAGE_NAMES[0]) == static_cast<size_t>(SystemStage::Count),
              "STAGE_NAMES must stay in sync with SystemStage");

} // namespace

void Engine::run() {
    float accumulator = 0.0f;

    LOG_TRACE("Entering main loop");

    while (m_window.beginFrame()) {
        const float deltaTime = m_frameTracker.getFrameRateInfo().frameTime / 1000.0f;

        m_window.updateInput();

        // The editor (if attached) writes the scene viewport rect to
        // WindowManager each frame. Without an editor, the 3D pipeline
        // sees the full window. A zero size in either dimension means
        // "no editor reported a rect this frame" -> use the window.
        const uint32_t winW = static_cast<uint32_t>(m_window.getWidth());
        const uint32_t winH = static_cast<uint32_t>(m_window.getHeight());
        const uint32_t vpW  = m_window.sceneViewportWidth();
        const uint32_t vpH  = m_window.sceneViewportHeight();
        const bool     vpSet = (vpW > 0 && vpH > 0);

        // Real time drives input/camera/UI; simulation time (paused, scaled, or
        // single-stepped) drives the sim systems and the fixed accumulator.
        const float simDelta = m_simClock.advance(deltaTime, Config::FIXED_TIME_STEP);

        FrameContext ctx{
            m_scene, m_resources, m_window, m_frameTracker,
            deltaTime,
            simDelta,
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

        // Fixed-step accumulator runs on simulation time, so pause (simDelta 0)
        // naturally yields zero ticks with no special case, a single-step feeds
        // exactly one step's worth, and a frozen accumulator never burst-catches
        // up on resume.
        const float beforeClamp = accumulator + simDelta;
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

        // Run every stage in order; each system does its whole job in update().
        // RenderSystem::update() builds + draws the frame in the Render stage,
        // and the editor (UI stage, after Render) draws its UI on top - so the
        // loop needs no special cases or cached system pointers.
        for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
            PROFILE_SCOPE_NAMED(STAGE_NAMES[s]);
            for (auto& sys : m_systemsByStage[s]) {
                if (sys->isEnabled()) sys->update(ctx);
            }
        }

        m_window.swapBuffers();

        m_frameTracker.update();

        // Optional FPS log to the console (opt-in via logFPS; the editor
        // leaves it off since it shows FPS in its status bar). Throttled to once
        // a second so it does not flood the log at thousands of frames a second.
        if (m_fpsLog) {
            m_fpsLogTimer += deltaTime;
            if (m_fpsLogTimer >= 1.0f) {
                m_fpsLogTimer = 0.0f;
                const FrameRateInfo& fr = m_frameTracker.getFrameRateInfo();
                LOG_INFO("FPS: %.0f (%.2f ms)", fr.frameRate, fr.frameTime);
            }
        }

        PROFILE_FRAME_MARK();
    }

    LOG_TRACE("Main loop exited, running shutdown");
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

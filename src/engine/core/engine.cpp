#define VKM_LOG_CATEGORY "CORE"

#include "core/engine.h"

#include <atomic>
#include <csignal>

#include "logger.h"

#include "core/engine_config.h"
#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"

namespace Vkm::Engine {

namespace {

constexpr const char* STAGE_NAMES[] = {
    "Input", "Simulation", "Transform", "Visibility", "Render", "UI"
};
static_assert(sizeof(STAGE_NAMES) / sizeof(STAGE_NAMES[0]) == static_cast<size_t>(SystemStage::Count),
              "STAGE_NAMES must stay in sync with SystemStage");

// Set from a signal handler, so it must be a lock-free integral type and the
// handler must touch nothing else - no logging, no allocation. The loop reads it
// and leaves through its normal exit, which is the point: an interrupt should
// unload the game module, join the workers and flush the log, not skip all three.
std::atomic<bool> g_interrupted{false};

extern "C" void onInterrupt(int) { g_interrupted.store(true, std::memory_order_relaxed); }

} // namespace

Engine::Engine()  = default;
Engine::~Engine() = default;

void Engine::run() {
    constexpr float FPS_LOG_INTERVAL = 1.0f;
    float fpsLogTimer = 0.0f;

    // Ctrl+C in the terminal that launched the engine. Without this the signal
    // has nowhere to land: the loop only ends when the window reports itself
    // closed, so a headless or unresponsive session had to be killed.
    std::signal(SIGINT,  onInterrupt);
    std::signal(SIGTERM, onInterrupt);

    LOG_TRACE("Entering main loop");

    while (!g_interrupted.load(std::memory_order_relaxed) && m_window.beginFrame()) {
        m_clock.beginFrame();

        FrameContext ctx{
            m_scene, m_resources,
            m_clock, m_events, m_window, m_input
        };

        m_window.updateInput();

        // Resolve actions once, before any system runs, so every reader in the
        // frame - and both the fixed and variable updates - sees the same input
        // state and the same edges.
        m_input.update(m_window.getInputHandle());

        if (!m_initialized) {
            initSystems(ctx);
        }

        while (m_clock.consumeFixedStep()) {
            PROFILE_SCOPE("FixedUpdate");
            for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
                PROFILE_SCOPE_NAMED(STAGE_NAMES[s]);
                for (auto& sys : m_systemsByStage[s]) {
                    if (sys->hasFixedUpdate()) sys->fixedUpdate(ctx);
                }
            }
        }

        for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
            PROFILE_SCOPE_NAMED(STAGE_NAMES[s]);

            // Queued events deliver at the top of Simulation - after input,
            // before any gameplay system ticks. The bus is infrastructure, so
            // the loop owns this step rather than a stand-in system.
            if (s == static_cast<size_t>(SystemStage::Simulation)) m_events.flush();

            for (auto& sys : m_systemsByStage[s]) {
                sys->update(ctx);
            }
        }

        m_window.swapBuffers();

        if (m_fpsLog) {
            fpsLogTimer += m_clock.getDeltaTime();
            if (fpsLogTimer >= FPS_LOG_INTERVAL) {
                fpsLogTimer = 0.0f;
                LOG_INFO("FPS: %.0f (%.2f ms)", m_clock.getFrameRate(), m_clock.getFrameTime());
            }
        }

        PROFILE_FRAME_MARK();
    }

    if (g_interrupted.load(std::memory_order_relaxed)) {
        LOG_INFO("Interrupted - shutting down");
    }
    LOG_TRACE("Main loop exited, running shutdown");

    // Join the workers before anything else winds down. The pool is a
    // function-local static, so left to itself it is destroyed after the
    // singletons its in-flight decodes push into - a load still running at quit
    // would hand its result to a queue that no longer exists.
    ThreadPool::get().shutdown();

    shutdownSystems();
}

void Engine::initSystems(FrameContext& ctx) {
    PROFILE_SCOPE("Engine::initSystems");

    size_t totalSystems = 0;
    for (auto& stage : m_systemsByStage) {
        for (auto& system : stage) {
            system->init(ctx);
            ++totalSystems;
        }
    }
    m_initialized = true;
    LOG_INFO("Initialized %zu system(s) across %zu stage(s)", totalSystems, m_systemsByStage.size());
}

void Engine::shutdownSystems() {
    LOG_TRACE("Shutting down systems (reverse registration order)");
    for (auto stageIt = m_systemsByStage.rbegin(); stageIt != m_systemsByStage.rend(); ++stageIt) {
        for (auto it = stageIt->rbegin(); it != stageIt->rend(); ++it) {
            (*it)->shutdown();
        }
    }
}

} // namespace Vkm::Engine

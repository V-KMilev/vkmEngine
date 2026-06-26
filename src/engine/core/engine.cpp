#define VKM_LOG_CATEGORY "CORE"

#include "core/engine.h"

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
    constexpr float FPS_LOG_INTERVAL = 1.0f;
    float fpsLogTimer = 0.0f;

    LOG_TRACE("Entering main loop");

    while (m_window.beginFrame()) {
        m_clock.beginFrame();

        FrameContext ctx{
            m_window, m_clock,
            m_scene, m_resources,
            nullptr
        };

        m_window.updateInput();

        if (!m_initialized) {
            initSystems(ctx);
        }

        // Fixed-step: the clock hands out one tick per accumulated step; each
        // tick runs every fixed-update system in stage order, before the
        // variable update below.
        while (m_clock.consumeFixedStep()) {
            PROFILE_SCOPE("FixedUpdate");
            for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
                PROFILE_SCOPE_NAMED(STAGE_NAMES[s]);
                for (auto& sys : m_systemsByStage[s]) {
                    if (sys->hasFixedUpdate()) sys->fixedUpdate(ctx);
                }
            }
        }

        // Variable-rate: every stage's update() in order, one Tracy zone per stage.
        for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
            PROFILE_SCOPE_NAMED(STAGE_NAMES[s]);
            for (auto& sys : m_systemsByStage[s]) {
                sys->update(ctx);
            }
        }

        m_window.swapBuffers();

        // Debug fps logging.
        if (m_fpsLog) {
            fpsLogTimer += m_clock.getDeltaTime();
            if (fpsLogTimer >= FPS_LOG_INTERVAL) {
                fpsLogTimer = 0.0f;
                LOG_INFO("FPS: %.0f (%.2f ms)", m_clock.getFrameRate(), m_clock.getFrameTime());
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
    // Reverse stage order, then reverse within each stage.
    for (auto stageIt = m_systemsByStage.rbegin(); stageIt != m_systemsByStage.rend(); ++stageIt) {
        for (auto it = stageIt->rbegin(); it != stageIt->rend(); ++it) {
            (*it)->shutdown();
        }
    }
}

} // namespace Engine

#define VKM_LOG_CATEGORY "CORE"

#include "core/engine.h"

#include <atomic>
#include <csignal>

#include "logger.h"

#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"

namespace Vkm::Engine {

namespace {

constexpr const char* STAGE_NAMES[] = {"Input", "Simulation", "Transform", "Visibility", "Render", "Editor"};
static_assert(
    sizeof(STAGE_NAMES) / sizeof(STAGE_NAMES[0]) == static_cast<size_t>(SystemStage::Count),
    "STAGE_NAMES must stay in sync with SystemStage"
);

// Set from a signal handler, so lock-free and the handler touches nothing else. The
// loop leaves through its normal exit, so the module unloads, workers join, log flushes.
std::atomic<bool> g_interrupted{false};

extern "C" void onInterrupt(int) { g_interrupted.store(true, std::memory_order_relaxed); }

} // namespace

Engine::Engine()  = default;
Engine::~Engine() = default;

void Engine::run() {
    constexpr float FPS_LOG_INTERVAL = 1.0f;
    float statusTimer = 0.0f;

    std::signal(SIGINT,  onInterrupt);
    std::signal(SIGTERM, onInterrupt);

    LOG_TRACE("Entering main loop");

    while (!g_interrupted.load(std::memory_order_relaxed) && m_window.beginFrame()) {
        m_clock.beginFrame();

        FrameContext ctx{
            m_scene,
            m_resources,
            m_clock,
            m_events,
            m_window,
            m_input,
            m_net,
            m_chrome,
            m_render
        };

        m_window.updateInput();

        // Before the ticks, so they see the arrived world. Wall clock: a paused
        // editor still has to notice a peer that has gone.
        m_net.advance(m_clock.getDeltaTime());
        m_net.receive(m_scene, m_resources);

        // See NetSession::pacing.
        m_clock.setPacing(m_net.pacing());

        // Before the ticks, so the local character collides with the world where
        // it is shown, not where the newest packet put it.
        m_net.interpolate(m_scene, m_clock.getDeltaTime());

        // Once, so every frame-rate reader agrees; also latches the next tick's edges.
        m_input.update(m_window.getInputHandle(), m_chrome);

        if (!m_initialized) {
            initSystems(ctx);
        }

        // Ticks re-run because the server disagreed. No event flush, no
        // InputMap::beginTick, only isReplayed systems - see networking.md, "Replay".
        for (const InputCommand& command : m_net.replayCommands()) {
            PROFILE_SCOPE("Replay");
            m_net.beginReplayTick(command);
            for (auto& stage : m_systemsByStage) {
                for (auto& sys : stage) {
                    if (sys->isReplayed()) sys->fixedUpdate(ctx);
                }
            }
            m_net.endTick(m_scene, command.tick);
        }
        m_net.endReplay(m_scene);

        bool ticked = false;
        while (m_clock.consumeFixedStep()) {
            PROFILE_SCOPE("FixedUpdate");
            ticked = true;
            m_input.beginTick(m_clock.getTick());
            m_net.beginTick(
                m_clock.getTick(),
                m_input.command(),
                m_input.actionCount(),
                m_input.actionFingerprint()
            );
            for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
                PROFILE_SCOPE_NAMED(STAGE_NAMES[s]);

                // Per tick, not per frame, or the frame rate would decide how many
                // ticks late a reaction lands.
                if (s == static_cast<size_t>(SystemStage::Simulation)) m_events.flush();

                for (auto& sys : m_systemsByStage[s]) {
                    sys->fixedUpdate(ctx);
                }
            }

            // Kept so the server's answer can be compared rather than believed.
            m_net.endTick(m_scene, m_clock.getTick());
        }

        // A still world runs no tick, and a press made meanwhile is not one it was running for.
        if (!ticked && m_clock.isSimulationStill()) m_input.discardPendingEdges();

        {
            // The local character is drawn with the correction not yet worked off,
            // for this scope only, so no tick simulates from it.
            const NetSession::Drawn drawn(m_net, m_scene, m_clock.getDeltaTime());

            for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
                PROFILE_SCOPE_NAMED(STAGE_NAMES[s]);

                // What was queued outside a tick or by the frame's last tick; flush
                // drains, so nothing repeats.
                if (s == static_cast<size_t>(SystemStage::Simulation)) m_events.flush();

                for (auto& sys : m_systemsByStage[s]) {
                    sys->update(ctx);
                }
            }
        }

        // After the ticks, so a snapshot is the world now, not a frame ago.
        m_net.send(m_scene, m_clock.getTick());

        m_window.swapBuffers();

        statusTimer += m_clock.getDeltaTime();
        if (statusTimer >= FPS_LOG_INTERVAL) {
            statusTimer = 0.0f;
            if (m_fpsLog) {
                LOG_INFO("FPS: %.0f (%.2f ms)", m_clock.getFrameRate(), m_clock.getFrameTime());
            }
            if (!m_net.isOffline()) LOG_INFO("Net: %s", m_net.describe().c_str());
        }

        PROFILE_FRAME_MARK();
    }

    // Told rather than dropped, so a peer frees its seat now, not after a timeout.
    m_net.close();

    if (g_interrupted.load(std::memory_order_relaxed)) {
        LOG_INFO("Interrupted - shutting down");
    }
    LOG_TRACE("Main loop exited, running shutdown");

    // Join the workers first; see ThreadPool::shutdown.
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

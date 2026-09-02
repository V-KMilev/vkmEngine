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
    float statusTimer = 0.0f;

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
            m_clock, m_events, m_window, m_input, m_net
        };

        m_window.updateInput();

        // Before the ticks that consume it, so a tick sees an arrived world
        // rather than last frame's. The timers move on wall clock: a paused
        // editor still has to notice a peer that has gone.
        m_net.advance(m_clock.getDeltaTime());
        m_net.receive(m_scene, m_resources);

        // Drawn before the ticks that read it, so what the player's own
        // character collides with this frame is where the world is shown to be
        // rather than where the newest packet said it was.
        m_net.interpolate(m_scene, m_clock.getDeltaTime(),
                          1.0f / m_clock.getFixedStep());

        // Resolve actions once, so every frame-rate reader sees the same state.
        // This also latches the edges the next tick's command is built from.
        m_input.update(m_window.getInputHandle());

        if (!m_initialized) {
            initSystems(ctx);
        }

        // Ticks that already happened, run again because the server disagreed.
        // The event flush, InputMap::beginTick and every system answering false
        // to isReplayed are absent - see networking.md, "Replay".
        for (const InputCommand& command : m_net.replayCommands()) {
            PROFILE_SCOPE("Replay");
            m_net.beginReplayTick(command);
            for (auto& stage : m_systemsByStage) {
                for (auto& sys : stage) {
                    if (sys->hasFixedUpdate() && sys->isReplayed()) sys->fixedUpdate(ctx);
                }
            }
            m_net.endTick(m_scene, command.tick);
        }
        m_net.endReplay(m_scene);

        while (m_clock.consumeFixedStep()) {
            PROFILE_SCOPE("FixedUpdate");
            // The command this tick runs under, built before any system reads it.
            m_input.beginTick(m_clock.getTick());
            m_net.beginTick(m_clock.getTick(), m_input.command(), m_input.actionCount());
            for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
                PROFILE_SCOPE_NAMED(STAGE_NAMES[s]);

                // Delivered at the top of Simulation on the next tick: waiting
                // for the end of the frame would put a reaction however many
                // ticks later the frame rate decides.
                if (s == static_cast<size_t>(SystemStage::Simulation)) m_events.flush();

                for (auto& sys : m_systemsByStage[s]) {
                    if (sys->hasFixedUpdate()) sys->fixedUpdate(ctx);
                }
            }

            // What this tick left, kept so the server's answer about it can be
            // compared rather than believed.
            m_net.endTick(m_scene, m_clock.getTick());
        }

        {
            // What a client draws for its own character is the simulation plus
            // the correction not yet visibly worked off. In the component for
            // this scope only, so no tick can simulate from it.
            const NetSession::Drawn drawn(m_net, m_scene, m_clock.getDeltaTime());

            for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
                PROFILE_SCOPE_NAMED(STAGE_NAMES[s]);

                // Whatever was queued outside a tick, or by the last tick of
                // this frame. flush drains, so this never repeats what the loop
                // above already delivered.
                if (s == static_cast<size_t>(SystemStage::Simulation)) m_events.flush();

                for (auto& sys : m_systemsByStage[s]) {
                    sys->update(ctx);
                }
            }
        }

        // After the ticks that caused it, so a snapshot describes the world as
        // it now stands rather than as it stood a frame ago.
        m_net.send(m_scene, m_clock.getTick());

        m_window.swapBuffers();

        // One line a second while a session is open, whether or not the frame
        // rate is logged: somebody asking why the game feels as it does is
        // asking about the connection, and the runtime has no panel to show it.
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

    // Said rather than simply stopped, while the socket is open and the peers
    // still exist: one that is told frees its seat now rather than a timeout
    // from now, and can say the game closed rather than appear to freeze.
    m_net.close();

    if (g_interrupted.load(std::memory_order_relaxed)) {
        LOG_INFO("Interrupted - shutting down");
    }
    LOG_TRACE("Main loop exited, running shutdown");

    // Join the workers first: the pool is a function-local static, destroyed
    // after the singletons its in-flight decodes push into, so a load still
    // running at quit would hand its result to a queue that no longer exists.
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

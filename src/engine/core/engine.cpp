#define VKM_LOG_CATEGORY "CORE"

#include "core/engine.h"

#include <optional>

#include "vkm_log/logger.h"

#include "base/profile/profiler.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/scene/world_transform.h"
#include "ecs/component/render/camera.h"
#include "vkm_platform/process/this_process.h"
#include "vkm_platform/threading/thread_pool.h"
#include "frame/seconds_ahead.h"

namespace Vkm::Engine {

namespace {

constexpr const char* STAGE_NAMES[] = {"Input", "Simulation", "Transform", "Visibility", "Render", "Editor"};
static_assert(
    sizeof(STAGE_NAMES) / sizeof(STAGE_NAMES[0]) == static_cast<size_t>(SystemStage::Count),
    "STAGE_NAMES must stay in sync with SystemStage"
);

/// Whether the frames a host shows carry the debug lines drawn into them: never a shipped game's.
#if VKM_SHIPPING
constexpr bool SHOWS_DEBUG_DRAWING = false;
#else
constexpr bool SHOWS_DEBUG_DRAWING = true;
#endif

} // namespace

Engine::Engine()
    : m_frame{
          FrameProducts{},
          m_scene,
          m_resources,
          m_clock,
          m_events,
          m_window,
          m_input,
          m_net,
          m_chrome,
          m_graphics,
          m_camera,
          m_readiness
      }
{}

Engine::~Engine() = default;

void Engine::run() {
#if !VKM_SHIPPING
    constexpr float NET_LOG_INTERVAL = 1.0f;
    float netLogTimer = 0.0f;
#endif

    LOG_TRACE("Entering main loop");

    // A stop the system asks for (Ctrl+C, SIGTERM) leaves through the loop's normal exit, so the module
    // unloads, workers join and the log flushes.
    while (!Platform::stopRequested() && m_window.beginFrame()) {
        m_clock.beginFrame();

        FrameContext& ctx = m_frame;
        ctx.clearProducts();
        // A host that shows nothing draws nothing, nor does a shipped game: their behaviors get
        // DebugDraw::inert().
        ctx.debug = SHOWS_DEBUG_DRAWING && m_window.isOpen() ? &m_debug : nullptr;

        m_window.updateInput();
        // A window mode the game asked for, taken between frames as the desktop changes it.
        if (const std::optional<Platform::WindowMode> mode = m_graphics.takeWindowRequest()) {
            m_window.updateMode(*mode);
        }

        // Before the ticks, so they see the arrived world. Wall clock: a paused
        // game still has to notice a peer that has gone.
        m_net.advance(m_clock.getDeltaTime());
        m_net.receive(m_scene, m_resources);
        m_net.deliver(m_events);

        // See NetSession::pacing.
        m_clock.setPacing(m_net.pacing());

        // Before the ticks, so the local character collides with the world where
        // it is shown, not where the newest packet put it.
        m_net.interpolate(m_scene, m_clock.getDeltaTime());

        // Once, so every frame-rate reader agrees; also latches the next tick's edges.
        m_input.update(m_window.getInputHandle(), m_chrome);

        // Where the player looks, as the last frame showed it: a tick steers by the view its input was
        // taken with (BehaviorBase::alongView), not by a camera that has turned since.
        const EntityId eye = m_camera.of(m_scene);
        if (const Transform* pose = m_scene.tryGet<Transform>(eye)) {
            m_input.setView(resolvedWorldRotation(m_scene, eye, *pose));
        }

        if (!m_initialized) {
            initSystems(ctx);
        }

        // Ticks re-run because the server disagreed. No event flush, no InputMap::beginTick, only
        // isReplayed systems - see networking.md, "Replay". What they draw was drawn the first time.
        m_debug.setMuted(true);
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
        m_debug.setMuted(false);

        // No tick runs while a mesh a collider or water is made of decodes; loads land outside the
        // ticks, so it is there by the next one that runs. Nor does a world of its own tick while no
        // world can be drawn - a cold start's programs build for seconds - or it is played under the
        // cover, its time spent and its keys taken. A session's world is the server's, and goes on.
        const bool unseen = m_readiness.compiling() && m_net.isOffline();
        m_clock.setHeld(m_tickHold.holds(m_scene, m_resources) || unseen);

        bool ticked = false;
        while (m_clock.consumeFixedStep()) {
            PROFILE_SCOPE("FixedUpdate");
            ticked = true;
            m_debug.beginTick();
            m_input.beginTick(m_clock.getTick());
            m_net.beginTick(m_clock.getTick(), m_input.command());
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
            m_debug.endTick();

            // A tick that made a collider or water of a mesh still decoding is the last until it lands.
            if (m_tickHold.holds(m_scene, m_resources)) m_clock.setHeld(true);
        }

        // A still world runs no tick, and a press made meanwhile is not one it was running for.
        if (!ticked && m_clock.isSimulationStill()) m_input.discardPendingEdges();

        {
            // The local character is drawn with the correction not yet worked off,
            // for this scope only, so no tick simulates from it.
            const NetSession::Drawn drawn(m_net, m_scene, m_clock.getDeltaTime());

            m_drawnAhead.carry(m_scene, m_net, secondsAhead(m_clock, m_window));

            for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
                PROFILE_SCOPE_NAMED(STAGE_NAMES[s]);

                // What was queued outside a tick or by the frame's last tick; flush
                // drains, so nothing repeats.
                if (s == static_cast<size_t>(SystemStage::Simulation)) m_events.flush();

                for (auto& sys : m_systemsByStage[s]) {
                    sys->update(ctx);
                }
            }

            m_drawnAhead.putBack(m_scene);
        }

        // After the ticks, so a snapshot is the world now, not a frame ago.
        m_net.send(m_scene, m_clock.getTick());
        m_debug.endFrame();

        m_window.swapBuffers();

#if !VKM_SHIPPING
        // Not in a shipped game: a write to its log blocks the frame while the disk is flushing.
        netLogTimer += m_clock.getDeltaTime();
        if (netLogTimer >= NET_LOG_INTERVAL) {
            netLogTimer = 0.0f;
            if (!m_net.isOffline()) LOG_INFO("Net: %s", m_net.describe().c_str());
        }
#endif

        PROFILE_FRAME_MARK();
    }

    // Told rather than dropped, so a peer frees its seat now, not after a timeout.
    m_net.close();

    if (Platform::stopRequested()) {
        LOG_INFO("Interrupted - shutting down");
    }
    LOG_TRACE("Main loop exited, running shutdown");

    // Join the workers first; see ThreadPool::shutdown.
    Platform::ThreadPool::get().shutdown();

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

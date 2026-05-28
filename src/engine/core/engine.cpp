#define VKM_LOG_CATEGORY "CORE"

#include "core/engine.h"

#include <algorithm>
#include <chrono>
#include <vector>

#include "logger.h"

#include "core/engine_config.h"
#include "debug/profiler.h"
#include "platform/threading/render_thread.h"
#include "platform/threading/thread_pool.h"
#include "system/render/render_system.h"

namespace Engine {

Engine::Engine()  = default;
Engine::~Engine() = default;

namespace {

constexpr const char* STAGE_NAMES[] = {
    "Input", "Simulation", "Transform", "Visibility", "Render", "UI"
};

/**
 * @brief Greedy layer assignment: walk systems in registration order, drop each
 *
 * one into the earliest layer where its access doesn't conflict with any
 * system already in that layer. Two accesses conflict when one's writes
 * overlap the other's reads or writes (read-only / read-only is fine).
 *
 * Systems with empty SystemAccess are treated as "writes everything" -
 * they conflict with anyone and therefore always end up in their own
 * dedicated layer. This is the safe default: a system that hasn't
 * declared accesses (or whose work is genuinely conservative, like
 * EventSystem flushing user callbacks) never gets parallel-dispatched.
 */
bool overlaps(const std::vector<TypeId>& a, const std::vector<TypeId>& b) {
    for (TypeId t : a)
        if (std::find(b.begin(), b.end(), t) != b.end()) return true;
    return false;
}

bool isConservative(const SystemAccess& a) {
    // An empty SystemAccess that was not explicitly declared via
    // SystemAccess::none() is treated as "writes everything" - the safe
    // default for systems that didn't bother to declare. A system that
    // explicitly declared empty (touches no ECS / no shared state) is
    // parallel-safe with anyone and is NOT conservative.
    if (a.noAccessDeclared) return false;
    return a.reads.empty() && a.writes.empty();
}

bool conflicts(const SystemAccess& a, const SystemAccess& b) {
    // Conservative declarations conflict with everything (we don't know
    // what they touch). Pure read-only against pure read-only is fine.
    if (isConservative(a) || isConservative(b)) return true;
    if (overlaps(a.writes, b.writes)) return true;
    if (overlaps(a.writes, b.reads))  return true;
    if (overlaps(a.reads,  b.writes)) return true;
    return false;
}

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
            Config::FixedTimeStep,
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
        accumulator = std::min(beforeClamp, Config::MaxFrameAccumulator);
        if (beforeClamp > Config::MaxFrameAccumulator) {
            // Sustained slow frames would spam this every tick. Rate-limit
            // to one warning per second of wall clock; on the throttled
            // frames just bump a counter and emit a summary on the next
            // un-throttled warning.
            const auto now = std::chrono::steady_clock::now();
            if (now - m_lastAccumClampWarn >= std::chrono::seconds(1)) {
                if (m_accumClampSuppressed > 0) {
                    LOG_WARNING("Frame accumulator clamped (%.3fs > %.3fs cap, +%d similar in the last second) - spiral-of-death guard fired; some fixed ticks dropped",
                        beforeClamp, Config::MaxFrameAccumulator, m_accumClampSuppressed);
                } else {
                    LOG_WARNING("Frame accumulator clamped (%.3fs > %.3fs cap) - spiral-of-death guard fired; some fixed ticks dropped",
                        beforeClamp, Config::MaxFrameAccumulator);
                }
                m_accumClampSuppressed = 0;
                m_lastAccumClampWarn = now;
            } else {
                ++m_accumClampSuppressed;
            }
        }
        while (accumulator >= Config::FixedTimeStep) {
            PROFILE_SCOPE("FixedUpdate");
            for (System* system : m_fixedUpdaters) {
                if (system->isEnabled()) {
                    system->fixedUpdate(ctx);
                }
            }
            accumulator -= Config::FixedTimeStep;
        }

        // First frame after init: optionally migrate the GL context to a
        // dedicated render thread + locate the RenderSystem so the overlap
        // loop can split its buildView / executeFrame phases. Done here
        // rather than before the loop so initSystems (and any GL work it
        // triggers, like the IBL bake's first execute) still sees the
        // context on the main thread.
        if (m_renderThreadEnabled && !m_renderThread) {
            const size_t renderIdx = static_cast<size_t>(SystemStage::Render);
            for (auto& system : m_systemsByStage[renderIdx]) {
                if (auto* rs = dynamic_cast<RenderSystem*>(system.get())) {
                    m_renderSystem = rs;
                    break;
                }
            }
            if (!m_renderSystem) {
                LOG_ERROR("Render thread enabled but no RenderSystem registered; falling back to single-threaded mode");
                m_renderThreadEnabled = false;
            } else {
                // Cache UI-stage systems with GL-side companion work
                // (currently just EditorSystem). Their executeGL() runs
                // on the render thread after the scene render finishes,
                // so the UI overlays the rendered scene.
                const size_t uiIdx = static_cast<size_t>(SystemStage::UI);
                for (auto& system : m_systemsByStage[uiIdx]) {
                    if (system->hasGLWork()) m_glWorkSystems.push_back(system.get());
                }
                // Editor panels can no longer render previews inline (GL
                // context moves off main); RenderSystem will queue requests
                // instead and the render thread drains them in executeFrame.
                m_renderSystem->setDeferPreviewRender(true);
                m_renderThread = std::make_unique<RenderThread>(m_window.getWindowContext());
            }
        }

        if (m_renderThread) {
            // Phase 2B overlap. Frame K layout (overlapping with render K-1):
            //   pre-wait phase  (non-mutator systems, all stages)   <- overlaps
            //   buildView K     (RenderSystem; writes m_views[K&1]) <- overlaps
            //   waitForFrame    (block until render K-1 is done)    <-- sync point
            //   post-wait phase (mutator systems, all stages)
            //   postFrame K     (render thread reads m_views[K&1])
            // The render thread runs RenderSystem::executeFrame against
            // the buffer index it was posted with - main has already moved
            // on to writing the OTHER buffer for frame K+1, so no race.
            constexpr size_t RENDER_IDX = static_cast<size_t>(SystemStage::Render);

            // UI stage IS included in runPhase. Systems with hasGLWork()
            // (EditorSystem) do their build on main here; their executeGL()
            // is appended to the render-thread lambda below.
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

            // 1. Pre-wait: non-mutator systems. Reads only; overlap with render K-1.
            runPhase(/*mutators=*/false);

            // 2. Build the RenderView for frame K on main (still overlapping with render K-1).
            m_renderSystem->buildView(ctx, m_renderFrameIndex);

            // 3. Wait for render K-1 to finish before any resource mutation.
            m_renderThread->waitForFrame();

            // 4. Post-wait: mutator systems. Render thread is idle; safe to mutate.
            runPhase(/*mutators=*/true);

            // 5. Post render K. ctx is copied by value into the lambda so
            //    the next loop iteration can mutate the local ctx without
            //    affecting the render thread's snapshot. UI-stage GL-work
            //    systems run AFTER executeFrame so the UI overlays the
            //    rendered scene.
            const uint32_t frameIdx = m_renderFrameIndex;
            m_renderThread->postFrame([this, ctx, frameIdx]() mutable {
                m_renderSystem->executeFrame(ctx, frameIdx);
                for (System* sys : m_glWorkSystems) sys->executeGL(ctx);
                m_window.swapBuffers();
            });
            ++m_renderFrameIndex;
        } else {
            for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
                updateStage(static_cast<SystemStage>(s), ctx);
            }
            if (!m_window.swapBuffers()) break;
        }

        m_frameTracker.update();
        PROFILE_FRAME_MARK();
    }

    LOG_TRACE("Main loop exited, running shutdown");
    // Tear the render thread down before shutdownSystems so any GL
    // teardown in destructors finds the context current on main again.
    m_renderThread.reset();
    shutdownSystems();
}

void Engine::initSystems(FrameContext& ctx) {
    PROFILE_SCOPE("Engine::initSystems");

    // Compute the per-stage layer plan from each system's declared access.
    buildSchedule();

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

void Engine::buildSchedule() {
    for (size_t s = 0; s < m_systemsByStage.size(); ++s) {
        auto& stage = m_systemsByStage[s];
        StageSchedule& plan = m_schedule[s];
        plan.layers.clear();

        // Greedy: walk systems in registration order, drop into earliest
        // layer with no conflict. Empty layers handled by always adding to
        // the back when nothing fit (preserves registration order across
        // layers as a tiebreak).
        for (auto& sysPtr : stage) {
            ScheduledSystem entry;
            entry.system = sysPtr.get();
            entry.access = sysPtr->declareAccess();

            bool placed = false;
            for (auto& layer : plan.layers) {
                bool ok = true;
                for (const auto& other : layer) {
                    if (conflicts(entry.access, other.access)) { ok = false; break; }
                }
                if (ok) {
                    layer.push_back(std::move(entry));
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                plan.layers.push_back({ std::move(entry) });
            }
        }

        // Diagnostic: summarise the plan. Helps the user see what would
        // benefit from setParallelDispatch and what's serialized by a
        // conservative declaration.
        if (!stage.empty()) {
            size_t maxLayer = 0;
            for (const auto& layer : plan.layers) {
                if (layer.size() > maxLayer) maxLayer = layer.size();
            }
            LOG_VERBOSE("Schedule[%s]: %zu system(s), %zu layer(s), max layer width %zu",
                STAGE_NAMES[s], stage.size(), plan.layers.size(), maxLayer);
        }
    }
}

void Engine::setParallelDispatch(SystemStage stage, bool enabled) {
    const size_t idx = static_cast<size_t>(stage);
    if (idx >= m_schedule.size()) return;
    m_schedule[idx].parallelDispatch = enabled;
    if (enabled) {
        LOG_INFO("Parallel dispatch enabled for stage %zu", idx);
    }
}

void Engine::updateStage(SystemStage stage, FrameContext& ctx) {
    PROFILE_SCOPE_NAMED(STAGE_NAMES[static_cast<size_t>(stage)]);

    const StageSchedule& plan = m_schedule[static_cast<size_t>(stage)];

    for (const auto& layer : plan.layers) {
        // Single-system layers always run inline - dispatch overhead would
        // dwarf the work. Parallel dispatch needs the caller's explicit
        // opt-in (off by default) AND a layer with more than one entry.
        if (layer.size() == 1 || !plan.parallelDispatch) {
            for (const auto& entry : layer) {
                if (!entry.system->isEnabled()) continue;
                entry.system->update(ctx);
                // Single-threaded path: GL companion runs inline here
                // (we're on the GL thread; no render-thread split active).
                // Skipped when render thread is enabled - the run loop
                // handles routing into the render-thread lambda instead.
                if (!m_renderThread && entry.system->hasGLWork()) {
                    entry.system->executeGL(ctx);
                }
            }
            continue;
        }

        // Fan out across the layer via the engine's parallelFor pattern
        // (grain = 1, one system per task). parallelFor pumps the first
        // chunk on the main thread and waits on the rest before returning,
        // so the next layer sees every prior write before it starts.
        parallelFor(layer.size(), size_t{1}, [&](size_t i) {
            const auto& entry = layer[i];
            if (entry.system->isEnabled()) entry.system->update(ctx);
        });
        // GL-work systems can't be safely fanned out (GL context is single-
        // threaded); call them serially on main after the parallel layer.
        if (!m_renderThread) {
            for (const auto& entry : layer) {
                if (entry.system->isEnabled() && entry.system->hasGLWork()) {
                    entry.system->executeGL(ctx);
                }
            }
        }
    }
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

#pragma once

#include <cstdint>
#include <vector>

#include "core/memory/types.h"
#include "system/visibility/visibility.h"

namespace Engine {

class Scene;
class ResourceManager;
class WindowManager;
class StatisticTracker;

/**
 * @brief Named per-frame execution stages.
 *
 * Each system is registered at exactly one stage. Stages run in declaration
 * order each frame; within a stage, systems run in registration order.
 *
 * The structure mirrors how a frame actually flows:
 *   Input        → poll devices, capture/handle input (e.g., CameraController)
 *   Simulation   → state mutations: events, gameplay, animation, physics
 *   Transform    → derive world-space data from local Transforms (HierarchySystem)
 *   Visibility   → culling against the derived world state (VisibilitySystem)
 *   Render       → submit draw commands (RenderSystem)
 *   UI           → overlays, editor (EditorSystem)
 *
 * fixedUpdate() observes the same ordering (rarely matters in practice).
 *
 * Future systems slot in by responsibility:
 *   Physics      → Simulation (its fixedUpdate hook does the work)
 *   Gameplay     → Simulation
 *   Scripting    → Simulation, or split between Input (input scripts) and Render (UI scripts)
 */
enum class SystemStage : uint8_t {
    Input        = 0,
    Simulation   = 1,
    Transform    = 2,
    Visibility   = 3,
    Render       = 4,
    UI           = 5,

    Count  ///< Sentinel: number of stages. Keep last.
};

/**
 * @brief Per-frame state bundle passed to all systems.
 *
 * Provides a uniform interface for systems to access shared per-frame data.
 * visibility is a non-owning pointer to persistent storage (owned by VisibilitySystem)
 * to avoid per-frame vector allocation/deallocation.
 *
 * Two deltas:
 *   - deltaTime: variable, real elapsed time since last render frame. Read in update().
 *   - fixedDeltaTime: constant simulation step (1/60 by default). Read in fixedUpdate().
 */
struct FrameContext {
    Scene& scene;
    ResourceManager& resources;
    WindowManager& window;
    StatisticTracker& statistics;
    float deltaTime;
    float fixedDeltaTime;
    uint32_t viewportWidth;
    uint32_t viewportHeight;
    const Visibility* visibility = nullptr;
};

/**
 * @brief Declares which component types a system reads and writes.
 *
 * Drives the per-stage layer scheduler in Engine. Engine::buildSchedule()
 * partitions each stage's systems into layers using a greedy assignment:
 * a system goes into the earliest layer where its (reads + writes) don't
 * overlap any other system's writes, and its writes don't overlap any
 * other system's reads. With parallel dispatch enabled for a stage (via
 * Engine::setParallelDispatch), systems within a layer fan out across
 * the ThreadPool; otherwise the layer runs sequentially.
 *
 * Default returned by System::declareAccess() is empty (no reads, no
 * writes). The scheduler treats that as "conservative": the system
 * conflicts with every other system in the stage and always ends up in
 * its own dedicated layer. The safe default - a system that hasn't
 * declared, or whose work is genuinely unpredictable (EventSystem
 * flushing user callbacks, EditorSystem reacting to user input), never
 * gets parallel-dispatched.
 *
 * Systems that DO declare access are committing to a contract:
 *   - They touch only the listed component TypeIds.
 *   - They don't call scene.add / scene.remove / scene.destroyEntity
 *     (structural SparseSet mutations are not thread-safe and would
 *     race with other parallel-dispatched systems).
 *   - They don't write shared external state (ResourceManager versions,
 *     Window state, GPU state, ...) outside what other parallel-safe
 *     systems can tolerate.
 */
struct SystemAccess {
    std::vector<TypeId> reads;   ///< Component TypeIds this system reads from
    std::vector<TypeId> writes;  ///< Component TypeIds this system writes to
};

/**
 * @brief Abstract base class for per-frame systems.
 *
 * Systems are executed sequentially in registration order. Each system
 * reads from and/or writes to the FrameContext. Systems support lifecycle
 * hooks (init/shutdown) and can be enabled/disabled at runtime.
 */
class System {
    public:
        virtual ~System() = default;

        System(const System& other) = delete;
        System& operator=(const System& other) = delete;

        System(System && other) = delete;
        System& operator=(System && other) = delete;

    public:
        /**
         * @brief Called once after all systems are registered, before the first update.
         * @param ctx The shared FrameContext for initialization.
         */
        virtual void init(FrameContext& ctx) {}

        /**
         * @brief Execute this system for the current frame.
         * @param ctx The shared FrameContext for this frame.
         */
        virtual void update(FrameContext& ctx) = 0;

        /**
         * @brief Execute this system at the fixed simulation rate.
         *
         * Called 0+ times per render frame, driven by an accumulator in the main
         * loop. Use ctx.fixedDeltaTime (not deltaTime). Intended for deterministic
         * simulation (physics, networking tick). Empty default; opt in by override.
         *
         * Note: fixedUpdate runs BEFORE update each frame, so ctx.visibility is
         * either null (first frame) or stale (previous frame's result). Don't
         * rely on it from fixedUpdate.
         */
        virtual void fixedUpdate(FrameContext& ctx) {}

        /**
         * @brief Called once on engine shutdown, in reverse registration order.
         */
        virtual void shutdown() {}

        /**
         * @brief Declare which component types this system reads and writes.
         *
         * Override to participate in the startup write-write conflict warning
         * (see SystemAccess). Default returns empty (the system opts out of
         * the diagnostic; ordering is governed entirely by stage + registration
         * order).
         */
        virtual SystemAccess declareAccess() const { return {}; }

        bool isEnabled() const { return m_enabled; }
        void setEnabled(bool enabled) { m_enabled = enabled; }

    protected:
        System() = default;

    private:
        bool m_enabled = true;
};

} // namespace Engine

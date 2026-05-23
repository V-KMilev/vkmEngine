#pragma once

#include <cstdint>
#include <vector>

#include "core/memory/types.h"
#include "system/visibility/visibility.h"

namespace Engine {

class Scene;
class ResourceManager;
class WindowManager;
class FrameTracker;

/**
 * @brief Named per-frame execution stages.
 *
 * Each system is registered at exactly one stage. Stages run in declaration
 * order each frame; within a stage, systems run in registration order.
 *
 * The structure mirrors how a frame actually flows:
 *   Input        -> poll devices, capture/handle input (e.g., CameraController)
 *   Simulation   -> state mutations: events, gameplay, animation, physics
 *   Transform    -> derive world-space data from local Transforms (HierarchySystem)
 *   Visibility   -> culling against the derived world state (VisibilitySystem)
 *   Render       -> submit draw commands (RenderSystem)
 *   UI           -> overlays, editor (EditorSystem)
 *
 * fixedUpdate() observes the same ordering (rarely matters in practice).
 *
 * Future systems slot in by responsibility:
 *   Physics      -> Simulation (its fixedUpdate hook does the work)
 *   Gameplay     -> Simulation
 *   Scripting    -> Simulation, or split between Input (input scripts) and Render (UI scripts)
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
    FrameTracker& frameTracker;
    float deltaTime;
    float fixedDeltaTime;
    // The scene's render rect within the GLFW window. The editor reports
    // it via WindowManager::setSceneViewport so the 3D pass renders at the
    // viewport's aspect & size, not the full window. With no editor, the
    // engine defaults to the full window.
    uint32_t viewportX = 0;
    uint32_t viewportY = 0;
    uint32_t viewportWidth;
    uint32_t viewportHeight;

    /**
     * @brief Per-frame visibility snapshot - reads from VisibilitySystem, which
     *
     * populates it in SystemStage::Visibility. Lifecycle:
     *   - null on the very first frame (no stage has run yet)
     *   - null in updates that run before Visibility stage (Input,
     *     Simulation, Transform) - those stages don't have data yet
     *   - non-null in Visibility, Render, UI from frame 2 onward
     * Editor overlays guard with `if (ctx.visibility)` so they degrade
     * to a no-op on the first frame instead of asserting.
     */
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

    /**
     * @brief True when the system explicitly declared it touches no
     *        component state.
     *
     * Default-constructed / list-initialised SystemAccess leaves this
     * false; the scheduler treats those as "conservative" (writes
     * everything) and serialises the system on its own layer. An
     * explicit-empty declaration (returned by SystemAccess::none()) is
     * parallel-safe with any other system.
     *
     * This split matters for "trivial" systems whose work doesn't touch
     * the ECS at all (debug overlays, frame stat printers): without
     * SystemAccess::none() they get a dedicated layer they don't need.
     */
    bool noAccessDeclared = false;

    /**
     * @brief Factory for the explicit "I touch no component state"
     *        declaration.
     *
     * Use this in declareAccess() overrides on systems whose update body
     * has no reads, no writes, no scene mutation, and no external state
     * changes that other parallel-safe systems care about.
     *
     * @return SystemAccess with noAccessDeclared = true.
     */
    static SystemAccess none() {
        SystemAccess a;
        a.noAccessDeclared = true;
        return a;
    }
};

/**
 * @brief Abstract base class for per-frame systems.
 *
 * Systems are scheduled per SystemStage and executed in stage order each
 * frame. Within a stage, the default is registration order; Engine's layer
 * scheduler can run independent systems concurrently when their
 * SystemAccess declarations don't conflict and the stage has parallel
 * dispatch enabled. Systems read from and/or write to a shared
 * FrameContext and support init/update/fixedUpdate/shutdown hooks plus
 * runtime enable/disable.
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
         * @brief Whether this system implements fixedUpdate().
         *
         * Override and return true in any system that provides a real
         * fixedUpdate body. The engine builds a filtered list at init so the
         * fixed-step accumulator loop only visits opt-in systems instead of
         * dispatching empty virtuals across every registered system every
         * tick. Default false matches the default empty fixedUpdate.
         */
        virtual bool hasFixedUpdate() const { return false; }

        /**
         * @brief Called once on engine shutdown, in reverse registration order.
         */
        virtual void shutdown() {}

        /**
         * @brief Declare which component types this system reads and writes.
         *
         * Drives Engine's per-stage layer scheduler (see SystemAccess for the
         * full contract). Default returns an empty access, which the scheduler
         * treats conservatively (the system conflicts with everything in its
         * stage and runs on its own layer). Systems that genuinely touch no
         * shared component state should return SystemAccess::none() instead -
         * that's explicitly "no access" and lets the scheduler pack them
         * concurrently with anyone else.
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

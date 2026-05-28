#pragma once

#include <cstdint>

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
 * visibility is a non-owning pointer to persistent storage (owned by
 * VisibilitySystem) so systems can read culling results without forcing
 * a per-frame vector allocation.
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
     * @brief Per-frame visibility snapshot populated by VisibilitySystem.
     *
     * Lifecycle:
     *   - null on the very first frame (no stage has run yet)
     *   - null in updates that run before the Visibility stage (Input,
     *     Simulation, Transform) - those stages don't have data yet
     *   - non-null in Visibility, Render, UI from frame 2 onward
     * Editor overlays guard with `if (ctx.visibility)` so they degrade
     * to a no-op on the first frame instead of asserting.
     */
    const Visibility* visibility = nullptr;
};

/**
 * @brief Abstract base class for per-frame systems.
 *
 * Systems are scheduled per SystemStage and executed in stage order each
 * frame, in registration order within a stage. They read from and/or write
 * to a shared FrameContext and support init/update/fixedUpdate/shutdown
 * hooks plus runtime enable/disable.
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
         * @brief Does this system mutate ResourceManager during update()?
         *
         * When the engine runs the rendering backend on a separate thread,
         * systems are split into a "pre-wait" phase (overlaps with the
         * previous frame's render) and a "post-wait" phase (runs after
         * render finishes, safe to mutate resources). Mutating
         * ResourceManager while the render thread is reading it is a race.
         *
         * Defaults to TRUE (conservative): forgetting to override means
         * "no overlap", not "race condition". Override to false in
         * systems verified to only read ResourceManager during update.
         */
        virtual bool mutatesResources() const { return true; }

        /**
         * @brief Does this system have a follow-up step that must run on
         *        the thread holding the rendering backend's context?
         *
         * Defaults to false. Override true if executeBackend() does real
         * work; the engine routes those calls to the render thread when
         * one is active, and runs them inline on the main thread otherwise.
         *
         * Today only EditorSystem opts in: ImGui's build phase
         * (NewFrame + panel draws + Render) runs on the main thread during
         * update(), but the actual draw submission must happen wherever
         * the rendering API context lives.
         */
        virtual bool hasBackendWork() const { return false; }

        /**
         * @brief Backend-thread companion to update(). See hasBackendWork()
         *        for when it's called. Runs after RenderSystem::executeFrame
         *        inside the render thread's per-frame lambda (so the system's
         *        work overlays the rendered scene), and on main in
         *        single-threaded mode (right after update()).
         */
        virtual void executeBackend(FrameContext& /*ctx*/) {}

        bool isEnabled() const { return m_enabled; }
        void setEnabled(bool enabled) { m_enabled = enabled; }

    protected:
        System() = default;

    private:
        bool m_enabled = true;
};

} // namespace Engine

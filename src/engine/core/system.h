#pragma once

#include <cstdint>

namespace Engine {
    class WindowManager;
    class Clock;

    class Scene;
    class ResourceManager;
    struct Visibility;
}

namespace Engine {

/**
 * @brief Named per-frame execution stages.
 *
 * Each system is registered at exactly one stage. Stages run in declaration
 * order each frame; within a stage, systems run in registration order.
 *
 * The structure mirrors how a frame actually flows:
 *   Input        -> poll devices, capture/handle input (e.g., CameraControllerSystem)
 *   Simulation   -> state mutations: events, async loading, gameplay/scripts
 *                   (BehaviorSystem), animation, physics
 *   Transform    -> derive world-space data from local Transforms (HierarchySystem)
 *   Visibility   -> culling against the derived world state (VisibilitySystem)
 *   Render       -> submit draw commands (RenderSystem)
 *   UI           -> overlays, editor (EditorSystem)
 *
 * fixedUpdate() observes the same ordering (rarely matters in practice).
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
 * @brief Per-frame state bundle passed to every system.
 *
 * A uniform handle to the shared per-frame state: the window, the frame Clock,
 * the Scene and ResourceManager, and the visibility snapshot. Time is read
 * through the Clock - getDeltaTime() for real-time work, getSimDelta() for
 * simulation update(), getFixedStep() in fixedUpdate(). visibility is a
 * non-owning pointer to storage owned by VisibilitySystem (null until that
 * system runs this frame), so systems read culling results without a per-frame
 * allocation.
 */
struct FrameContext {
    WindowManager& window;
    Clock& clock;

    Scene& scene;
    ResourceManager& resources;
    const Visibility* visibility;
};

/**
 * @brief Abstract base class for per-frame systems.
 *
 * Systems are scheduled per SystemStage and executed in stage order each
 * frame, in registration order within a stage. They read from and/or write
 * to a shared FrameContext and support init/update/fixedUpdate/shutdown hooks.
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
         * @brief Whether this system implements fixedUpdate().
         *
         * Override and return true in any system that provides a real
         * fixedUpdate body. The fixed-step accumulator loop checks this so it
         * skips systems with no real fixedUpdate body instead of dispatching an
         * empty virtual across every registered system every tick. Default false
         * matches the default empty fixedUpdate.
         */
        virtual bool hasFixedUpdate() const { return false; }

        /**
         * @brief Called once after all systems are registered, before the first update.
         * @param ctx The shared FrameContext for initialization.
         */
        virtual void init(FrameContext& ctx) {}

        /**
          * @brief Called once on engine shutdown.
          */
        virtual void shutdown() {}

        /**
         * @brief Execute this system for the current frame.
         * @param ctx The shared FrameContext for this frame.
         */
        virtual void update(FrameContext& ctx) = 0;

        /**
         * @brief Execute this system at the fixed simulation rate.
         *
         * Called 0+ times per render frame, driven by an accumulator in the main
         * loop. Use ctx.clock.getFixedStep() for the step length. Intended for deterministic
         * simulation (physics, networking tick). Empty default; opt in by override.
         *
         * Note: fixedUpdate runs BEFORE update each frame, so ctx.visibility is
         * either null (first frame) or stale (previous frame's result). Don't
         * rely on it from fixedUpdate.
         */
        virtual void fixedUpdate(FrameContext& ctx) {}

    protected:
        System() = default;
};

} // namespace Engine

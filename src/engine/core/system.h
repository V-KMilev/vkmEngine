#pragma once

#include <cstdint>

namespace Vkm::Engine {
    class WindowManager;
    class Clock;

    class Scene;
    class ResourceManager;
    class EventBus;
    class InputMap;
    class PoseBuffer;
    struct Visibility;
    struct UIDrawData;
} // namespace Vkm::Engine

namespace Vkm::Engine {

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
 * The field types encode two kinds of state. References are engine-owned
 * SERVICES, valid for the whole session: time through the Clock (getDeltaTime()
 * for real-time work, getSimDelta() in update(), getFixedStep() in
 * fixedUpdate()), `events` the gameplay bus flushed at the top of the Simulation
 * stage, `input` sampled once before any system runs so every reader agrees on
 * what is held and where the edges are.
 *
 * Pointers are per-frame PRODUCTS, null until their producer has run this frame:
 * `visibility` from VisibilitySystem, `poses` from SkeletalAnimationSystem, `ui`
 * from UISystem. Producers own the storage and reuse it across frames, so a
 * consumer must be registered after its producer - that ordering lives in
 * setupEngineApp.
 */
struct FrameContext {
    Scene&           scene;
    ResourceManager& resources;

    Clock&           clock;
    EventBus&        events;
    WindowManager&   window;
    InputMap&        input;

    const Visibility* visibility = nullptr;
    const PoseBuffer* poses      = nullptr;
    const UIDrawData* ui         = nullptr;
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
         * Override and return true in any system with a real fixedUpdate body;
         * the fixed-step loop calls fixedUpdate() only on the systems that answer
         * true, so the loop's participants are stated rather than inferred.
         * Default false matches the default empty fixedUpdate.
         */
        virtual bool hasFixedUpdate() const { return false; }

        /**
         * @brief Called once after all systems are registered, before the first update.
         */
        virtual void init(FrameContext& ctx) {}

        /**
         * @brief Called once on engine shutdown.
         */
        virtual void shutdown() {}

        /**
         * @brief Execute this system for the current frame.
         */
        virtual void update(FrameContext& ctx) = 0;

        /**
         * @brief Execute this system at the fixed simulation rate.
         *
         * Called 0+ times per render frame, driven by an accumulator in the main
         * loop. Use ctx.clock.getFixedStep() for the step length. Intended for deterministic
         * simulation (physics, networking tick). Empty default; opt in by override.
         *
         * Note: the frame context is rebuilt each frame and the fixed-step loop
         * runs before any producer stage, so ctx.visibility, ctx.poses and ctx.ui
         * are always null here. Read per-frame products from update() only.
         */
        virtual void fixedUpdate(FrameContext& ctx) {}

    protected:
        System() = default;
};

} // namespace Vkm::Engine

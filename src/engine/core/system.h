#pragma once

#include <cstdint>

#include "platform/input/input_map.h"

namespace Engine {
    class WindowManager;
    class Clock;

    class Scene;
    class ResourceManager;
    class EventBus;
    struct Visibility;
    struct UIDrawData;
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
 * Two kinds of state share this struct, and the field types encode which is
 * which:
 *
 * References are engine-owned SERVICES - always valid, stable across the whole
 * session. Time is read through the Clock: getDeltaTime() for real-time work,
 * getSimDelta() for simulation update(), getFixedStep() in fixedUpdate().
 * `events` is the gameplay pub/sub bus (owned by the Engine, flushed at the
 * top of the Simulation stage). `input` resolves named actions from the frame's
 * physical input; it is sampled once before any system runs, so every reader
 * agrees on what is held and where the edges are.
 *
 * Pointers are per-frame PRODUCTS - computed by one stage and consumed by a
 * later one, null until their producer has run this frame. `visibility` points
 * at the VisibilitySystem's culling result; `ui` at the UISystem's draw list.
 * Producers own the storage (reused across frames), so consumers read the
 * results without a per-frame allocation. A consumer of a product must be
 * registered after its producer - that ordering lives in setupEngineApp.
 */
struct FrameContext {
    Scene&           scene;
    ResourceManager& resources;

    Clock&           clock;
    EventBus&        events;
    WindowManager&   window;
    InputMap&        input;

    const Visibility* visibility = nullptr;
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

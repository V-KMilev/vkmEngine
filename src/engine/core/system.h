#pragma once

#include <cstdint>

namespace Vkm::Engine {
    class WindowManager;
    class Clock;
    class HostChrome;

    class Scene;
    class ResourceManager;
    class EventBus;
    class InputMap;
    class NetSession;
    class PoseBuffer;
    struct Visibility;
    struct UIDrawData;
    struct SplashFrame;
}

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
 * SERVICES, valid for the whole session: time through the Clock, `events` the
 * gameplay bus flushed at the top of the Simulation stage, `input` sampled once
 * before any system runs so every reader agrees on the edges, `chrome` what an
 * authoring host has said about the frame it draws over - nothing at all, in a
 * shipped game.
 *
 * A system reads the timeline its responsibility lives on, not the one its stage
 * sits in. Simulation state - animation, particles, physics, onUpdate - reads
 * getSimDelta() and getFixedStep(), which is what makes pause, single-step and
 * time-scale reach all of it without a case of their own. Presentation and
 * services run every frame regardless, because pausing a game must not cut its
 * music or freeze the menu asking whether to quit.
 *
 * Pointers are per-frame PRODUCTS, null until their producer has run this frame:
 * `visibility` from VisibilitySystem, `poses` from SkeletalAnimationSystem, `ui`
 * from UISystem. Producers own the storage and reuse it across frames, so a
 * consumer is registered after its producer - that ordering lives in
 * setupEngineApp.
 */
struct FrameContext {
    Scene&           scene;
    ResourceManager& resources;

    Clock&           clock;
    EventBus&        events;
    WindowManager&   window;
    InputMap&        input;
    NetSession&      net;
    HostChrome&      chrome;

    const Visibility*  visibility = nullptr;
    const PoseBuffer*  poses      = nullptr;
    const UIDrawData*  ui         = nullptr;
    const SplashFrame* splash     = nullptr;
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
         * @brief Whether this system may be re-run over a tick that already happened.
         *
         * A client predicts its own character forward and is later told what
         * the server made of the same input. When the two disagree it re-runs
         * every tick since from the server's answer, and a system that takes
         * part in that must be a pure function of the world and the command:
         * given the same state and the same input it does the same thing, and
         * doing it twice is the same as doing it once.
         *
         * A system that fires anything - an event, a sound, an animation
         * advancing by a fixed step - answers false and simply does not run in
         * the replay pass. Its effect already happened on the live tick; doing
         * it again is a footstep played twice for one step taken.
         *
         * Default false, and the default is the statement: a system opts in
         * once somebody has reasoned about what re-running it means.
         */
        virtual bool isReplayed() const { return false; }

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
         *
         * Empty by default, like fixedUpdate: a system runs on the frame clock,
         * the tick, or both, and one that runs only on the tick has nothing to
         * say here.
         */
        virtual void update(FrameContext& ctx) {}

        /**
         * @brief Execute this system at the fixed simulation rate.
         *
         * Called 0+ times per render frame, driven by an accumulator in the main
         * loop. Use ctx.clock.getFixedStep() for the step length. Intended for deterministic
         * simulation (physics, networking tick). Empty default; opt in by override.
         *
         * The frame context is rebuilt each frame and the fixed-step loop runs
         * before any producer stage, so ctx.visibility and ctx.ui are null here
         * - read those from update() only. ctx.poses is the exception, and it is
         * one because a pose is simulation: SkeletalAnimationSystem fills it in
         * its own fixedUpdate, so a system registered after it in the Simulation
         * stage reads this tick's pose rather than last frame's.
         */
        virtual void fixedUpdate(FrameContext& ctx) {}

    protected:
        System() = default;
};

} // namespace Vkm::Engine

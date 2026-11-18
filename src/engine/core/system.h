#pragma once

#include <cstdint>

#include "core/frame_context.h"

namespace Vkm::Engine {

/**
 * @brief Named per-frame execution stages.
 *
 * Stages run in declaration order, in update() and fixedUpdate() alike; within
 * one, systems run in registration order. Input acts on input Engine::run
 * sampled before any stage; Simulation mutates state (events, loading, gameplay,
 * animation, physics); Editor is the authoring host's own UI.
 */
enum class SystemStage : uint8_t {
    Input        = 0,
    Simulation   = 1,
    Transform    = 2,
    Visibility   = 3,
    Render       = 4,
    Editor       = 5,

    Count  ///< Number of stages. Keep last.
};

/**
 * @brief Abstract base class for per-frame systems.
 *
 * Scheduled per SystemStage; see SystemStage for ordering.
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
         * @brief Whether this system may be re-run over a tick that already happened.
         *
         * When a client's prediction disagrees with the server, every tick since
         * is re-run. A replayed system must be a pure function of world and
         * command, where running twice equals running once.
         *
         * A system with effects (events, sounds, animation steps) answers false,
         * or true and leaves the effects to what it runs: BehaviorSystem replays,
         * and a behavior asks isReplaying() before it presents.
         *
         * @return True to take part in the replay pass.
         */
        virtual bool isReplayed() const { return false; }

        /**
         * @brief Called once after all systems are registered, before the first update.
         *
         * @param ctx The first frame's context; its products are all null.
         */
        virtual void init(FrameContext& ctx) {}

        /**
         * @brief Called once on engine shutdown.
         */
        virtual void shutdown() {}

        /**
         * @brief Execute this system for the current frame.
         *
         * @param ctx This frame's services and the products published so far.
         */
        virtual void update(FrameContext& ctx) {}

        /**
         * @brief Execute this system at the fixed simulation rate.
         *
         * Called 0+ times per frame; the step is ctx.clock.getFixedStep().
         *
         * Runs before any producer stage, so ctx.visibility and ctx.ui are null
         * here. ctx.poses is set by SkeletalAnimationSystem's own fixedUpdate, so
         * a later Simulation system reads this tick's pose.
         *
         * @param ctx This frame's services; see above for which products are set.
         */
        virtual void fixedUpdate(FrameContext& ctx) {}

    protected:
        System() = default;
};

} // namespace Vkm::Engine

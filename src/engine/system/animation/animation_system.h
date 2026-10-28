#pragma once

#include <cstdint>
#include <vector>

#include "core/system.h"
#include "ecs/entity.h"
#include "system/physics/body_pose.h"

namespace Vkm::Engine {

struct Animation;
struct Transform;

/**
 * @brief Advances every Animation component and writes its result into the entity's local Transform.
 *
 * Runs on the fixed step, so a paused frame does not re-sample and clobber an authored Transform.
 * Runs in SystemStage::Simulation, ahead of HierarchySystem, which resolves what it writes that frame.
 * A Kinematic Rigidbody it moves is given the velocities it moved at, and none once its clip stops,
 * so what it pushes is woken and what rides it is carried (docs/reference/physics.md).
 */
class AnimationSystem : public System {
    public:
        AnimationSystem() = default;
        ~AnimationSystem() override = default;

        AnimationSystem(const AnimationSystem& other) = delete;
        AnimationSystem& operator=(const AnimationSystem& other) = delete;

        AnimationSystem(AnimationSystem && other) = delete;
        AnimationSystem& operator=(AnimationSystem && other) = delete;

    public:
        void fixedUpdate(FrameContext& ctx) override;

        /**
         * @brief Pose @p transform as @p animation stands at its playhead.
         *
         * An empty track leaves its channel as authored. Callable while the world is not stepping, to
         * show a scrubbed playhead.
         *
         * @param animation The clip and its playhead.
         * @param transform Pose written.
         */
        static void applyAnimation(const Animation& animation, Transform& transform);

    private:
        /// A Kinematic body a clip moves, as the tick found it.
        struct Driven {
            EntityId id;
            uint32_t slot     = 0;      ///< Its Animation's dense slot.
            bool     starting = false;  ///< Whether its clip starts this tick (playOnStart).
            float    time     = 0.0f;   ///< Its playhead.
            BodyPose before;            ///< Its world pose.
        };

    private:
        std::vector<Driven> m_driven;  ///< This tick's; scratch.
};

} // namespace Vkm::Engine

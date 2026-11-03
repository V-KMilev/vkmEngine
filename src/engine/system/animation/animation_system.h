#pragma once

#include <cstdint>
#include <vector>

#include "core/system.h"
#include "ecs/entity.h"
#include "core/frame/body_pose.h"

namespace Vkm::Engine {

struct Animation;
struct Transform;

/**
 * @brief Advances every Animation component and writes its result into the entity's local Transform.
 *
 * Runs on the fixed step, so a paused frame does not re-sample and clobber an authored Transform.
 * Runs in SystemStage::Simulation, ahead of HierarchySystem, which resolves what it writes that frame.
 * A Kinematic Rigidbody it moves is given the velocities it moved at, for the channels its clip has
 * tracks for, and none once its clip stops, so what it pushes is woken and what rides it is carried.
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
         * @brief Pose @p transform as @p animation stands at @p time.
         *
         * An empty track leaves its channel as authored. Callable while the world is not stepping, to
         * show a scrubbed playhead.
         *
         * @param animation The clip.
         * @param time Seconds into it; the playhead, `animation.time`, for the pose it shows now.
         * @param transform Pose written.
         */
        static void poseAt(const Animation& animation, float time, Transform& transform);

    private:
        /**
         * @brief A Kinematic body a clip moves, as the tick found it.
         */
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

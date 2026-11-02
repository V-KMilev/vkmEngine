#pragma once

#include <cstdint>
#include <vector>

#include "core/system.h"
#include "debug/error/fault_latch.h"
#include "ecs/entity.h"
#include "ecs/scene.h"

namespace Vkm::Engine {

class Scene;
struct Ragdoll;

/**
 * @brief Keeps a ragdoll's bodies in step with whichever is in charge.
 *
 * The bodies exist, and so simulate, whether or not the ragdoll is active. Inactive, they are kinematic
 * hitboxes (see isPosedByAnimation) placed from the animated pose every tick with its velocity, so
 * activation hands the solver the character's real shape and motion.
 *
 * Runs on the tick, after SkeletalAnimationSystem, whose pose it reads, and before PhysicsSystem, so the
 * solver reads these bone transforms the same step.
 */
class RagdollSystem : public System, public ISceneObserver {
    public:
        RagdollSystem() = default;
        ~RagdollSystem() override = default;

        RagdollSystem(const RagdollSystem& other) = delete;
        RagdollSystem& operator=(const RagdollSystem& other) = delete;

        RagdollSystem(RagdollSystem && other) = delete;
        RagdollSystem& operator=(RagdollSystem && other) = delete;

    public:
        void init(FrameContext& ctx) override;
        void fixedUpdate(FrameContext& ctx) override;

        /// Drops the observer registration, so nothing calls a dead system.
        void shutdown() override;

        /**
         * @brief Destroy a ragdoll's bones when the thing they belong to goes.
         *
         * A plain destroyEntity does not walk children, and a bone moved out of the group escapes one that
         * does; an observer catches every destroy, gameplay's included.
         *
         * @param id The entity being destroyed.
         */
        void onEntityDestroyed(EntityId id) override;

    private:
        Scene*     m_scene = nullptr;  ///< For removing the observer registration
        FaultLatch m_scaled;           ///< A ragdoll built for its rig at another scale
};

} // namespace Vkm::Engine

#pragma once

#include <cstdint>
#include <vector>

#include "core/system.h"
#include "debug/fault_latch.h"
#include "ecs/entity.h"
#include "ecs/scene_observer.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Which ragdoll a body belongs to, and which bone it is.
 *
 * Maps a hit body back to its bone. A search, so no per-limb component has to stay true through save,
 * load and rebuild.
 *
 * @param scene Scene to search.
 * @param body The entity a query returned.
 * @param[out] outBone Index into the rig's bone array, when there is one.
 * @return The entity carrying the Ragdoll, or an invalid id.
 */
EntityId ragdollOwnerOf(const Scene& scene, EntityId body, int32_t* outBone = nullptr);

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

/**
 * @brief Is @p entity a ragdoll bone the animation is placing this tick?
 *
 * Such a bone is a hitbox rather than a body; the rule is stated here once (markPosedByAnimation is the
 * whole-world form). False for an active ragdoll.
 *
 * @param scene World holding the ragdolls.
 * @param entity Entity asked about; a dead one is never posed.
 * @return True when @p entity is a live bone of an inactive ragdoll.
 */
bool isPosedByAnimation(const Scene& scene, EntityId entity);

/**
 * @brief isPosedByAnimation for a whole world, in one walk of its ragdolls.
 *
 * Asking per entity walks every ragdoll each time; a caller asking of many marks them all once.
 *
 * @param scene  World whose inactive ragdolls are walked.
 * @param bySlot True at the slot of every posed bone; a slot past its end is not one.
 */
void markPosedByAnimation(const Scene& scene, std::vector<bool>& bySlot);

} // namespace Vkm::Engine

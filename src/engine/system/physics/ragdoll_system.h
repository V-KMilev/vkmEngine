#pragma once

#include <cstdint>

#include "core/system.h"
#include "ecs/entity.h"
#include "ecs/scene_observer.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Which ragdoll a body belongs to, and which bone it is.
 *
 * The way back from a hit. A ragdoll's bones are ordinary bodies with ordinary
 * colliders, so a query already picks one out - and on its own that is an
 * entity called "legR" with nothing saying whose leg it is, which is not enough
 * to do anything with. This closes the loop: the ragdoll maps bones to bodies,
 * and this maps a body back.
 *
 * A search rather than a back-reference on each body, because a scene holds a
 * handful of ragdolls of a dozen bones each and the alternative is a component
 * per limb that has to be kept true through every save, load and rebuild.
 *
 * @param scene Scene to search.
 * @param body The entity a query returned.
 * @param[out] outBone Index into the rig's bone array, when there is one.
 * @return The entity carrying the Ragdoll, or an invalid id.
 */
EntityId ragdollOwnerOf(const Scene& scene, EntityId body,
                        int32_t* outBone = nullptr);

/**
 * @brief Keeps a ragdoll's bodies in step with whichever is in charge.
 *
 * A ragdoll's bodies exist whether or not it is active, and a body that exists
 * is simulated - so without this, building one drops a skeleton on the floor
 * beside a character still standing. `active` gated the pose and nothing else,
 * so the component's own switch did half of what it said.
 *
 * Inactive, the bodies are kinematic and placed from the animated pose every
 * frame. That is what makes the transition work: switching to active hands the
 * solver a skeleton already in the shape the character was in, rather than one
 * in its bind pose that snaps before it falls.
 *
 * Registered at SystemStage::Simulation after SkeletalAnimationSystem, whose
 * pose it reads, and before PhysicsSystem, whose bodies it writes. All three
 * run on the tick, so the bone transforms this writes are the ones the solver
 * reads in the same step rather than whatever the last frame left.
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
        /// Simulation runs on the tick; nothing here answers to the frame.
        void fixedUpdate(FrameContext& ctx) override;
        bool hasFixedUpdate() const override { return true; }

        /// Drops the observer registration, so nothing calls a dead system.
        void shutdown() override;

        /**
         * @brief Destroy a ragdoll's bones when the thing they belong to goes.
         *
         * The bones live under a group node inside the character, and a
         * hierarchy teardown takes them along - but a plain destroyEntity
         * does not walk children, and a bone something moved out of the group
         * is not covered by one that does. This observer is what guarantees
         * the bones die with the owner however the owner dies.
         *
         * So the hierarchy cannot take them along, and without this a deleted
         * character leaves its skeleton lying in the scene. Registered as an
         * observer rather than handled in the editor's delete, because gameplay
         * destroys things too and only one of those paths goes through a menu.
         *
         * @param id The entity being destroyed.
         */
        void onEntityDestroyed(EntityId id) override;

    private:
        Scene* m_scene = nullptr;   ///< For removing the observer registration
};

} // namespace Vkm::Engine

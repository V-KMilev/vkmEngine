#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief One bone of a ragdoll, and the body that moves it.
 *
 * A body covers its bone rather than sitting on it (a thigh capsule's origin is mid-thigh, the bone's is
 * the hip), so `bodyFromBone` is recorded at build and the pose is the body's transform put back through
 * it. It carries the rig's scale at that build (Ragdoll::rigScale): a rig that changes scale wants its
 * ragdoll rebuilt.
 */
struct RagdollBone {
    int32_t   bone         = -1;               ///< Index into the rig's bone array
    EntityId  body{};                          ///< The entity simulating it
    glm::mat4 bodyFromBone = glm::mat4(1.0f);  ///< Bone space -> body space: the bone as the body sees it

    /**
     * @brief Where the pose put the body last tick, world space; runtime, not serialized.
     *
     * This tick's pose against it is the velocity a driven body hands the solver when the ragdoll takes
     * over. `posed` is false until a pose has been applied since the bones were last handed to the solver.
     */
    glm::vec3 lastPosition = {0.0f, 0.0f, 0.0f};
    glm::quat lastRotation = {1.0f, 0.0f, 0.0f, 0.0f};
    bool      posed        = false;
};

/**
 * @brief Turns a rig over to physics, and back.
 *
 * Ordinary bodies and joints shaped like a skeleton, plus which body poses which bone, so one skinned
 * mesh can be driven by a clip one frame and the solver the next.
 *
 * `active` is the whole switch. False holds the bodies kinematic, placed from the animated pose every
 * tick at its speed, so activation inherits the character's real shape and motion. The bones are
 * siblings under `root`, a child of this entity, not nested, so a hip cannot move a shin behind the
 * solver's back.
 */
struct Ragdoll {
    bool     active = false;  ///< Physics drives the rig when true
    EntityId root{};          ///< Node the bones are grouped under

    /**
     * @brief The layer bit the bones were built on.
     *
     * The build takes it out of the owner's collidesWith; this is what puts it back when the ragdoll goes.
     */
    int boneLayer = 1 << 1;

    /**
     * @brief The rig's world scale when the bones were built.
     *
     * Every bone's offset carries it. Recorded so a scale change is checked against the rig, not a posed
     * bone an animation may scale on purpose.
     */
    float rigScale = 1.0f;
    std::vector<RagdollBone> bones;     ///< One per simulated bone

    /// The pose held the bones last tick; runtime, not serialized.
    bool held = false;
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Ragdoll)
    VKM_F(active)
    VKM_F(boneLayer)
    VKM_F(rigScale)
VKM_REFLECT_END()

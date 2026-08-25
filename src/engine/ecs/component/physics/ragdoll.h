#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

/**
 * @brief One bone of a ragdoll, and the body that moves it.
 *
 * `boneFromBody` is what makes the two agree. A body is placed to cover a bone,
 * not to sit on it: a thigh's capsule spans hip to knee and its origin is the
 * middle, where the bone's is the hip. Recorded once when the ragdoll is built,
 * so the pose is the body's transform put back through it rather than an
 * assumption that the two coincide.
 */
struct RagdollBone {
    int32_t   bone = -1;                     ///< Index into the rig's bone array
    EntityId  body{};                        ///< The entity simulating it
    glm::mat4 boneFromBody = glm::mat4(1.0f);  ///< Body space -> bone space
};

/**
 * @brief Turns a rig over to physics, and back.
 *
 * A ragdoll is not a mode of the animation system; it is a set of ordinary
 * bodies and joints that happens to be shaped like a skeleton. Nothing in the
 * solver knows one is there. What the component adds is the mapping back: which
 * body poses which bone, so the same skinned mesh can be driven by a clip one
 * frame and by the solver the next.
 *
 * `active` is the whole switch, and RagdollSystem is what makes it one. False
 * holds the bodies kinematic and places them from the animated pose every
 * frame - so they exist without falling, and the frame it is switched on the
 * solver inherits the shape the character was actually in rather than a bind
 * pose that snaps first. That is also what lets a character die and get up
 * again without the rig being rebuilt.
 *
 * `root` is the node the bones hang under, a child of the entity the component
 * is on. A rig belongs inside the character it is a rig for, and one node to
 * hold it keeps a dozen limbs from burying whatever else the character owns.
 * The bones are siblings under it rather than nested bone-inside-bone: they are
 * independent bodies that joints hold together, and nesting them would let a
 * hip move a shin behind the solver's back.
 */
struct Ragdoll {
    bool active = false;                ///< Physics drives the rig when true
    EntityId root{};                    ///< Node the bones are grouped under

    /**
     * @brief The layer bit the bones were built on.
     *
     * Recorded because the build takes it out of the owner's collidesWith, and
     * something has to put it back when the ragdoll goes. Without it a
     * character that was once given a ragdoll and then cleared is a character
     * that silently stopped colliding with a layer, in a field the author can
     * see and did not change.
     */
    int boneLayer = 1 << 1;
    std::vector<RagdollBone> bones;     ///< One per simulated bone
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Ragdoll)
    VKM_F(active)
VKM_REFLECT_END()

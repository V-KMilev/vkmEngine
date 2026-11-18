#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "base/reflect/reflect.h"
#include "ecs/component/scene/transform.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

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

    glm::vec3 lastPosition = {0.0f, 0.0f, 0.0f};  ///< Where the pose put the body's center of mass last tick,
                                                  ///< world space; runtime, not serialized. This tick's pose
                                                  ///< against it is the velocity a driven body hands the
                                                  ///< solver when the ragdoll takes over. `posed` is false
                                                  ///< until a pose has been applied since the bones were last
                                                  ///< handed to the solver.
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
    static constexpr int BONE_LAYER = 1 << 1;  ///< The collision layer bit the bones are on. The build takes
                                               ///< it out of the owner's mask, so the bones and the collider
                                               ///< they sit in never push each other, and out of the bones'
                                               ///< own, as overlapping limbs would spend the first tick
                                               ///< resolving their authored interpenetration; clearRagdoll
                                               ///< puts it back. Everything else hits them, as hit boxes
                                               ///< while inactive.

    bool     active = false;  ///< Physics drives the rig when true
    EntityId root{};          ///< Node the bones are grouped under

    float                    rigScale = 1.0f;  ///< The rig's world scale when the bones were built. Every
                                               ///< bone's offset carries it. Recorded so a scale change is
                                               ///< checked against the rig, not a posed bone an animation may
                                               ///< scale on purpose.
    std::vector<RagdollBone> bones;            ///< One per simulated bone

    bool held = false;  ///< The pose held the bones last tick; runtime, not serialized.

    glm::mat4 pinnedWorld = glm::mat4(1.0f);  ///< Where `root` stands in the world while live, so the bones
                                              ///< under it stay where the solver puts them however this
                                              ///< entity is moved; runtime, not serialized.
    Transform rootRest;                       ///< `root`'s own Transform as it went live, put back when it
                                              ///< stands down.
    bool      pinned      = false;            ///< Whether pinnedWorld holds.
};

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
 * @brief Destroy a ragdoll's bone bodies, then the node they are grouped under.
 *
 * The bones go one by one first, since a scene may have moved one out from under the node, and the
 * node goes whole, since a plain destroyEntity leaves its children. The Ragdoll itself is the caller's.
 *
 * @param scene   Scene holding them.
 * @param ragdoll The ragdoll whose bodies go; its own entity is untouched.
 */
void destroyRagdollBodies(Scene& scene, const Ragdoll& ragdoll);

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

VKM_REFLECT_BEGIN(::Vkm::Engine::Ragdoll)
    VKM_F(active, "On, physics poses the rig; off, the bodies follow the animation")
    VKM_F(rigScale)
VKM_REFLECT_END()

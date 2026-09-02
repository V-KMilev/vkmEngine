#pragma once

#include <cstdint>

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Is @p entity inside a prefab instance rather than being its root?
 *
 * Such an entity is deliberately not replicated. Both ends build the subtree
 * from the same prefab file, but each allocates its children's slots locally,
 * so the two ends disagree about what slot 57 is - and an entity's slot is its
 * name on the wire. The root replicates, the hierarchy carries the rest, and
 * anything that animates the subtree runs on both ends from the same data.
 *
 * The consequence worth knowing: a spawned prefab's children are posed by
 * whatever drives them locally. An animator does that correctly on both ends. A
 * ragdoll does not - the server's bones fall and the client's do not hear about
 * it - so a ragdoll belongs on a character the scene author placed, where the
 * slots match, until entities inside an instance have a name of their own.
 */
bool isInsidePrefabInstance(const Scene& scene, EntityId entity);

/**
 * @brief Is @p entity a ragdoll bone the animation is placing this tick?
 *
 * Such a bone is deliberately not replicated, and this is asked on both ends
 * for the same reason: the sender has nothing to say about it and the receiver
 * must not be told, because each is already working it out.
 *
 * While a ragdoll is inactive its bodies are kinematic and placed from the
 * animated pose every tick (`RagdollSystem`), and the clip is chosen from the
 * body's velocity, which does replicate. So both ends derive the same skeleton
 * from the same input, and sending it spends the packet describing what the
 * receiver was about to work out anyway. Measured on physics_lab, where four
 * characters carry twenty bones each: 96% of every snapshot, leaving the props
 * and the other players to compete for what was left.
 *
 * Active, this is false and the bones replicate. The solver drives them then
 * and the pose follows them rather than the other way round, so they are the
 * answer instead of a copy of it - which is also why `Ragdoll::active` is on
 * the wire: a client that missed the switch would pose twenty bones from a walk
 * cycle while the server had a body falling.
 *
 * The phases need not match. Each end advances its own clip time, so two ends
 * hold the same walk at different points in it - invisible, because a kinematic
 * bone decides nothing while it is inactive, and answered by the first snapshot
 * after the ragdoll switches on.
 */
bool isPosedByAnimation(const Scene& scene, EntityId entity);

/**
 * @brief Is @p entity a body that cannot move?
 *
 * A static body never moves, so both ends hold it exactly as the scene file
 * wrote it and nothing that happens in the game can make them disagree.
 * Describing it is not merely wasted - it is worse than saying nothing, because
 * what comes back is the quantised copy rather than the authored value, and
 * that copy replaces a value which was right.
 *
 * The cost is not uniform, and that is what makes it matter. A rotation is
 * quantised as an angle, so the error it carries grows with the size of the
 * thing turned: on a crate a fraction of a degree is invisible, and on a floor
 * sixty metres across the same fraction lifts one corner and drops the other by
 * ten centimetres. A character standing on the low end then finds nothing
 * beneath it and falls.
 */
bool isImmovable(const Scene& scene, EntityId entity);

/**
 * @brief Why an entity is not described on the wire, or None when it is.
 *
 * The three rules above, asked once. `writeSnapshot` reads it to decide what to
 * skip and the editor reads it to tell an author what the other end will see,
 * and there is one of them so those two cannot come to different answers - a
 * panel that says a body replicates while the sender quietly drops it is worse
 * than a panel that says nothing.
 */
enum class NetSilence : uint8_t {
    None,                  ///< It is on the wire.
    InsidePrefabInstance,  ///< It has no slot the other end would recognise.
    PosedByAnimation,      ///< Both ends work it out; see isPosedByAnimation.
    Immovable,             ///< It cannot move, and the scene file is exact.
    Count
};

/// Which of the rules keeps @p entity off the wire, if any.
NetSilence netSilence(const Scene& scene, EntityId entity);

/**
 * @brief What to tell an author about @p reason.
 *
 * @param reason Why the entity is not described.
 * @return The explanation, or nullptr when the entity does replicate and there
 *         is nothing to say.
 */
const char* toString(NetSilence reason);

} // namespace Vkm::Engine

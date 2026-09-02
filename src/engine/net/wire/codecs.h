#pragma once

#include "ecs/component/core/transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "net/wire/bit_stream.h"

namespace Vkm::Engine {

/**
 * @brief How far from the origin a replicated position can be, in metres.
 *
 * Half a kilometre each way, which covers every world the engine has been
 * pointed at and costs twenty bits a coordinate at millimetre resolution. A
 * body past it is described at the boundary rather than dropped, because a
 * body that has left the world is already a bug in the game and refusing the
 * packet it rode in would punish every other body in it.
 */
constexpr float NET_WORLD_EXTENT = 512.0f;

/**
 * @brief A transform, as the wire carries it.
 *
 * Position and rotation always; scale only when it is not one, which is a bit
 * for the overwhelming case and ninety-six for the rare project that animates
 * it. Local rather than world, because the hierarchy runs on both ends and
 * sending world transforms would replicate a parent's motion once per child.
 */
void netEncode(const Transform& value, BitWriter& out);
void netDecode(Transform& value, BitReader& in);

/**
 * @brief The part of a body that changes as it is simulated.
 *
 * Velocities and the sleep flag. Mass, damping, friction and the rest are
 * authoring properties: both ends read them from the same scene file, so
 * sending them would be sending a constant sixty-four times a second. The
 * consequence is worth stating plainly - a script that changes a body's mass at
 * runtime changes it on that end alone.
 *
 * Each velocity carries a presence bit and is written only when it is large
 * enough to matter, so a body drifting to rest stops paying for its own
 * velocity before it stops paying for its position.
 */
void netEncode(const Rigidbody& value, BitWriter& out);
void netDecode(Rigidbody& value, BitReader& in);

/**
 * @brief Whether the character is standing on something, and nothing else.
 *
 * The one thing about a character that an end which does not simulate it cannot
 * work out for itself. The contact under a resting capsule exists because
 * gravity renews it every tick; a client applies no gravity to a body it does
 * not decide, and holds its position only to the millimetre the wire carries -
 * which is coarser than the contact. So it is decided by the authority and sent.
 *
 * Everything else about the component stays local. Movement input, the step-up
 * state and the slope normal are what a simulating end works out from this and
 * from the command, and sending them would overwrite an owner's own prediction
 * with an answer a round trip old.
 */
void netEncode(const CharacterController& value, BitWriter& out);
void netDecode(CharacterController& value, BitReader& in);

/**
 * @brief Whether physics has taken the rig over, and nothing else about it.
 *
 * One bit, and it decides what the rest of the character costs. While it is
 * false the bones are placed from the animation on both ends and never travel;
 * the moment it is true they are the simulation and every one of them does. A
 * client that missed the switch would pose twenty bones from a walk cycle while
 * the server had a body falling down a staircase, and nothing later would
 * correct it - which is why the flag is on the wire and the bones are not.
 *
 * The bone list is not sent. It names entities, and both ends read the same
 * scene file, so both already have it.
 */
void netEncode(const Ragdoll& value, BitWriter& out);
void netDecode(Ragdoll& value, BitReader& in);

/**
 * @brief Register everything the engine itself replicates.
 *
 * Called once at startup, before a project's module adds its own.
 */
void registerEngineNetTypes();

} // namespace Vkm::Engine

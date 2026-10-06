#pragma once

#include <cstddef>

#include <glm/glm.hpp>

#include "ecs/component/core/transform.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/prefab/net_spawn.h"
#include "net/wire/bit_stream.h"
#include "net/wire/schema.h"

namespace Vkm::Engine {

/**
 * @brief Longest prefab path a NetSpawn carries.
 *
 * Bounded because it arrives from the network and opens a file; see isSayable.
 */
constexpr size_t NET_PREFAB_PATH_MAX = 200;

/**
 * @brief Most entities below a spawned root whose slots a NetSpawn names.
 *
 * A bigger instance is sent with none and built wherever a client's slots are
 * free, so the two ends' slots can part.
 */
constexpr size_t NET_SPAWN_MAX_SLOTS = 32;

/**
 * @brief Most bytes a NetSpawn encodes to: the path's length, the longest path,
 *        the ten floats of the pose, then the slot count and NET_SPAWN_MAX_SLOTS
 *        entries of a 32-bit uid and a 23-bit slot, rounded up.
 */
constexpr size_t NET_SPAWN_MAX_BYTES = 1 + NET_PREFAB_PATH_MAX + 10 * sizeof(float)
    + (6 + NET_SPAWN_MAX_SLOTS * (32 + 23) + 7) / 8;

/**
 * @brief NetSpawn's name in the schema.
 *
 * The one type written for an entity whose state is silent; see writeSnapshot.
 */
constexpr const char* NET_SPAWN_TYPE = "NetSpawn";

/**
 * @brief The scale a described Transform holds at both ends.
 *
 * Finite and within NET_MAX_SCALE either way: a non-finite scale becomes one,
 * a finite one past the bound is clamped. Applied by the codec both ways and
 * by a server to what it holds (NetServer::holdScalesToTheWire), so both ends
 * hold the same value.
 *
 * @param scale A Transform's scale.
 * @return The scale the wire carries for it.
 */
glm::vec3 netScale(const glm::vec3& scale);

/**
 * @brief A transform, as the wire carries it.
 *
 * Scale only when it is not one. Local, not world: the hierarchy runs on both
 * ends, and world transforms would send a parent's motion once per child.
 */
void netEncode(const Transform& value, BitWriter& out);
void netDecode(Transform& value, BitReader& in);

/**
 * @brief The part of a body that changes as it is simulated.
 *
 * Velocities and the sleep flag. Authoring properties come from the scene
 * file; a script changing mass at runtime changes it on that end alone. Each
 * velocity is written only when large enough to matter.
 */
void netEncode(const Rigidbody& value, BitWriter& out);
void netDecode(Rigidbody& value, BitReader& in);

/**
 * @brief Whether the character is standing on something, and nothing else.
 *
 * A non-simulating end cannot work it out: it applies no gravity, and the
 * wire's position precision is coarser than the contact. The rest is derived
 * locally; sending it would overwrite an owner's prediction with old news.
 */
void netEncode(const CharacterController& value, BitWriter& out);
void netDecode(CharacterController& value, BitReader& in);

/**
 * @brief Whether physics has taken the rig over, and nothing else about it.
 *
 * While false the bones are posed from animation on both ends and never
 * travel; while true every one does (see isPosedByAnimation). The bone list
 * comes from the scene file.
 */
void netEncode(const Ragdoll& value, BitWriter& out);
void netDecode(Ragdoll& value, BitReader& in);

/**
 * @brief Whether @p spawn can be said on the wire and believed at the other end.
 *
 * A client opens the path on a server's say-so, so it must be a prefab inside
 * the project: relative, ".json", at most NET_PREFAB_PATH_MAX, no ".."
 * component, backslash, colon, control character or Windows device name -
 * judged on characters, so platforms agree. The pose must be finite, with a
 * rotation of some length and a scale within NET_MAX_SCALE either way.
 *
 * @param spawn The prefab path and pose to judge.
 * @return True when both can travel and be believed.
 */
bool isSayable(const NetSpawn& spawn);

/**
 * @brief Which prefab a spawned root is, and the pose it was built at, exactly.
 *
 * The pose unquantised: said once, and a static body is never corrected
 * later. What isSayable refuses decodes with no path, building nothing; every
 * field is read either way, so the stream stays in step.
 */
void netEncode(const NetSpawn& value, BitWriter& out);
void netDecode(NetSpawn& value, BitReader& in);

/**
 * @brief Register everything the engine itself replicates.
 *
 * Called on an empty schema, before ScriptModule::setupNetwork, so the
 * engine's types take the first wire indices.
 *
 * @param schema The schema to register on; the one every end consults by default.
 */
void registerEngineNetTypes(NetSchema& schema = NetSchema::get());

} // namespace Vkm::Engine

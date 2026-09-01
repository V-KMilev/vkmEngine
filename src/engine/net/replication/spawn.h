#pragma once

#include <string>

#include "ecs/component/core/transform.h"
#include "ecs/entity.h"
#include "net/wire/bit_stream.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Longest prefab path a spawn message carries.
 *
 * A project-relative path, so this is generous. Bounded because it arrives from
 * the network and is used to open a file.
 */
constexpr size_t NET_PREFAB_PATH_MAX = 200;

/**
 * @brief What one end must be told to build the same thing the other did.
 *
 * A prefab path and where to put it, and the slot to build it at - because an
 * entity's identity here is its slot, and a thing created after the scene
 * loaded has no slot either end agreed on in advance.
 */
struct NetSpawn {
    uint32_t    slot = 0;
    std::string prefab;
    Transform   at;
};

/**
 * @brief Write @p spawn as a reliable message body, kind byte included.
 *
 * @return False when the prefab path is empty or longer than
 *         NET_PREFAB_PATH_MAX, which is what readSpawn will take. Nothing is
 *         written in that case, so a caller reports it rather than sending a
 *         message no receiver can read.
 */
bool writeSpawn(BitWriter& out, const NetSpawn& spawn);

/// Read one back. False when it is malformed or names an impossible path.
bool readSpawn(BitReader& in, NetSpawn& spawn);

/// Write a despawn message body for @p slot, kind byte included.
void writeDespawn(BitWriter& out, uint32_t slot);

} // namespace Vkm::Engine

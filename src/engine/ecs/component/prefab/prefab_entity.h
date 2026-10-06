#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief The identity an entity carries inside an instanced prefab.
 *
 * The uid an override addresses: assigned once in the prefab file, never reused, so it
 * survives reordering, renaming and edits where ids, position and Name do not.
 * Runtime only; a scene never writes a prefab's subtree.
 */
struct PrefabEntity {
    static constexpr uint32_t ROOT = 0;  ///< Fixed, so an override on the root needs no lookup.

    uint32_t uid = ROOT;
};

} // namespace Vkm::Engine

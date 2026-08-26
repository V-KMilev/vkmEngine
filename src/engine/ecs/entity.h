#pragma once

#include <cstdint>

#include "core/memory/types.h"

namespace Vkm::Engine {

/**
 * @brief Names one entity of one Scene, for as long as that slot holds it.
 *
 * A slot in the scene's allocator paired with the generation that slot held
 * when the id was minted, so a recycled slot never answers to an id issued
 * against its previous occupant.
 *
 * Its own type rather than an alias for StorageIndex, which has the same two
 * fields and a different meaning: a resource handle is built on one too, and a
 * function taking an asset slot must not silently accept an entity. It is also
 * not a number. A reference that leaves this Scene - into a file, a prefab, an
 * undo snapshot, a connection - travels as whatever that carrier calls the
 * entity, and comes back through that carrier's resolver.
 */
struct EntityId {
    StorageIndex key = {};

    /// True when this names a slot at all; index 0 is the reserved null.
    constexpr explicit operator bool() const noexcept { return bool(key); }

    constexpr bool operator==(const EntityId& other) const noexcept {
        return key == other.key;
    }
    constexpr bool operator!=(const EntityId& other) const noexcept {
        return key != other.key;
    }

    constexpr uint32_t slot() const noexcept { return key.index; }

    constexpr uint32_t generation() const noexcept { return key.generation; }
};

static_assert(sizeof(EntityId) == 8, "EntityId must be 8 bytes");

} // namespace Vkm::Engine

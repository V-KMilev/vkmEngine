#pragma once

#include <cstdint>

#include "core/memory/types.h"

namespace Engine {

/**
 * @brief Entity identifier backed by a generational StorageIndex.
 *
 * Pairs a sparse-array slot index with a generation counter, giving entities
 * the same use-after-free protection and ID recycling that resource handles
 * enjoy.
 *
 * Conceptually a cross-entity reference rather than a raw ECS slot handle, but
 * it is an alias for StorageIndex, not a distinct type: the two are freely
 * interchangeable wherever either is accepted.
 */
using EntityId = StorageIndex;

} // namespace Engine
#pragma once

#include "core/memory/types.h"

namespace Vkm::Engine {

/**
 * @brief Opaque, type-safe handle for resources, distinguished by ResourceType.
 *
 * Wraps a StorageIndex (sparse slot index + generation counter). A default-constructed
 * handle is invalid (null sentinel). Handles detect stale access via generation matching.
 *
 * @tparam ResourceType The resource type (e.g., MeshAsset, TextureAsset, MaterialAsset).
 */
template <typename ResourceType>
struct Handle {
    using resource_t = ResourceType;
    StorageIndex key = {};

    /// True when the handle names a non-null slot.
    constexpr explicit operator bool() const noexcept { return bool(key); }

    constexpr bool operator==(const Handle& other) const noexcept { return key == other.key; }

    constexpr bool operator!=(const Handle& other) const noexcept { return key != other.key; }

    /**
     * @brief Get the sparse slot index for use as map key, sorting, or logging.
     */
    constexpr uint32_t id() const noexcept { return key.index; }
};

} // namespace Vkm::Engine

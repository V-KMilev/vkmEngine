#pragma once

#include "types.h"

namespace Engine {

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
    public:
        /**
        * @brief Check if the handle is valid.
        * @return True if the handle references a non-null slot.
        */
        constexpr explicit operator bool() const noexcept { return bool(key); }

        /**
        * @brief Check if the handle is equal to another handle.
        * @param other The other handle to compare.
        * @return True if both index and generation match.
        */
        constexpr bool operator==(const Handle& other) const noexcept { return key == other.key; }

        /**
        * @brief Check if the handle is not equal to another handle.
        * @param other The other handle to compare.
        * @return True if index or generation differ.
        */
        constexpr bool operator!=(const Handle& other) const noexcept { return key != other.key; }

        /**
        * @brief Get the sparse slot index for use as map key, sorting, or logging.
        * @return The uint32_t slot index (0 = invalid).
        */
        constexpr uint32_t id() const noexcept { return key.index; }

    public:
        using resource_t = ResourceType;
        StorageIndex key = {};
};

} // namespace Engine

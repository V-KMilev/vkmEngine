#pragma once

#include <cstdint>

namespace Engine {

/**
 * @brief Opaque, type-safe handle for resources, distinguished by Tag.
 * 
 * The handle holds a simple uint32_t ID. Value 0 always means "invalid".
 * Handles can be compared and validated but do not expose resource internals.
 * 
 * @tparam Tag Empty tag type to create a unique handle type per resource (e.g., MeshTag).
 */
template <typename Tag>
struct Handle {
    public:
        /**
        * @brief Check if the handle is valid.
        * @return True if the handle is valid (nonzero), false otherwise.
        */
        constexpr explicit operator bool() const noexcept { return value != 0; }

        /**
        * @brief Check if the handle is equal to another handle.
        * @param other The other handle to compare.
        * @return True if the handles have the same value, otherwise false.
        */
        constexpr bool operator==(const Handle& other) const noexcept { return value == other.value; }

        /**
        * @brief Check if the handle is not equal to another handle.
        * @param other The other handle to compare.
        * @return True if the handle values are different, otherwise false.
        */
        constexpr bool operator!=(const Handle& other) const noexcept { return value != other.value; }

    public:
        uint32_t value = 0; ///< 0 = invalid handle, >0 = valid handle
};

struct MeshTag {};
struct MaterialTag {};
struct TextureTag {};

using MeshHandle     = Handle<MeshTag>;
using MaterialHandle = Handle<MaterialTag>;
using TextureHandle  = Handle<TextureTag>;

} // namespace Engine
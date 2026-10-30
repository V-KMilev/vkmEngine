#pragma once

namespace Vkm::Engine {

/**
 * @brief A reflected field's name and what its VKM_F row says about it (Reflect::Field).
 */
struct FieldInfo {
    const char* name = "";       ///< Null-terminated; the key it is saved under.
    const char* tip  = nullptr;  ///< What it is for, or none.
    float       min  = 0.0f;     ///< The inspector's drag range; none when min == max.
    float       max  = 0.0f;
};

} // namespace Vkm::Engine

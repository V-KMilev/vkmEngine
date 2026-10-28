#pragma once

#include <cstring>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief Component for giving entities a human-readable name.
 *
 * A fixed-size array, so it is trivially copyable with no heap allocation. Build
 * one from a C-string with makeName().
 */
struct Name {
    char value[64] = {};
};

/**
 * @brief Build a Name from a C-string, truncating into the fixed buffer.
 *
 * Always null-terminates.
 *
 * @param str C-string to copy; null yields an empty Name.
 * @return The Name holding it.
 */
inline Name makeName(const char* str) {
    Name name;
    if (str) {
        std::strncpy(name.value, str, sizeof(name.value) - 1);
        name.value[sizeof(name.value) - 1] = '\0';
    }
    return name;
}

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Name)
    VKM_F(value)
VKM_REFLECT_END()

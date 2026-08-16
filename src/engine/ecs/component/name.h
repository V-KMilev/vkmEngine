#pragma once

#include <cstring>

#include "core/reflect.h"

namespace Engine {

/**
 * @brief Component for giving entities a human-readable name.
 *
 * Fixed-size char array to keep SparseSet storage cache-friendly: no heap
 * allocation, safe to memcpy, trivially copyable. A plain aggregate like every
 * other component - build one from a C-string with makeName().
 */
struct Name {
    char value[64] = {};
};
/**
 * @brief Build a Name from a C-string, truncating safely into the fixed buffer.
 *
 * Copies at most sizeof(Name::value) - 1 bytes and always null-terminates; a
 * null @p str yields an empty Name.
 *
 * @param str Source C-string to copy, or nullptr for an empty name.
 * @return A Name holding the (possibly truncated) text.
 */
inline Name makeName(const char* str) {
    Name name;
    if (str) {
        std::strncpy(name.value, str, sizeof(name.value) - 1);
        name.value[sizeof(name.value) - 1] = '\0';
    }
    return name;
}

} // namespace Engine

VKM_REFLECT_BEGIN(::Engine::Name)
    VKM_F(value)
VKM_REFLECT_END()

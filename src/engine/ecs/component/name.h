#pragma once

#include <cstring>

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

/// Build a Name from a C-string, truncating safely into the fixed buffer.
inline Name makeName(const char* str) {
    Name name;
    if (str) {
        std::strncpy(name.value, str, sizeof(name.value) - 1);
        name.value[sizeof(name.value) - 1] = '\0';
    }
    return name;
}

} // namespace Engine

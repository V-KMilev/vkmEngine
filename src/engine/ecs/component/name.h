#pragma once

#include <cstring>

namespace Engine {

/**
 * @brief Component for giving entities a human-readable name.
 *
 * Fixed-size char array to keep SparseSet storage cache-friendly.
 * No heap allocation - safe to memcpy, trivially copyable.
 */
struct Name {
    char value[64] = {};

    Name() = default;

    Name(const char* str) {
        if (str) {
            std::strncpy(value, str, sizeof(value) - 1);
            value[sizeof(value) - 1] = '\0';
        }
    }
};

} // namespace Engine

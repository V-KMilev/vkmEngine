#pragma once

// Read ahead of xatlas.cpp (modules/CMakeLists.txt): its unqualified acosf, sinf, cosf, atan2f and qsort,
// all inside xatlas::internal, find these before the C library's, so a lightmap unwrap - and the mesh a
// cook writes from it - is the same on every platform.

#include <cstddef>
#include <utility>

#include "portable_math.h"

namespace xatlas::internal {

inline float acosf(float x) { return static_cast<float>(Vkm::Engine::PortableMath::acos(x)); }
inline float sinf(float x) { return static_cast<float>(Vkm::Engine::PortableMath::sin(x)); }
inline float cosf(float x) { return static_cast<float>(Vkm::Engine::PortableMath::cos(x)); }
inline float atan2f(float y, float x) { return static_cast<float>(Vkm::Engine::PortableMath::atan2(y, x)); }

/// qsort as an insertion sort. The solver sorts each matrix row's coefficients with a compare that
/// answers 1 or 0, never below 0, which each C library's qsort orders its own way, so a row's products
/// add in another order. A row holds the unknowns that share a triangle with its own: it is short.
inline void qsort(void* base, size_t count, size_t size, int (*compare)(const void*, const void*)) {
    auto* bytes = static_cast<unsigned char*>(base);
    for (size_t i = 1; i < count; ++i) {
        for (size_t j = i; j > 0 && compare(bytes + (j - 1) * size, bytes + j * size) > 0; --j) {
            for (size_t b = 0; b < size; ++b) std::swap(bytes[(j - 1) * size + b], bytes[j * size + b]);
        }
    }
}

} // namespace xatlas::internal

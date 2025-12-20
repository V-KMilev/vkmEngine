#pragma once

#include <cstdint>

namespace Engine {

/**
 * @brief Base class for all resource types in the engine.
 *
 * This struct provides a minimal interface for resources/assets stored or managed by the engine.
 * It primarily defines a versioning mechanism for change tracking, hot-reloading, and cache validation.
 * Most resource types (e.g., textures, meshes, materials) should inherit from Resource.
 *
 * @note The version is intended to be incremented upon modification of the resource,
 *       typically by storage or resource manager code.
 */
struct Resource {
    public:
        Resource() = default;
        ~Resource() = default;

        Resource(const Resource& other) = default;
        Resource& operator=(const Resource& other) = default;

        Resource(Resource && other) = default;
        Resource& operator=(Resource && other) = default;

    public:
        uint64_t version = 1;
};

} // namespace Engine

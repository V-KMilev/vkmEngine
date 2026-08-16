#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace Engine {

/**
 * @brief Base class for all resource types in the engine.
 *
 * Resources carry a runtime version (for change tracking / hot reload /
 * GPU cache validation) and an optional name. The name is the serializable
 * identity of the asset - `ResourceManager::findByName<T>` resolves a name
 * back to a Handle on scene load. Code-generated assets may leave it empty
 * if they're not meant to survive serialization.
 */
struct Resource {
    public:
        // Rule-of-5 out-of-line: the source unique_ptr<json> needs the
        // full json type (only forward-declared here) to destruct +
        // copy-clone, so the special members are defined in resource.cpp.
        Resource();
        ~Resource();

        Resource(const Resource& other);
        Resource& operator=(const Resource& other);

        Resource(Resource && other) noexcept;
        Resource& operator=(Resource && other) noexcept;

    public:
        bool hasSource() const noexcept { return source != nullptr; }

        /**
         * @brief Mutable access to the source JSON, allocating an empty object
         * if the slot is null. Callers must #include <nlohmann/json.hpp>.
         */
        nlohmann::json&       sourceJson();

        /**
         * @brief Const access to the source JSON.
         *
         * Asserts hasSource() rather than allocating, since a const object
         * cannot lazily create the slot; guard the call with hasSource().
         *
         * @return Const reference to the existing source descriptor JSON.
         */
        const nlohmann::json& sourceJson() const;

    public:
        uint64_t    version    = 1;
        uint64_t    uid        = 0;             ///< Process-unique instance id stamped by ResourceManager::add. Identifies the asset itself, where a handle only identifies the slot it sits in.
        std::string name;                       ///< Serializable identity: scene files reference assets by name, resolved via ResourceManager::findByName. Kept unique within a type by add().
        bool        hidden     = false;         ///< Filtered from pickers / Asset Browser / scene save. See ResourceManager::addPrivate.
        std::unique_ptr<nlohmann::json> source; ///< Origin descriptor JSON, lazy-allocated.
};

} // namespace Engine

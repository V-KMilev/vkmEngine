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

        /// Mutable access to the source JSON, allocating an empty object
        /// if the slot is null. Callers must #include <nlohmann/json.hpp>.
        nlohmann::json&       sourceJson();

        /// Const access. Asserts hasSource(); use hasSource() to guard.
        const nlohmann::json& sourceJson() const;

    public:
        uint64_t    version    = 1;
        std::string name;                 ///< Serializable identity: scene files reference assets by name, resolved via ResourceManager::findByName. Kept unique within a type by add().
        bool        hidden     = false;   ///< Filtered from pickers / Asset Browser / scene save. See ResourceManager::addPrivate.

        /**
         * @brief Origin descriptor JSON, lazy-allocated.
         *
         * Stored behind a unique_ptr so this header only forward-declares
         * nlohmann::json - every TU that includes a Resource subclass
         * (MeshAsset, ...) avoids the ~10k-line json template tax.
         * Callers that read or write it must #include <nlohmann/json.hpp>
         * in their .cpp.
         *
         * Shape is loader-specific. Examples:
         *   {"kind":"generator","type":"sphere","segments":48,"rings":24}
         *   {"kind":"folder","path":"assets/PavingStones118_2K-JPG"}
         *   {"kind":"file","path":"assets/foo.png","sRGB":true}
         */
        std::unique_ptr<nlohmann::json> source;
};

} // namespace Engine

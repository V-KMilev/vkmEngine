#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace Engine {

/**
 * @brief Base class for all resource types in the engine.
 *
 * Resources carry a runtime version (for change tracking / hot reload /
 * GPU cache validation) and an optional name. The name is the serializable
 * identity of the asset — `ResourceManager::findByName<T>` resolves a name
 * back to a Handle on scene load. Code-generated assets may leave it empty
 * if they're not meant to survive serialization.
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
        uint64_t    version = 1;
        std::string name;

        /**
         * @brief Editor-internal resource flag.
         *
         * True for assets the editor creates for its own use (preview meshes,
         * neutral materials for thumbnails, etc.). Pickers, the Asset Browser,
         * and SceneSerializer all filter these out so they don't pollute the
         * user's asset graph or get written to scene saves.
         *
         * Default false: anything imported, authored, or loaded from disk is
         * the user's asset.
         */
        bool internal = false;

        /**
         * @brief Origin descriptor as a JSON object.
         *
         * Set by whatever generator or loader created this asset. A null
         * value means the origin is not tracked (the asset won't survive
         * a SceneSerializer save → cold-start load).
         *
         * Shape is loader-specific. Examples:
         *   {"kind":"generator","type":"sphere","segments":48,"rings":24}
         *   {"kind":"folder","path":"assets/PavingStones118_2K-JPG"}
         *   {"kind":"file","path":"assets/foo.png","sRGB":true}
         *
         * Consumed by Engine::AssetSerializer to recreate the asset on load.
         */
        nlohmann::json source;
};

} // namespace Engine

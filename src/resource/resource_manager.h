#pragma once

#include <type_traits>
#include <utility>

#include "l_assert.h"

#include "storage.h"

#include "mesh_asset.h"
#include "texture_asset.h"
#include "material_asset.h"

namespace Engine {

/**
 * @brief Manages all resource assets (meshes, textures, materials) using typed handles and type-safe storage.
 * 
 * The ResourceManager provides a unified interface to add, fetch, edit, commit (version bump), and
 * remove resources of different types. Internally, it dispatches these operations to type-specific storage
 * classes, using typed ResourceHandle types to ensure correctness and safety.
 * 
 * Supported resource types are MeshAsset, TextureAsset, and MaterialAsset, with their corresponding handle types.
 * 
 * This class is non-copyable and non-movable.
 */
class ResourceManager {
    public:
        ResourceManager() = default;
        ~ResourceManager() = default;

        ResourceManager(const ResourceManager& other) = delete;
        ResourceManager& operator=(const ResourceManager& other) = delete;

        ResourceManager(ResourceManager && other) = delete;
        ResourceManager& operator=(ResourceManager && other) = delete;

    public:
        /**
         * @brief Add a new resource (mesh, texture, or material) to the manager.
         * 
         * @tparam ResourceType The resource type (MeshAsset, TextureAsset, MaterialAsset).
         * @param resource The resource instance to add (will be moved).
         * @return The handle for the newly added resource.
         */
        template<class ResourceType>
        decltype(auto) add(ResourceType && resource) {
            using StorageType = std::remove_cv_t<std::remove_reference_t<ResourceType>>;
            return getStorage<StorageType>().add(std::forward<ResourceType>(resource));
        }

        /**
         * @brief Remove a resource by handle.
         * 
         * @tparam handleType The handle type associated with the resource.
         * @param handle Handle referencing the resource to remove.
         */
         template<class handleType>
         decltype(auto) remove(const handleType& handle) {
             using StorageType = typename handleType::resource_t;
             return getStorage<StorageType>().remove(handle);
         }

        /**
         * @brief Get const access to a resource by handle.
         * 
         * @tparam handleType The handle type associated with the resource.
         * @param handle Handle used to look up the resource.
         * @return const reference to the resource.
         */
        template<class handleType>
        decltype(auto) get(const handleType& handle) const {
            using StorageType = typename handleType::resource_t;
            return getStorage<StorageType>().get(handle);
        }

        /**
         * @brief Get mutable access to a resource for editing by handle.
         * 
         * @tparam handleType The handle type associated with the resource.
         * @param handle Handle used to look up the resource.
         * @return mutable reference to the resource.
         */
        template<class handleType>
        decltype(auto) edit(const handleType& handle) {
            using StorageType = typename handleType::resource_t;
            return getStorage<StorageType>().edit(handle);
        }

        /**
         * @brief Commit changes to a resource (increments the version).
         * 
         * @tparam handleType The handle type associated with the resource.
         * @param handle Handle referencing the resource to commit.
         */
        template<class handleType>
        void commit(const handleType& handle) {
            using StorageType = typename handleType::resource_t;
            getStorage<StorageType>().commit(handle);
        }

    private:
        /**
         * @brief Get mutable access to the appropriate storage for a resource type.
         * 
         * @tparam ResourceType The resource type.
         * @return Reference to the underlying Storage for the resource type.
         */
        template<typename ResourceType>
        auto& getStorage() {
            if constexpr (std::is_same_v<ResourceType, MeshAsset>) {
                return m_meshStorage;
            } else if constexpr (std::is_same_v<ResourceType, TextureAsset>) {
                return m_textureStorage;
            } else if constexpr (std::is_same_v<ResourceType, MaterialAsset>) {
                return m_materialStorage;
            } else {
                VKM_ASSERT(false, "Unsupported asset type");
            }
        }

        /**
         * @brief Get const access to the appropriate storage for a resource type.
         * 
         * @tparam ResourceType The resource type.
         * @return Const reference to the underlying Storage for the resource type.
         */
        template<typename ResourceType>
        const auto& getStorage() const {
            if constexpr (std::is_same_v<ResourceType, MeshAsset>) {
                return m_meshStorage;
            } else if constexpr (std::is_same_v<ResourceType, TextureAsset>) {
                return m_textureStorage;
            } else if constexpr (std::is_same_v<ResourceType, MaterialAsset>) {
                return m_materialStorage;
            } else {
                VKM_ASSERT(false, "Unsupported asset type");
            }
        }

    private:
        Storage<MeshAsset,     MeshHandle> m_meshStorage;
        Storage<TextureAsset,  TextureHandle> m_textureStorage;
        Storage<MaterialAsset, MaterialHandle> m_materialStorage;
};

} // namespace Engine
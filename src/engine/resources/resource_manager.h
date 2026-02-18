#pragma once

#include <type_traits>
#include <utility>

#include "l_assert.h"

#include "storage.h"
#include "resource.h"

#include "mesh_asset.h"
#include "texture_asset.h"
#include "material_asset.h"

namespace Engine {

/**
 * @brief Manages all resource assets (meshes, textures, materials) using typed handles and type-safe storage.
 *
 * The ResourceManager provides a unified interface to add, fetch, edit, commit (version bump), and
 * remove resources of different types. Internally, it dispatches these operations to type-specific
 * Engine::Storage instances, using typed Handle types to ensure correctness and safety.
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
        template<typename ResourceType>
        auto add(ResourceType && resource) {
            using T = std::remove_cv_t<std::remove_reference_t<ResourceType>>;

            StorageIndex key = getStorage<T>().add(std::forward<ResourceType>(resource));
            return Handle<T>{key};
        }

        /**
         * @brief Remove a resource by handle.
         *
         * @tparam HandleType The handle type associated with the resource.
         * @param handle Handle referencing the resource to remove.
         */
         template<typename HandleType>
         void remove(const HandleType& handle) {
             using T = typename HandleType::resource_t;
             getStorage<T>().remove(handle.key);
         }

        /**
         * @brief Get const access to a resource by handle.
         *
         * @tparam HandleType The handle type associated with the resource.
         * @param handle Handle used to look up the resource.
         * @return const reference to the resource.
         */
        template<typename HandleType>
        const auto& get(const HandleType& handle) const {
            using T = typename HandleType::resource_t;
            return getStorage<T>().get(handle.key);
        }

        /**
         * @brief Get mutable access to a resource for editing by handle.
         *
         * @tparam HandleType The handle type associated with the resource.
         * @param handle Handle used to look up the resource.
         * @return mutable reference to the resource.
         */
        template<typename HandleType>
        auto& edit(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            return getStorage<T>().get(handle.key);
        }

        /**
         * @brief Commit changes to a resource (increments the version).
         *
         * @tparam HandleType The handle type associated with the resource.
         * @param handle Handle referencing the resource to commit.
         */
        template<typename HandleType>
        void commit(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            static_assert(std::is_base_of_v<Resource, T>, "Resource type must inherit from Resource to use commit().");
            ++getStorage<T>().get(handle.key).version;
        }

    private:
        /**
         * @brief Get mutable access to the appropriate storage for a resource type.
         */
        template<typename T>
        auto& getStorage() {
            if constexpr (std::is_same_v<T, MeshAsset>) {
                return m_meshStorage;
            } else if constexpr (std::is_same_v<T, TextureAsset>) {
                return m_textureStorage;
            } else if constexpr (std::is_same_v<T, MaterialAsset>) {
                return m_materialStorage;
            } else {
                VKM_ASSERT(false, "Unsupported asset type");
            }
        }

        /**
         * @brief Get const access to the appropriate storage for a resource type.
         */
        template<typename T>
        const auto& getStorage() const {
            if constexpr (std::is_same_v<T, MeshAsset>) {
                return m_meshStorage;
            } else if constexpr (std::is_same_v<T, TextureAsset>) {
                return m_textureStorage;
            } else if constexpr (std::is_same_v<T, MaterialAsset>) {
                return m_materialStorage;
            } else {
                VKM_ASSERT(false, "Unsupported asset type");
            }
        }

    private:
        Storage<MeshAsset>     m_meshStorage;
        Storage<TextureAsset>  m_textureStorage;
        Storage<MaterialAsset> m_materialStorage;
};

} // namespace Engine

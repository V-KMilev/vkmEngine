#pragma once

#include <vector>
#include <stdexcept>

#include "resource.h"

namespace Engine {

/**
 * @brief Manages and stores render assets (meshes, textures, and materials) in memory.
 *
 * Provides type-safe handles for runtime access, version tracking, and safe mutable edits.
 * Handles are 1-based and refer to underlying asset arrays. Editing assets via edit* methods
 * requires calling the corresponding commit* method to increment asset versioning.
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
         * @brief Add a mesh asset to the manager.
         * @param mesh Mesh asset data to store.
         * @return MeshHandle Handle referencing the stored mesh asset.
         */
        MeshHandle addMesh(const MeshAsset& mesh);

        /**
         * @brief Add a texture asset to the manager.
         * @param tex Texture asset data to store.
         * @return TextureHandle Handle referencing the stored texture asset.
         */
        TextureHandle addTexture(const TextureAsset& tex);

        /**
         * @brief Add a material asset to the manager.
         * @param mat Material asset data to store.
         * @return MaterialHandle Handle referencing the stored material asset.
         */
        MaterialHandle addMaterial(const MaterialAsset& mat);

        /**
         * @brief Get read-only access to a mesh asset by handle.
         * @param handle MeshHandle referring to mesh.
         * @return const MeshAsset& Const reference to mesh asset.
         */
        const MeshAsset& getMesh(const MeshHandle& handle) const;

        /**
         * @brief Get read-only access to a texture asset by handle.
         * @param handle TextureHandle referring to texture.
         * @return const TextureAsset& Const reference to texture asset.
         */
        const TextureAsset& getTexture(const TextureHandle& handle) const;

        /**
         * @brief Get read-only access to a material asset by handle.
         * @param handle MaterialHandle referring to material.
         * @return const MaterialAsset& Const reference to material asset.
         */
        const MaterialAsset& getMaterial(const MaterialHandle& handle) const;

        /**
         * @brief Get mutable access to a mesh asset by handle for editing.
         * Call commitMesh(handle) after editing to increment version.
         * @param handle MeshHandle referring to mesh.
         * @return MeshAsset& Reference to mesh asset for editing.
         */
        MeshAsset& editMesh(const MeshHandle& handle);

        /**
         * @brief Get mutable access to a texture asset by handle for editing.
         * Call commitTexture(handle) after editing to increment version.
         * @param handle TextureHandle referring to texture.
         * @return TextureAsset& Reference to texture asset for editing.
         */
        TextureAsset& editTexture(const TextureHandle& handle);

        /**
         * @brief Get mutable access to a material asset by handle for editing.
         * Call commitMaterial(handle) after editing to increment version.
         * @param handle MaterialHandle referring to material.
         * @return MaterialAsset& Reference to material asset for editing.
         */
        MaterialAsset& editMaterial(const MaterialHandle& handle);

        /**
         * @brief Commit changes to a mesh asset after editing, bumping its version.
         * @param handle MeshHandle referring to mesh.
         */
        void commitMesh(const MeshHandle& handle);

        /**
         * @brief Commit changes to a texture asset after editing, bumping its version.
         * @param handle TextureHandle referring to texture.
         */
        void commitTexture(const TextureHandle& handle);

        /**
         * @brief Commit changes to a material asset after editing, bumping its version.
         * @param handle MaterialHandle referring to material.
         */
        void commitMaterial(const MaterialHandle& handle);

    private:
        /**
         * @brief Convert MeshHandle to array index (0-based).
         * Throws std::runtime_error if the handle is invalid.
         * @param handle MeshHandle referencing mesh.
         * @return size_t Asset array index.
         */
        static size_t idx(const MeshHandle& handle);

        /**
         * @brief Convert TextureHandle to array index (0-based).
         * Throws std::runtime_error if the handle is invalid.
         * @param handle TextureHandle referencing texture.
         * @return size_t Asset array index.
         */
        static size_t idx(const TextureHandle& handle);

        /**
         * @brief Convert MaterialHandle to array index (0-based).
         * Throws std::runtime_error if the handle is invalid.
         * @param handle MaterialHandle referencing material.
         * @return size_t Asset array index.
         */
        static size_t idx(const MaterialHandle& handle);

    private:
        std::vector<MeshAsset>     m_meshes;
        std::vector<TextureAsset>  m_textures;
        std::vector<MaterialAsset> m_materials;
};

} // namespace Engine
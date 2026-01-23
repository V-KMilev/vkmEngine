#pragma once

#include <unordered_map>
#include <cstdint>
#include <memory>

#include "mesh_asset.h"
#include "material_asset.h"
#include "texture_asset.h"

#include "resources/gl_mesh.h"
#include "resources/gl_material.h"
#include "resources/gl_texture.h"
#include "resources/gl_lights.h"
#include "gl_instance_batcher.h"

namespace Engine {
    class RenderView;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief GLView manages the OpenGL-side resources and synchronization for all
 * drawable mesh and material instances required for a render pass.
 * 
 * This class maintains an up-to-date mapping between engine-level mesh/material handles
 * and concrete OpenGL mesh/material objects (GLMesh/GLMaterial). It ensures all required
 * GPU resources are available and updated, coordinating with the resource manager and current scene data.
 */
class GLView {
    public:
        GLView() = default;
        ~GLView();

        GLView(const GLView& other) = delete;
        GLView& operator=(const GLView& other) = delete;

        GLView(GLView && other) = delete;
        GLView& operator=(GLView && other) = delete;

    public:
        /**
        * @brief Synchronize OpenGL mesh objects with those referenced in the current RenderView.
        *
        * For each mesh handle in renderView, ensures a corresponding GLMesh exists and is up-to-date
        * with its latest version from the resource manager. Creates a new GLMesh or updates an
        * existing one as necessary.
        *
        * @param renderView The view description for the current render pass.
        * @param resourceManager The resource manager for accessing mesh assets.
        */
        void syncMeshes(const RenderView& renderView, const ResourceManager& resourceManager);

        /**
        * @brief Synchronize OpenGL material objects with those referenced in the current RenderView.
        *
        * For each material handle in renderView, ensures a corresponding GLMaterial exists and is up-to-date
        * with its latest version from the resource manager. Creates a new GLMaterial or updates an
        * existing one as necessary.
        *
        * @param renderView The view description for the current render pass.
        * @param resourceManager The resource manager for accessing material assets.
        */
        void syncMaterials(const RenderView& renderView, const ResourceManager& resourceManager);

        /**
        * @brief Synchronize OpenGL texture objects with those referenced in materials.
        *
        * For each texture handle referenced in materials, ensures a corresponding GLTexture exists and is up-to-date
        * with its latest version from the resource manager. Creates a new GLTexture or updates an
        * existing one as necessary.
        *
        * @param renderView The view description for the current render pass.
        * @param resourceManager The resource manager for accessing texture assets.
        */
        void syncTextures(const RenderView& renderView, const ResourceManager& resourceManager);

        /**
        * @brief Obtain the OpenGL mesh (GLMesh) for the given engine mesh handle.
        *
        * Returns nullptr if the mesh is not found (should be called after syncMeshes).
        *
        * @param handle The mesh handle to look up.
        * @return Pointer to the corresponding GLMesh object, or nullptr if not found.
        */
        const GLMesh* getMesh(const MeshHandle& handle) const;

        /**
         * @brief Obtain the OpenGL material (GLMaterial) for the given engine material handle.
         *
         * Returns nullptr if the material is not found (should be called after syncMaterials).
         *
         * @param handle The material handle to look up.
         * @return Pointer to the corresponding GLMaterial object, or nullptr if not found.
         */
        const GLMaterial* getMaterial(const MaterialHandle& handle) const;

        /**
         * @brief Synchronize lights with those in the current RenderView.
         *
         * Updates the lights uniform buffer with all lights from the RenderView and uploads to GPU.
         *
         * @param renderView The view description for the current render pass.
         * @param resourceManager The resource manager (unused for lights, kept for consistency).
         */
         void syncLights(const RenderView& renderView, const ResourceManager& resourceManager);

        /**
         * @brief Obtain the OpenGL texture (GLTexture) for the given engine texture handle.
         *
         * Returns nullptr if the texture is not found (should be called after syncTextures).
         *
         * @param handle The texture handle to look up.
         * @return Pointer to the corresponding GLTexture object, or nullptr if not found.
         */
        const GLTexture* getTexture(const TextureHandle& handle) const;

        /**
         * @brief Get the GLLights object for binding.
         */
        const GLLights& getLights() const { return m_lights; }

        /**
         * @brief Builds instance batches from the current render view.
         *
         * Groups drawables by (mesh, material) and updates instance buffers.
         * Must be called after syncMeshes/syncMaterials.
         *
         * @param renderView The render view with sorted drawables.
         */
        void buildInstanceBatches(const RenderView& renderView);

        /**
         * @brief Returns the instance batcher for accessing batches.
         */
        GLInstanceBatcher& getInstanceBatcher() { return m_instanceBatcher; }
        const GLInstanceBatcher& getInstanceBatcher() const { return m_instanceBatcher; }

        /**
         * @brief Gets a mutable mesh for attaching instance buffers.
         *
         * @param handle The mesh handle to look up.
         * @return Pointer to the GLMesh, or nullptr if not found.
         */
        GLMesh* getMutableMesh(const MeshHandle& handle);

    private:
        std::unordered_map<uint32_t, std::unique_ptr<GLMesh>> m_meshes;
        std::unordered_map<uint32_t, uint64_t> m_meshVersions;

        std::unordered_map<uint32_t, std::unique_ptr<GLMaterial>> m_materials;
        std::unordered_map<uint32_t, uint64_t> m_materialVersions;

        std::unordered_map<uint32_t, std::unique_ptr<GLTexture>> m_textures;
        std::unordered_map<uint32_t, uint64_t> m_textureVersions;

        GLLights m_lights;

        GLInstanceBatcher m_instanceBatcher;
};

} // namespace Engine
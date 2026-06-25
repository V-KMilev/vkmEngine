#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "resource/asset/mesh_asset.h"      // MeshHandle
#include "resource/asset/material_asset.h"  // MaterialHandle
#include "resource/asset/texture_asset.h"   // TextureHandle

#include "data/gl_mesh.h"
#include "data/gl_material.h"
#include "data/gl_texture.h"

namespace Core {
    class Texture2D;
}

namespace Engine {
    struct RenderView;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief Versioned, handle-indexed table of one GL resource kind.
 *
 * @tparam GLT The GL resource type stored (GLMesh, GLMaterial, GLTexture).
 */
template <typename GLT>
struct GLResourceTable {
    std::vector<std::unique_ptr<GLT>> entries;
    std::vector<uint64_t>             versions;
    std::vector<uint32_t>             generations;  ///< handle generation per slot; mismatch == slot recycled
};

/**
 * @brief GPU-side mirror of the assets a frame references.
 *
 * One table per asset kind, indexed by handle.id() and version-gated: an asset
 * is uploaded the first time a frame references it and re-uploaded only when its
 * version changes. This is the only place the backend reads ResourceManager;
 * the render path resolves a drawable's handles to these GPU objects.
 */
class GLView {
    public:
        GLView() = default;
        ~GLView() = default;

        GLView(const GLView& other) = delete;
        GLView& operator=(const GLView& other) = delete;

        GLView(GLView && other) = delete;
        GLView& operator=(GLView && other) = delete;

    public:
        /**
         * @brief Upload / refresh every mesh, material and texture `view` references.
         *
         * @param view The render view to sync.
         * @param resources The resource manager to use.
         */
        void sync(const RenderView& view, const ResourceManager& resources);

        const GLMesh*     getMesh(const MeshHandle& handle) const;
        const GLMaterial* getMaterial(const MaterialHandle& handle) const;
        const Core::Texture2D* getTexture(const TextureHandle& handle) const;

    private:
        /**
         * @brief Upload or refresh a single asset into its table, version-gated.
         *
         * Upload `handle`'s asset into `table` on first sight, or update it when
         * the asset's version has moved on since the cached copy.
         *
         * @tparam GLT The type of the GL resource.
         * @tparam AssetT The type of the asset.
         * @param table The table to ensure.
         * @param handle The handle to ensure.
         * @param resources The resource manager to use.
         */
        template <typename GLT, typename AssetT>
        void ensure(GLResourceTable<GLT>& table, const Handle<AssetT>& handle, const ResourceManager& resources);

    private:
        GLResourceTable<GLMesh>     m_meshes;
        GLResourceTable<GLMaterial> m_materials;
        GLResourceTable<GLTexture>  m_textures;
};

} // namespace Engine

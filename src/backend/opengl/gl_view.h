#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/asset/font_asset.h"

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
    struct Slot {
        std::unique_ptr<GLT> gl;
        uint64_t             version    = 0;
        uint32_t             generation = 0;  ///< handle generation; mismatch == slot recycled
    };
    std::vector<Slot> slots;  ///< indexed by handle.id()
};

/**
 * @brief GPU-side mirror of the assets a frame references.
 *
 * One table per asset kind, indexed by handle.id() and version-gated: an asset
 * is uploaded the first time a frame references it and re-uploaded only when its
 * version changes. This is the only place the backend reads ResourceManager;
 * the render path resolves a drawable's handles to these GPU objects.
 *
 * The version gate is only meaningful within a single asset graph. A scene load
 * or editor play-stop restore swaps the whole graph, and the incoming one
 * restarts its handles and versions from scratch - so every table is dropped
 * when ResourceManager::epoch() moves and rebuilt against the new graph.
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

        /**
         * @brief Resolve a handle to its synced GPU object; null if the handle is empty
         * or its asset has not been sync()'d into the table yet. The returned
         * pointer is owned by this table - do not store it across a sync().
         */
        const GLMesh*     getMesh(const MeshHandle& handle) const;
        const GLMaterial* getMaterial(const MaterialHandle& handle) const;
        const Core::Texture2D* getTexture(const TextureHandle& handle) const;
        const Core::Texture2D* getFontAtlas(const FontHandle& handle) const;

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

        /**
         * @brief Drop every cached GPU object when @p resources swapped graphs.
         *
         * A no-op while the epoch is unchanged, which is every frame but the
         * first one after a load.
         *
         * @param resources The resource manager this view mirrors.
         */
        void invalidateOnEpochChange(const ResourceManager& resources);

    private:
        GLResourceTable<GLMesh>     m_meshes;
        GLResourceTable<GLMaterial> m_materials;
        GLResourceTable<GLTexture>  m_textures;
        GLResourceTable<GLTexture>  m_fontAtlases;  ///< SDF atlases keyed by FontHandle (fonts carry pixels, not TextureAssets).

        uint64_t m_epoch = 0;  ///< Asset-graph identity these tables were built against; 0 = never synced.
};

} // namespace Engine
